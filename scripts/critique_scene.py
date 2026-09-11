"""Judge a running scene against what a scene has to be to look expensive.

    AE3D_AGENT=7911 ./build/zombie_street &
    python scripts/critique_scene.py --port 7911

Nothing here is read off a screenshot. A picture tells a person that a wall
looks wrong and tells a program nothing at all; the numbers behind that
impression are texel density, silhouette and proportion, and every one of them
is a property of the scene the engine can be asked about directly.

The standards below are the ones a real art review applies, with the number
each of them is:

  Texel density. How many pixels of texture cross a metre of surface. At 100 a
  brick is a smear you can count the texels of; at 512 the surface holds up at
  arm's length, which is where this camera stands. Consistency matters as much
  as the absolute figure -- a scene whose densities span two orders of
  magnitude reads as parts from different games, whatever any one surface
  measures.

  Relief. How many distinct planes a shape's faces lie in. Counting directions
  cannot tell a box from a facade -- a wall with recessed windows, sills and a
  cornice is built from faces pointing the same six ways the box did -- but a
  box's faces lie in six planes and a facade's in hundreds. This is the measure
  that says "it is a slab" without anybody having to look at it.

  Density of geometry. Triangles for each square metre of surface. A figure
  carries its detail where it is seen, so a hand and a wall cannot be held to
  one number; what can be held is that a character is not a prop.

Exits non-zero with a line naming what failed.
"""

import argparse
import math
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from ae3d_session import Session, AgentError

# A texel every two millimetres is what a surface needs to survive being stood
# next to. Half of that is the floor below which a surface is visibly soft.
WANT_TEXELS = 512.0
FLOOR_TEXELS = 256.0
# Densities further apart than this read as different games in one frame.
SPREAD = 8.0
# A shape lying in no more planes than a cube does is a cube.
BOX_PLANES = 8
# What a figure has to carry to read as one rather than as a stack of props.
CHARACTER_TRIANGLES = 20000
CHARACTER_PLANES = 3000
# A planted foot is planted. These are the tolerances a foot-locked walk keeps;
# a leg swung by a curve against a body moving at its own speed misses them by
# an order of magnitude.
# How close to the bottom of its travel a joint has to be, and how still
# vertically, to count as standing on the road.
CONTACT_BAND = 0.020
CONTACT_SETTLE = 0.002

SLIDE_PER_FRAME = 0.025
SLIDE_PER_CONTACT = 0.040
# How far the shape of a drawn figure may stray from the shape of its skeleton.
# Wide, deliberately. The mesh reaches past the bones by the thickness of the
# surface, and a camera looking at a figure from an angle sees less of its width
# than the world span says -- head on this scene measures 1.05, obliquely 0.6.
# What is being caught here is not a few per cent but a figure deformed twice,
# which drew four times its own shape and every count it had still read right.
SHAPE_LOW = 0.35
SHAPE_HIGH = 2.5
# What the engine is set to, and how much of a difference that has to make. A
# map that is loaded and a map that reaches the frame are different facts, and
# this asserts the second one.
#
# How much is deliberately modest, because it depends on how big the frame is:
# the same maps move the worst pixel 0.094 at 1280x720 and 0.035 at the 320x180
# a CI draws at, since minification averages the relief away. What is being
# asked is whether they arrive at all, not how loud they are.
NORMAL_STRENGTH = 2.5
NORMAL_VISIBLE = 0.025
# Baked occlusion, measured the same way and from the scene's own camera.
OCCLUSION_VISIBLE = 0.03
# What a frame of this scene is allowed to cost, in the units that mean the same
# thing on every machine. Set above what it costs now with room to grow, and
# below what would be a different scene: 95 draws, 36,106 triangles, 4 program
# changes today.
# The scene is the engine's benchmark, so its draw count is the load it exists
# to carry, not an accident to be trimmed: a terrace with a roofline, downpipes
# and a dressed pavement draws in the low hundreds and is meant to. This is a
# ceiling against a scene that has quietly gone wrong -- a texture bound per
# face, a merge that stopped merging -- while tools/ae3d_bench.ae holds the
# exact figure and fails the build on a single draw more than last recorded.
MAX_DRAWS = 320
MAX_TRIANGLES = 60000
MAX_PROGRAM_CHANGES = 12
# How much sharper than its own busiest movement a bone may move between two
# samples. This walk measures 2.0; a leg interpolating the long way round
# measured 37 to 45, and so did a reading taken while the pose was half built.
SPIN_LIMIT = 8
# How far off upright a head may lean. A person looking down at their feet is
# about 40; anything past 60 is a head that has come off its neck.
HEAD_LEAN = 55
# The pelvis rises and falls as the weight goes from one leg to the other. Too
# little and the figure glides along a rail; too much and it pogos. A metre of
# a body's height moves a couple of centimetres a step.
WEIGHT_BOB_LOW = 0.010
WEIGHT_BOB_HIGH = 0.20
# The strike has to reach. The swiping arm straightens at the peak of the lunge
# and extends further than it ever does shuffling, by at least this. An arm is
# about 0.55 m and a right-angle bend shortens it to 0.39, so the most a
# straightening can add is around 0.16; 0.12 is a clear thrust and not a lean.
STRIKE_REACH = 0.12
# The head follows the body rather than leading it. The best alignment between
# how the head turns and how the hips turn is at a lag of at least this many
# frames -- a head that turns with the pelvis, or before it, belongs to a
# mannequin.
FOLLOW_LAG = 1

FAILURES = []


def check(name, ok, detail=""):
    print("  %-4s %s%s" % ("ok" if ok else "FAIL", name, (" (%s)" % detail) if detail else ""))
    if not ok:
        FAILURES.append(name)


def group_of(name):
    return name.split("_", 1)[0] if "_" in name else name


def texel_density(models):
    print("== how much texture crosses a metre of surface ==")
    textured = [m for m in models if m.get("texels_per_m")]
    if not textured:
        check("every surface carries a measurable texture", False, "none reported one")
        return
    worst = sorted(textured, key=lambda m: m["texels_per_m"])
    for m in worst[:6]:
        print("  %-22s %-14s %6.0f texels/m  %.2f repeats/m over %.1f m2"
              % (m["name"], m.get("material", "-"), m["texels_per_m"],
                 m["uv_repeats_per_m"], m["area_m2"]))
    low = worst[0]
    check("no surface is softer than a texel every four millimetres",
          low["texels_per_m"] >= FLOOR_TEXELS,
          "%s is %.0f, wanted %.0f" % (low["name"], low["texels_per_m"], FLOOR_TEXELS))

    high = worst[-1]
    spread = high["texels_per_m"] / max(low["texels_per_m"], 1.0)
    check("the scene is textured to one standard", spread <= SPREAD,
          "%.0f to %.0f is %.0f times, wanted %.0f"
          % (low["texels_per_m"], high["texels_per_m"], spread, SPREAD))

    short = [m for m in textured if m["texels_per_m"] < WANT_TEXELS]
    check("every surface holds up at arm's length", not short,
          "%d of %d below %.0f" % (len(short), len(textured), WANT_TEXELS))


def relief(models):
    print("\n== what the shapes are, rather than what they are painted with ==")
    # Big enough to fill a frame, and flat enough to have nothing in it. A pane
    # of glass lies in one plane and is meant to.
    slabs = [m for m in models
             if m.get("planes", 0) <= BOX_PLANES and m.get("area_m2", 0.0) > 8.0
             and "Glass" not in m["name"] and "Lit" not in m["name"]]
    for m in sorted(models, key=lambda m: -m.get("area_m2", 0.0))[:8]:
        print("  %-22s %6d triangles, %5d planes, %9.1f m2"
              % (m["name"], m.get("triangles", 0), m.get("planes", 0), m["area_m2"]))
    check("nothing large enough to fill a frame is a bare slab", not slabs,
          "%d of them, %s" % (len(slabs), ", ".join(m["name"] for m in slabs[:4])))


def character(models, prefix):
    print("\n== the figure ==")
    parts = [m for m in models if m["name"].startswith(prefix)]
    if not parts:
        check("the scene has a figure in it", False, "nothing named %s" % prefix)
        return
    triangles = sum(m.get("triangles", 0) for m in parts)
    planes = sum(m.get("planes", 0) for m in parts)
    area = sum(m.get("area_m2", 0.0) for m in parts)
    print("  %d part(s), %d triangles, %d planes, %.2f m2 of surface"
          % (len(parts), triangles, planes, area))
    check("the figure carries the geometry a figure needs",
          triangles >= CHARACTER_TRIANGLES,
          "%d triangles, wanted %d" % (triangles, CHARACTER_TRIANGLES))
    check("and enough of a silhouette to read as one",
          planes >= CHARACTER_PLANES,
          "%d distinct planes, wanted %d" % (planes, CHARACTER_PLANES))
    check("and is one surface rather than a pile of parts", len(parts) <= 2,
          "%d separate models" % len(parts))


def proportion(models, prefix):
    """A figure is judged against itself: heads tall, and limb against limb."""
    by_name = {m["name"]: m for m in models if m["name"].startswith(prefix)}
    if not by_name:
        return
    def size(name, axis):
        m = by_name.get(prefix + name)
        return m["size"][axis] if m and m.get("size") else 0.0

    head = size("Head", 1)
    if head <= 0.0:
        return
    print("\n== proportion ==")
    standing = max((m["size"][1] for m in by_name.values() if m.get("size")), default=0.0)
    thigh, shin = size("LegUpperL", 1), size("LegLowerL", 1)
    upper, fore = size("ArmUpperL", 1), size("ArmLowerL", 1)
    print("  head %.2f m, thigh %.2f, shin %.2f, upper arm %.2f, forearm %.2f"
          % (head, thigh, shin, upper, fore))
    if thigh > 0.0 and shin > 0.0:
        check("the shin is close to the thigh in length",
              0.8 <= shin / thigh <= 1.05, "shin/thigh %.2f" % (shin / thigh))
    if upper > 0.0 and fore > 0.0:
        check("and the forearm a little shorter than the upper arm",
              0.75 <= fore / upper <= 1.0, "forearm/upper %.2f" % (fore / upper))


def budget(engine):
    """What a frame costs, against what it is allowed to cost.

    Milliseconds cannot be budgeted across machines: this scene draws in 4.6 ms
    on a workstation and 412 on a runner rasterising in software, so a threshold
    that holds on one is meaningless on the other. Draw calls, triangles and
    state changes mean the same thing everywhere, and they are what a scene
    actually regresses by -- somebody splits a mesh, or gives every object its
    own material, and the count moves before anybody notices the time.

    The milliseconds are printed beside them, split by pass, so that when a
    count is exceeded there is something to say about where the time went. They
    are CPU-side: the GPU is not waited on, and a driver that stalls charges the
    wait to whichever call blocks, so the split says where the frame was
    submitted rather than where it was rendered.
    """
    print("\n== what a frame costs ==")
    engine("frame.capture")
    stats = engine("frame.stats")
    passes = stats["shadow_ms"] + stats["scene_ms"] + stats["post_ms"]
    print("  %d draws, %d triangles, %d program / %d material / %d texture changes"
          % (stats["draw_calls"], stats["triangles"], stats["program_binds"],
             stats["material_binds"], stats["texture_binds"]))
    print("  shadow %.2f ms, scene %.2f ms, post %.2f ms, submitted in %.2f ms"
          % (stats["shadow_ms"], stats["scene_ms"], stats["post_ms"], passes))
    check("the scene draws within its budget of draw calls",
          stats["draw_calls"] <= MAX_DRAWS,
          "%d draws, allowed %d" % (stats["draw_calls"], MAX_DRAWS))
    check("and within its budget of triangles",
          stats["triangles"] <= MAX_TRIANGLES,
          "%d triangles, allowed %d" % (stats["triangles"], MAX_TRIANGLES))
    check("and does not thrash the program it draws with",
          stats["program_binds"] <= MAX_PROGRAM_CHANGES,
          "%d program changes, allowed %d"
          % (stats["program_binds"], MAX_PROGRAM_CHANGES))
    check("and changes material no more often than it draws",
          stats["material_binds"] <= stats["draw_calls"],
          "%d material changes against %d draws"
          % (stats["material_binds"], stats["draw_calls"]))


def surface_relief(engine, models, lit_from=(0.0, 1.6, 3.0), lit_at=(0.0, 0.0, 0.0)):
    """Whether surfaces have a shape as well as a colour, and whether it lands.

    A normal map that a material names and the renderer never loaded is a wall
    lit as the one flat plane it is, and nothing about its colour says so. So
    both facts are asked for: that the maps are there, and that turning them off
    changes the picture -- because loaded and reaching the frame are different
    things, and only the second one is worth having.
    """
    print("\n== the shape of the surfaces ==")
    big = [m for m in models
           if m.get("texture") and m.get("area_m2", 0.0) > 8.0
           and "Glass" not in m["name"] and "Lit" not in m["name"]]
    without = [m["name"] for m in big if not m.get("normal")]
    unloaded = [m["name"] for m in big if m.get("normal") and not m.get("normal_loaded")]
    print("  %d of %d surfaces over 8 m2 carry a normal map"
          % (len(big) - len(without), len(big)))
    check("every surface big enough to stand next to has a normal map", not without,
          "%d without: %s" % (len(without), ", ".join(without[:4])))
    check("and the renderer loaded every one it names", not unloaded,
          ", ".join(unloaded[:4]))

    # Measured from somewhere the light actually falls. A normal map changes
    # how a surface catches light, so a view down a dark street at grazing
    # angles measures almost nothing and says nothing either -- the honest place
    # to ask is a lit surface seen face on.
    was = engine("camera.get")
    try:
        engine("render.set", normal_strength=0.0)
    except AgentError as refused:
        check("the maps can be turned off to see whether they matter", False, str(refused))
        return
    engine("camera.set", position=list(lit_from), look_at=list(lit_at))
    engine("frame.hold")
    engine("render.set", normal_strength=NORMAL_STRENGTH)
    moved = engine("frame.diff", tolerance=2)
    # camera.get answers a direction, not a point, so the way back is a point
    # along it. Left where it was found, because everything after this measures
    # the scene as the demo actually frames it.
    home = [was["position"][i] + was["front"][i] * 4.0 for i in range(3)]
    engine("camera.set", position=was["position"], look_at=home)
    print("  under a lamp they move %.1f%% of the frame, worst %.3f"
          % (moved["fraction"] * 100, moved["max_delta"]))
    check("and the maps reach the picture",
          moved["max_delta"] >= NORMAL_VISIBLE,
          "the worst pixel moves %.3f, wanted %.3f"
          % (moved["max_delta"], NORMAL_VISIBLE))

    # Ambient occlusion, the same way. Ambient is the dimmest light in a scene
    # and the one that reaches everywhere, so without this a corner is as bright
    # as an open wall and every surface reads flat wherever the lamps do not
    # fall. Measured from the scene's own camera, because occlusion darkens what
    # is already in shadow and that is most of the frame.
    engine("render.set", occlusion_strength=0.0)
    engine("frame.hold")
    engine("render.set", occlusion_strength=1.0)
    shut = engine("frame.diff", tolerance=2)
    print("  occlusion moves %.1f%% of the frame, worst %.3f"
          % (shut["fraction"] * 100, shut["max_delta"]))
    check("and the corners are darker than the open walls",
          shut["max_delta"] >= OCCLUSION_VISIBLE,
          "the worst pixel moves %.3f, wanted %.3f"
          % (shut["max_delta"], OCCLUSION_VISIBLE))


def silhouette(engine, mesh, columns=52, rows=44):
    """What the figure actually covers on screen, against what it should.

    Every other measure here counts things: triangles, planes, texels. A mesh
    can have all of them right and still be wrong, because none of them says
    what shape it is -- and a figure deformed twice reads as 26,000 triangles
    and 9,000 planes exactly like a figure deformed once. This is the measure
    that says what is there.

    Two facts, both cheap. A silhouette is compared against the box the
    skeleton says the figure occupies, so a mesh spread over tens of metres by
    a pose baked in twice is caught however many triangles it has. And it is
    checked for being in one piece, because a figure that has come apart is in
    several.
    """
    print("\n== the figure on screen ==")
    e = engine

    def grid():
        return e("frame.grid", columns=columns, rows=rows)["cells"]

    e("scene.isolate", object=mesh)
    with_it = grid()
    e("scene.isolate", object="__nothing__")
    without = grid()
    e("scene.isolate")
    mask = [[sum(abs(a[i] - b[i]) for i in range(3)) > 0.015
             for a, b in zip(ra, rb)] for ra, rb in zip(with_it, without)]

    cells = [(x, y) for y, row in enumerate(mask) for x, on in enumerate(row) if on]
    if not cells:
        check("the figure is on screen at all", False, "nothing was drawn for %s" % mesh)
        return
    left = min(x for x, _ in cells)
    right = max(x for x, _ in cells)
    top = min(y for _, y in cells)
    bottom = max(y for _, y in cells)

    stats = e("frame.stats")
    drawn = ((right - left + 1) / float(columns), (bottom - top + 1) / float(rows))
    print("  covers %d cells, %.0f%% of the frame across and %.0f%% down"
          % (len(cells), drawn[0] * 100, drawn[1] * 100))

    # The shape of what was drawn against the shape of what is meant to have
    # drawn it. Both are ratios, so neither depends on where the camera stands
    # or how big the frame is, and a mesh spread over tens of metres by a pose
    # baked in twice disagrees with its own skeleton by a factor, not a few per
    # cent. This is the one measure that catches a figure that has exploded:
    # every count it has is still right.
    bones = [x["world_position"] for x in e("scene.skeleton", object=mesh)["bones"]]
    across = max(max(v[0] for v in bones) - min(v[0] for v in bones),
                 max(v[2] for v in bones) - min(v[2] for v in bones))
    upright = max(v[1] for v in bones) - min(v[1] for v in bones)
    wide_px = (right - left + 1) * stats["width"] / float(columns)
    tall_px = (bottom - top + 1) * stats["height"] / float(rows)
    if upright > 1e-6 and tall_px > 0.0:
        want = across / upright
        got = wide_px / tall_px
        ratio = got / max(want, 1e-6)
        print("  its bones span %.2f across by %.2f up (%.2f); it drew %.2f"
              % (across, upright, want, got))
        check("the figure is the shape its skeleton is",
              SHAPE_LOW <= ratio <= SHAPE_HIGH,
              "drew %.2f against %.2f, which is %.1f times" % (got, want, ratio))

    box = None
    entry = next((m for m in e("scene.tree", detail=True)["models"] if m["name"] == mesh), None)
    if entry:
        box = entry.get("screen_region")
    if box:
        # The bones say where the figure is and how big; the pixels say where it
        # drew and how big. A mesh posed twice, or drawn through a transform its
        # skeleton does not know about, disagrees with them.
        wide = (box["width"] / float(stats["width"])) * 1.35 + 0.06
        tall = (box["height"] / float(stats["height"])) * 1.35 + 0.06
        check("the figure draws no bigger than its bones say it is",
              drawn[0] <= wide and drawn[1] <= tall,
              "drew %.0f%% x %.0f%%, allowed %.0f%% x %.0f%%"
              % (drawn[0] * 100, drawn[1] * 100, wide * 100, tall * 100))


    # One piece. Anything that has come off a figure is a separate island.
    seen = set()
    islands = []
    for start in cells:
        if start in seen:
            continue
        stack, size = [start], 0
        seen.add(start)
        while stack:
            x, y = stack.pop()
            size += 1
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                near = (x + dx, y + dy)
                if near in seen or near not in set(cells):
                    continue
                seen.add(near)
                stack.append(near)
        islands.append(size)
    big = [n for n in islands if n >= max(3, len(cells) // 20)]
    check("the figure is in one piece", len(big) <= 1,
          "%d pieces of it, sizes %s" % (len(big), sorted(big, reverse=True)[:4]))

    # A standing figure is taller than it is wide. One lying in a heap is not.
    check("and stands taller than it is wide",
          (bottom - top) >= (right - left),
          "%d cells tall against %d wide" % (bottom - top + 1, right - left + 1))


def limbs(engine, mesh, figure_prefix, columns=64, rows=48):
    """How many legs the figure has, counted off the picture.

    This exists because it had three. A joint with no thickness was still grown
    around, so a limb ran from the hips to the ground between the legs -- in
    every frame, with the triangle count, the plane count, the texel density,
    the proportion and the foot planting all reading correctly. Nothing here
    could see it, and it took being told.

    Counted as runs of covered cells across the lower third of the silhouette,
    at the widest point of the stride, which is where legs are separate.
    """
    print("\n== how many legs ==")
    worst = 0
    at = 0.0
    for tenth in range(10):
        moment = tenth * 0.26
        engine("anim.set", time=moment)
        bones = {b["name"]: b["world_position"]
                 for b in engine("scene.skeleton", object=mesh)["bones"]}
        hips = bones.get("Hips") or [0.0, 1.0, 0.0]
        engine("camera.set", position=[hips[0], 1.05, hips[2] + 2.5],
               look_at=[hips[0], 0.95, hips[2]])
        engine("scene.isolate", object=mesh)
        lit = engine("frame.grid", columns=columns, rows=rows)["cells"]
        engine("scene.isolate", object="__nothing__")
        bare = engine("frame.grid", columns=columns, rows=rows)["cells"]
        engine("scene.isolate")
        mask = [[sum(abs(a[i] - b[i]) for i in range(3)) > 0.012
                 for a, b in zip(ra, rb)] for ra, rb in zip(lit, bare)]
        covered = [y for y, row in enumerate(mask) if any(row)]
        if not covered:
            continue
        low = covered[0] + int((covered[-1] - covered[0]) * 0.78)
        for y in range(low, min(covered[-1], len(mask) - 1) + 1):
            runs, inside = 0, False
            for on in mask[y]:
                if on and not inside:
                    runs += 1
                inside = on
            if runs > worst:
                worst, at = runs, moment
    print("  the most separate limbs anywhere below the hips: %d (at %.2fs)"
          % (worst, at))
    check("the figure has two legs and not three", worst <= 2,
          "%d separate limbs across the lower body at %.2fs" % (worst, at))


def spins(engine, mesh, seconds=4.0, samples=320):
    """Whether any bone takes the long way round.

    A rotation and its negation are the same pose and interpolate along
    opposite arcs, so a clip whose keys flip sign between frames sends a bone
    through a full turn -- which is what the right leg was doing, six times,
    while every number about the clip read as correct.

    Sampled between the keys, because at a key the pose is right and it is the
    arc between two of them that is wrong.
    """
    print("\n== does anything spin ==")
    track = {}
    for step in range(samples):
        engine("anim.set", time=step * seconds / samples)
        for bone in engine("scene.skeleton", object=mesh)["bones"]:
            track.setdefault(bone["name"], []).append(bone["world_position"])
    worst_name, worst = None, 0.0
    for name, rows in track.items():
        steps = sorted(sum((rows[i][k] - rows[i - 1][k]) ** 2 for k in range(3)) ** 0.5
                       for i in range(1, len(rows)))
        # Against the 95th and not the median, because a planted foot is
        # motionless for half a cycle: its median step is zero and everything
        # is infinitely more than that.
        busy = steps[int(len(steps) * 0.95)]
        if busy < 1e-6:
            continue
        spike = steps[-1] / busy
        if spike > worst:
            worst_name, worst = name, spike
    print("  the sharpest movement between samples: %s at %.1f times its own busiest"
          % (worst_name, worst))
    check("no bone takes the long way round", worst <= SPIN_LIMIT,
          "%s moves %.1f times its busiest in one step, allowed %d"
          % (worst_name, worst, SPIN_LIMIT))


def head_carriage(engine, mesh, seconds=5.0, samples=48):
    """Whether the figure carries its head or wears it.

    A pose solve that runs after the clip can overrule it, and one aiming the
    wrong bone overrules it completely: the neck runs upwards, so aiming the
    neck at something ahead lays the head over on its side. It reached 102
    degrees off vertical and stayed there for the last two seconds of the clip,
    which from the front is a figure with no head at all.
    """
    print("\n== how the head is carried ==")
    worst, when = 0.0, 0.0
    for step in range(samples):
        moment = step * seconds / samples
        engine("anim.set", time=moment)
        bones = {b["name"]: b["world_position"]
                 for b in engine("scene.skeleton", object=mesh)["bones"]}
        if "Neck" not in bones or "Head" not in bones:
            return
        up = [bones["Head"][i] - bones["Neck"][i] for i in range(3)]
        length = sum(v * v for v in up) ** 0.5
        if length < 1e-6:
            continue
        lean = math.degrees(math.acos(max(-1.0, min(1.0, up[1] / length))))
        if lean > worst:
            worst, when = lean, moment
    print("  the head leans at most %.0f degrees off upright (at %.2fs)" % (worst, when))
    check("the figure carries its head upright", worst <= HEAD_LEAN,
          "%.0f degrees at %.2fs, allowed %d" % (worst, when, HEAD_LEAN))


def gait(engine, figure_mesh, feet, road, seconds, fps):
    """Whether the figure walks or skates.

    The one thing a walk has to do is leave a planted foot where it was
    planted. A leg swung by a curve covers whatever that curve covers while the
    body covers whatever its speed covers, and the difference comes out as a
    foot sliding along the road -- which reads as a figure on castors and is
    invisible to every other measurement in this file.

    Read off the bones, because a skinned mesh does not move: its vertices sit
    in the pose they were bound in and the bones carry them, so where a foot is
    is a fact about the skeleton and nothing else can answer it.

    A foot is down when it is at the bottom of its travel and is neither coming
    down nor going up. Height alone is not enough: a toe leaves the road before
    it has climbed a centimetre, and judging contact by height alone scored the
    first six frames of the swing as a planted foot and charged the walk for the
    distance the foot covered in them. Both halves are needed and neither is
    circular -- this asks nothing about how the foot moves along the road, which
    is the thing being measured.
    """
    print("\n== the walk ==")
    try:
        engine("scene.skeleton", object=figure_mesh)
    except AgentError as refused:
        check("the figure has a skeleton to read", False, str(refused))
        return

    track = {}
    frames = int(seconds * fps)
    for frame in range(frames):
        engine("anim.set", time=frame / float(fps))
        for bone in engine("scene.skeleton", object=figure_mesh)["bones"]:
            track.setdefault(bone["name"], []).append(bone["world_position"])

    lowest = min(min(at[1] for at in track[foot]) for foot in feet if foot in track)
    check("no foot goes through the road", lowest >= road - 0.01,
          "the lowest is %+.3f, the road is %+.3f" % (lowest, road))

    worst_slide = 0.0
    worst_drift = 0.0
    contacts = 0
    for foot in feet:
        rows = track.get(foot)
        if not rows:
            continue
        floor = min(at[1] for at in rows) + CONTACT_BAND
        run = []
        runs = []
        for index, at in enumerate(rows):
            settled = index > 0 and abs(at[1] - rows[index - 1][1]) <= CONTACT_SETTLE
            if at[1] < floor and settled:
                run.append(at[0])
            elif run:
                runs.append(run)
                run = []
        if run:
            runs.append(run)
        for one in runs:
            if len(one) < 2:
                continue
            contacts += 1
            worst_drift = max(worst_drift, abs(one[-1] - one[0]))
            for index in range(1, len(one)):
                worst_slide = max(worst_slide, abs(one[index] - one[index - 1]))
        print("  %-8s lowest %+.3f, %d contact(s)" % (foot, min(at[1] for at in rows), len(runs)))

    check("the figure takes steps", contacts >= 2, "%d contacts in %.1fs" % (contacts, seconds))
    check("a planted foot stays planted", worst_slide <= SLIDE_PER_FRAME,
          "worst %.3f m in a frame, allowed %.3f" % (worst_slide, SLIDE_PER_FRAME))
    check("and does not creep over the whole contact", worst_drift <= SLIDE_PER_CONTACT,
          "worst %.3f m, allowed %.3f" % (worst_drift, SLIDE_PER_CONTACT))


def _track(engine, mesh, seconds, fps):
    """Every bone's world position at each frame of the clip, once."""
    frames = int(seconds * fps)
    rows = []
    for frame in range(frames):
        engine("anim.set", time=frame / float(fps))
        rows.append({b["name"]: b["world_position"]
                     for b in engine("scene.skeleton", object=mesh)["bones"]})
    return rows


def weight_shift(engine, mesh, seconds=5.0, fps=24):
    """Whether the figure takes its weight, or glides.

    A walk is a controlled fall onto each leg in turn, and the pelvis drops as
    the weight lands and rises as it is pushed off -- twice a stride. Read off
    the hip bone, whose height is a fact about the skeleton and nothing the
    camera or the mesh can hide. A pelvis held at one height is a figure on a
    rail, whatever its legs are doing.
    """
    print("\n== does it take its weight ==")
    track = _track(engine, mesh, seconds, fps)
    if "Hips" not in track[0]:
        check("the figure shifts its weight", False, "no Hips bone to read")
        return
    heights = [row["Hips"][1] for row in track]
    bob = max(heights) - min(heights)
    print("  the hips ride between %+.3f and %+.3f" % (min(heights), max(heights)))
    check("the figure takes its weight onto each leg",
          WEIGHT_BOB_LOW <= bob <= WEIGHT_BOB_HIGH,
          "the pelvis rises and falls %.3f m over the walk, wanted %.3f to %.3f"
          % (bob, WEIGHT_BOB_LOW, WEIGHT_BOB_HIGH))


def hand_to_target(engine, mesh, seconds=5.0, fps=24, strike=(2.67, 3.83)):
    """Whether the strike reaches, or is a pose already held.

    The lunge is the one moment the figure does something to the world, and it
    reads only if the swiping arm extends further than it ever does shuffling.
    Measured as the straight-line distance from shoulder to wrist -- the arm's
    reach, which does not care which way the body faces -- at the peak of the
    lunge against the furthest it reaches at any other time. A strike that does
    not beat the walk is an arm held out for a second, not a swing.
    """
    print("\n== the strike ==")
    track = _track(engine, mesh, seconds, fps)
    if "WristR" not in track[0] or "ShoulderR" not in track[0]:
        check("the strike reaches", False, "no right arm to read")
        return
    # How far the arm is extended, as the straight-line distance from the
    # shoulder to the wrist. Rotation-independent -- it says nothing about
    # which way the body faces -- so it isolates the arm reaching out from the
    # body lunging under it. A bent shuffle keeps it short; a strike straightens
    # the elbow and the distance jumps toward the arm's full length.
    def extension(row):
        a, w = row["ShoulderR"], row["WristR"]
        return sum((w[i] - a[i]) ** 2 for i in range(3)) ** 0.5

    reach = [extension(track[i]) for i in range(len(track))]
    lo, hi = strike
    during = [reach[i] for i in range(len(track)) if lo <= i / float(fps) <= hi]
    # The walk it is compared against is the shuffle before the lunge, not the
    # whole clip: the arm passes through straight as it recovers from the swing,
    # and folding that recovery back into "the walk" would credit the walk with
    # the strike's own reach. Before the lunge the arm has only ever shuffled.
    before = [reach[i] for i in range(len(track)) if i / float(fps) < lo]
    if not during or not before:
        check("the strike reaches", False, "no lunge in the clip")
        return
    peak = max(during)
    walk = max(before)
    print("  the arm extends to %.3f m at the strike, %.3f m in the shuffle before it"
          % (peak, walk))
    check("the strike reaches past anything the walk does", peak - walk >= STRIKE_REACH,
          "the strike extends the arm %.3f m past the walk, wanted %.3f"
          % (peak - walk, STRIKE_REACH))


def secondary_motion(engine, mesh, seconds=5.0, fps=24):
    """Whether the upper body follows the hips, or is bolted to them.

    The turn of the pelvis arrives at the shoulders a beat later and at the
    head later still: that lag is the whole of what makes a body read as a
    body and not as a rig. Measured by sliding the head's side-to-side motion
    against the hips' and finding the offset that lines them up best. A head
    that lines up best at zero lag, or a negative one, turns with the pelvis
    or ahead of it -- which nothing living does.
    """
    print("\n== does the body follow through ==")
    track = _track(engine, mesh, seconds, fps)
    if "Head" not in track[0] or "Hips" not in track[0]:
        check("the head follows the body", False, "no Head and Hips to read")
        return
    # Across the street, which is the axis the turn swings the head along.
    hips = [row["Hips"][2] for row in track]
    head = [row["Head"][2] for row in track]

    def centred(values):
        mean = sum(values) / len(values)
        return [v - mean for v in values]

    hips = centred(hips)
    head = centred(head)
    best_lag, best = 0, -1.0
    for lag in range(0, 12):
        total = 0.0
        for i in range(len(track) - lag):
            total += hips[i] * head[i + lag]
        if total > best:
            best, best_lag = total, lag
    print("  the head's turn lines up best %d frames behind the hips" % best_lag)
    check("the head follows the body rather than leading it", best_lag >= FOLLOW_LAG,
          "best alignment at a lag of %d frames, wanted at least %d" % (best_lag, FOLLOW_LAG))


def main(argv):
    parser = argparse.ArgumentParser(prog="critique_scene")
    parser.add_argument("--port", type=int, default=7911)
    parser.add_argument("--launch", metavar="BINARY")
    parser.add_argument("--figure", default="Zombie_")
    parser.add_argument("--walks", default="Zombie_Body",
                        help="the skinned model whose skeleton carries the walk")
    parser.add_argument("--feet", default="AnkleL,AnkleR,ToeL,ToeR")
    parser.add_argument("--road", type=float, default=0.0)
    args = parser.parse_args(argv)

    started = time.time()
    scene = None
    if args.launch:
        env = dict(os.environ)
        env["AE3D_AGENT"] = str(args.port)
        env["AE3D_FRAMES"] = "100000"
        log = tempfile.NamedTemporaryFile(prefix="ae3d_critique_", suffix=".log", delete=False)
        scene = subprocess.Popen([args.launch], env=env, stdout=log, stderr=subprocess.STDOUT)
        engine = None
        deadline = time.time() + 30.0
        while time.time() < deadline and engine is None:
            if scene.poll() is not None:
                print("critique_scene: %s stopped before it opened the channel" % args.launch)
                return 3 if scene.returncode == 0 else 2
            try:
                engine = Session(args.port)
            except OSError:
                time.sleep(0.25)
        if engine is None:
            scene.kill()
            print("critique_scene: %s never answered" % args.launch)
            return 2
    else:
        try:
            engine = Session(args.port)
        except OSError as failure:
            print("critique_scene: nothing answering on port %d (%s)" % (args.port, failure))
            return 2

    with engine:
        engine("frame.pause")
        models = engine("scene.tree", detail=True, audit=True)["models"]
        texel_density(models)
        relief(models)
        surface_relief(engine, models)
        character(models, args.figure)
        proportion(models, args.figure)
        budget(engine)
        if args.walks:
            silhouette(engine, args.walks)
            limbs(engine, args.walks, args.figure)
            spins(engine, args.walks)
            head_carriage(engine, args.walks)
            gait(engine, args.walks, args.feet.split(","), args.road, 2.6, 24)
            weight_shift(engine, args.walks)
            hand_to_target(engine, args.walks)
            secondary_motion(engine, args.walks)
        engine("frame.resume")

    if scene is not None:
        scene.terminate()
        try:
            scene.wait(timeout=5)
        except subprocess.TimeoutExpired:
            scene.kill()

    print("\n%s in %.1fs"
          % ("critique_scene: the scene meets every standard" if not FAILURES
             else "critique_scene: %d standard(s) not met" % len(FAILURES),
             time.time() - started))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except AgentError as refused:
        print("critique_scene: %s" % refused)
        sys.exit(2)
