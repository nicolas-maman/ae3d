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
# How far off the road surface the deepest a foot reaches may sit. A foot below
# the road by more than SINK has gone through it; one above by more than FLOAT
# never touches down and the figure walks on air. The road passed in is that
# surface -- for this scene the pavement the walk is on, where the lowest of the
# tracked feet rests at about 0.115 -- so the band is set around a real touch,
# not around zero, which a floating figure would pass as readily as a planted
# one.
GROUND_SINK = 0.03
GROUND_FLOAT = 0.07
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
# and a dressed pavement draws in the low hundreds and is meant to. On Vulkan
# the wet road adds a camera-depth prepass for its reflection, which redraws the
# frustum-culled opaque set once more -- a real, bounded cost the ceiling
# leaves room for, not the whole street redrawn. This is a ceiling against a
# scene that has quietly gone wrong -- a texture bound per face, a merge that
# stopped merging, a prepass that stopped culling -- while tools/ae3d_bench.ae
# holds the exact figure and fails the build on a single draw more than last
# recorded.
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
# What a night street has to be in the frame, read off the rendered grid rather
# than off a screenshot -- every number below comes back through the channel.
#
# A night is contrast: bright sources against deep dark. A scene lit by a flat
# ambient wash has neither, so it is caught by having no cell brighter than a
# lamp and no run of dark between them.
LIGHT_SOURCE_LUM = 0.55     # a cell this bright is a light source (lamp, lit window)
MIN_LIGHT_SOURCES = 3       # a night street has at least this many alight
# ...but not most of the frame. A scene flooded flat by a bright ambient lights
# a quarter of its cells this bright; a night lit in pools lights a twentieth.
# This is the number that tells the two apart, where the median cannot -- the
# dark sky pulls a flooded frame's median down just as far as a real night's.
LIGHT_SOURCE_FRACTION_MAX = 0.15
BRIGHTEST_WANTED = 0.6      # something in frame burns at least this bright
# Deep dark, but not crushed to nothing: a black frame reads as broken, a lifted
# one as fog. The darkest tenth sits in this band, which is shadow with a little
# indirect light in it and no more.
DARK_FLOOR = 0.008          # below this is a pure-black crush
DARK_CEIL = 0.22            # above this the night has been washed flat
# The frame as a whole is dark -- it is night -- so its median is low.
NIGHT_MEDIAN_MAX = 0.42
# The light is warm and the dark is cool: sodium lamps against a dusk sky. Read
# as the red-minus-blue of the bright cells and of the dark cells.
WARM_LIGHTS_MIN = 0.10      # bright cells lean warm by at least this -- a lamp, not a grey flood
COOL_DARK_MAX = 0.02        # dark cells do not lean warm past this
# The road reflects the lights: the brightest of the road band stands far above
# its own median. A dry matte road is uniform; a wet one has the lamp in it.
ROAD_REFLECTION_MIN = 0.25
# A night street is lit in pools -- bright under each lamp, dark between -- which
# is what a point light with real falloff makes and a flat fill does not. Read
# off the road's horizontal profile: peaks under the lamps with dark gaps
# between. Measured on both backends the road shows three pools whose gaps fall
# to a quarter of the peak; these are the floors, well inside that.
POOL_MIN = 2                # at least this many separate pools of light on the road
POOL_DIP_FRACTION = 0.6     # the dark between two pools falls to at most this of the peak
# The wet road reflects the street's lamps: turning its reflectivity off dims it
# to a matte surface, turning it on brings the lamps back as bright cells on the
# tarmac. Proved by toggling the road's reflectivity over the channel and
# counting the bright cells in the road band. Measured lift on both backends is
# ~1.85x; this floor sits well inside that, and a matte road cannot reach it.
REFLECTION_ON = 0.8         # the reflectivity the wet road is measured at
REFLECTION_LAMP_LIFT = 1.4  # the road carries at least this many times as many bright cells wet as dry
# Screen-space reflection mirrors the on-screen geometry (lit windows, lamps)
# onto the wet road: it only ever adds a mirrored light, never darkens, so with
# it on the road carries more bright cells than without. Measured on Vulkan at
# strength 1.5 the road gains ~30% more bright cells; the floor sits under that.
# Vulkan-only for now (the OpenGL path is a later step), so it is measured only
# where the backend is Vulkan. Bloom is held off across the A/B so the reading
# is the reflection's own contribution, not the composite it replaces.
SSR_ROAD_HEIGHT = 0.115     # the reflective plane, matching the scene's road
SSR_STRENGTH = 1.5          # the reflection strength the standard measures at
SSR_LAMP_LIFT = 1.15        # SSR adds at least this many times as many road bright cells
# The reflective pass writes into an intermediate the bloom pass then reads, so
# the mirrored lamps bloom with the real ones rather than the reflection
# replacing the bloom. Proven by holding SSR on and toggling bloom: if bloom
# were replaced it would not change the frame, so bloom on must lift the light.
SSR_BLOOM_COEXIST_LIFT = 1.05  # with SSR on, bloom lifts total light at least this much
# Held still, the frame must not move. Two frames drawn of the same paused pose
# should be the same frame; a cell that drifts between them is temporal shimmer
# -- an unseeded dither, a jittered ray march with no accumulation, a readback
# of uninitialised memory -- which reads as a scene that crawls when nothing in
# it does. Measured: Vulkan is bit-exact, OpenGL within one code value; this
# floor is many times that and far below anything the eye would catch.
STABILITY_EPS = 0.004       # the most a cell may differ between two held frames
# Bloom, proved by turning it off. Every bloom term scales by its intensity, so
# dialling it to zero over the channel is the same render path with the glow
# removed -- no pipeline change to confound the reading. Bloom adds light and
# spreads it, so with it on the frame carries more total luminance and more
# cells burn over the light-source line. Measured lift on both backends is
# ~1.6x total and ~2.4x bright cells; these are the floors, well under that.
BLOOM_TOTAL_LIFT = 1.15    # the lit frame is at least this times as bright in total
BLOOM_BRIGHT_LIFT = 1.30   # and at least this many times as many cells burn bright
# Shadows, proved by turning them off. A shadow is light kept out of a place the
# scene is occluded from, so with shadows off the frame takes in the light that
# was being blocked: more total luminance, and far more cells over the
# light-source line once nothing is in shadow. Measured on both backends the
# frame gains ~1.45x total and ~5.5x bright cells with shadows removed; these
# floors sit well inside that.
SHADOW_TOTAL_LIFT = 1.15   # removing shadows brightens the frame by at least this
SHADOW_BRIGHT_LIFT = 1.80  # and lets at least this many times as many cells burn bright
# anim.track poses a time without drawing it; this is how far a bone read that
# way may sit from the same bone read through a drawn frame. It is a hair off
# zero, not zero, only to leave room for the order floats are summed in; the
# two paths run the same clip and the same solve, so a real gap is a bug.
SAMPLE_MATCH_EPS = 0.0005


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
    times = [step * seconds / samples for step in range(samples)]
    for bones in _series(engine, mesh, times):
        for name, position in bones.items():
            track.setdefault(name, []).append(position)
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
    times = [step * seconds / samples for step in range(samples)]
    for step, bones in enumerate(_series(engine, mesh, times)):
        moment = times[step]
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
    times = [frame / float(fps) for frame in range(int(seconds * fps))]
    for bones in _series(engine, figure_mesh, times):
        for name, position in bones.items():
            track.setdefault(name, []).append(position)

    lowest = min(min(at[1] for at in track[foot]) for foot in feet if foot in track)
    check("no foot sinks through the road", lowest >= road - GROUND_SINK,
          "the lowest foot is %+.3f, %.3f under the road at %+.3f, allowed %.3f"
          % (lowest, road - lowest, road, GROUND_SINK))
    check("and the walk plants a foot on it rather than floating over it",
          lowest <= road + GROUND_FLOAT,
          "the lowest foot is %+.3f, %.3f above the road at %+.3f, allowed %.3f"
          % (lowest, lowest - road, road, GROUND_FLOAT))

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


def sampling_matches(engine, mesh):
    """That anim.track reads the same pose a drawn frame does.

    Every walk standard below reads the skeleton through anim.track, which poses
    a time without rendering it. That is only trustworthy if it lands on the
    same pose the per-frame path does -- anim.set, which seeks and reposes, then
    scene.skeleton, which is answered at the frame boundary after the scene's
    solve has run. So the two are compared here, bone for bone, before anything
    is measured off the fast one. If they ever diverge, every number after this
    is measuring a pose that was never drawn, and this is what says so.
    """
    print("\n== the fast pose reads true ==")
    times = [0.4, 1.1, 1.9, 2.67, 3.3]
    batched = _series(engine, mesh, times)
    worst, where = 0.0, ""
    for i, moment in enumerate(times):
        engine("anim.set", time=moment)
        drawn = {b["name"]: b["world_position"]
                 for b in engine("scene.skeleton", object=mesh)["bones"]}
        for name, pos in drawn.items():
            d = max(abs(pos[k] - batched[i][name][k]) for k in range(3))
            if d > worst:
                worst, where = d, "%s at %.2fs" % (name, moment)
    print("  worst bone disagreement between the two: %.6f m (%s)" % (worst, where or "-"))
    check("anim.track lands on the pose a frame draws", worst <= SAMPLE_MATCH_EPS,
          "a bone read %.6f m apart between anim.track and the per-frame path (%s), allowed %.4f"
          % (worst, where, SAMPLE_MATCH_EPS))


def _series(engine, mesh, times):
    """Every bone's world position at each of the given animation times.

    One channel call. anim.track poses each time exactly as a drawn frame does
    -- the clips applied, then the scene's own solve on top -- and reads every
    bone, with no frame rendered between samples. It is the per-frame anim.set
    plus scene.skeleton loop collapsed into a single answer, which is what makes
    the animation standards affordable to run a thousand samples deep, and on
    the software renderer a headless runner falls back to.
    """
    return [{b["name"]: b["world_position"] for b in sample["bones"]}
            for sample in engine("anim.track", object=mesh, times=times)["samples"]]


def _track(engine, mesh, seconds, fps):
    """Every bone's world position at each frame of the clip, once."""
    return _series(engine, mesh, [frame / float(fps) for frame in range(int(seconds * fps))])


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


def _lum(cell):
    return 0.2126 * cell[0] + 0.7152 * cell[1] + 0.0722 * cell[2]


def lighting(engine, at=2.2):
    """Whether the frame reads as a night, measured off the rendered grid.

    Nothing here is looked at. frame.grid answers the whole frame as a grid of
    mean colours -- the same numbers a person's eye would be forming an
    impression from -- and the impression is made of contrast, warm light
    against cool dark, and the lamps caught in the wet road. Each of those is a
    number, so each is a standard the scene either meets or does not, on
    whichever backend drew the frame.
    """
    print("\n== does it read as a night ==")
    # The scene is already paused by the caller; seek and read, and do not
    # resume -- the measures after this one depend on the playhead staying put.
    engine("anim.set", time=at)
    grid = engine("frame.grid", columns=48, rows=27)["cells"]
    cells = [c for row in grid for c in row]
    if not cells:
        check("the frame came back to be read", False, "the grid was empty")
        return
    lums = sorted(_lum(c) for c in cells)
    n = len(lums)

    brightest = lums[-1]
    median = lums[n // 2]
    dark = lums[max(1, n // 10) - 1]
    sources = [c for c in cells if _lum(c) >= LIGHT_SOURCE_LUM]

    print("  luminance runs %.3f to %.3f, median %.3f, darkest tenth at %.3f"
          % (lums[0], brightest, median, dark))
    print("  %d cells burn as light sources" % len(sources))

    check("something in the frame burns like a light",
          brightest >= BRIGHTEST_WANTED,
          "the brightest cell is %.3f, wanted %.2f" % (brightest, BRIGHTEST_WANTED))
    source_fraction = len(sources) / float(n)
    check("the street has its lamps and windows alight",
          len(sources) >= MIN_LIGHT_SOURCES,
          "%d cells over %.2f, wanted %d" % (len(sources), LIGHT_SOURCE_LUM, MIN_LIGHT_SOURCES))
    check("and lit in pools rather than flooded flat",
          source_fraction <= LIGHT_SOURCE_FRACTION_MAX,
          "%.0f%% of the frame burns that bright, wanted under %.0f%%"
          % (source_fraction * 100.0, LIGHT_SOURCE_FRACTION_MAX * 100.0))
    check("the night is dark rather than a flat wash",
          median <= NIGHT_MEDIAN_MAX,
          "the median cell is %.3f, wanted under %.2f" % (median, NIGHT_MEDIAN_MAX))
    check("the shadows are deep but not crushed to black",
          DARK_FLOOR <= dark <= DARK_CEIL,
          "the darkest tenth is %.3f, wanted %.3f to %.2f" % (dark, DARK_FLOOR, DARK_CEIL))

    # Warm light, cool dark: the red-minus-blue of the brightest and the darkest.
    bright_cells = sorted(cells, key=_lum)[-max(3, n // 20):]
    dark_cells = sorted(cells, key=_lum)[:max(3, n // 5)]
    warm = sum(c[0] - c[2] for c in bright_cells) / len(bright_cells)
    cool = sum(c[0] - c[2] for c in dark_cells) / len(dark_cells)
    print("  the light leans %+.3f warm, the dark leans %+.3f" % (warm, cool))
    check("the light is warm", warm >= WARM_LIGHTS_MIN,
          "the bright cells lean %+.3f, wanted %+.2f" % (warm, WARM_LIGHTS_MIN))
    check("and the dark is not warmer than the light",
          cool <= COOL_DARK_MAX and cool < warm,
          "the dark cells lean %+.3f" % cool)

    # The road: the bottom third of the frame, where the wet surface takes the
    # lamps. Its brightest stands well above its own median when it is wet.
    road = [c for row in grid[int(len(grid) * 0.62):] for c in row]
    if road:
        rl = sorted(_lum(c) for c in road)
        contrast = rl[-1] - rl[len(rl) // 2]
        print("  the road runs %.3f to %.3f (%.3f of reflection)"
              % (rl[0], rl[-1], contrast))
        check("the wet road takes the lamps", contrast >= ROAD_REFLECTION_MIN,
              "the road's brightest stands %.3f over its median, wanted %.2f"
              % (contrast, ROAD_REFLECTION_MIN))


def stability(engine, at=2.2):
    """That a held frame holds still.

    With the simulation paused on one pose, two frames drawn of it should be the
    same frame. Anything that drifts between them -- a dither reseeded each
    frame, a volumetric march jittered without accumulating, a readback of
    memory that was never cleared -- is temporal shimmer: the scene crawls while
    nothing in it moves, which no still image and no single-frame standard can
    see. Read as the largest a cell moves between two consecutive grids of the
    same paused pose.
    """
    print("\n== a held frame holds still ==")
    engine("anim.set", time=at)
    first = [c for row in engine("frame.grid", columns=64, rows=36)["cells"] for c in row]
    second = [c for row in engine("frame.grid", columns=64, rows=36)["cells"] for c in row]
    if not first or len(first) != len(second):
        check("the frame came back twice to be compared", False, "the grids did not match up")
        return
    worst = max(max(abs(first[i][k] - second[i][k]) for k in range(3)) for i in range(len(first)))
    print("  the most any cell moved between two held frames: %.6f" % worst)
    check("nothing shimmers when the scene is held still", worst <= STABILITY_EPS,
          "a cell moved %.6f between two held frames, allowed %.4f" % (worst, STABILITY_EPS))


def light_pools(engine, at=2.2):
    """That the lamps lay separate pools on the road, not one flat wash.

    A night street reads by its pools of light -- bright under each lamp, dark
    between -- and a point light with real falloff makes them where a flat
    ambient fill cannot. Read as the road's horizontal profile: the brightest
    cell of each column down the wet band, which rises to a peak under a lamp
    and falls away between them. Pools are the runs above the road's own bright
    line; the dark between two of them is measured against their peaks, because
    a wash has the peaks without the dark.
    """
    print("\n== the lamps lay pools on the road ==")
    engine("anim.set", time=at)
    grid = engine("frame.grid", columns=64, rows=36)["cells"]
    band = grid[int(len(grid) * 0.62):]
    width = len(band[0])
    col = [max(_lum(row[x]) for row in band) for x in range(width)]
    mean = sum(col) / width
    spread = (sum((v - mean) ** 2 for v in col) / width) ** 0.5
    bright_line = mean + 0.6 * spread

    pools = []
    x = 0
    while x < width:
        if col[x] >= bright_line:
            peak, run = col[x], x
            while run < width and col[run] >= bright_line * 0.75:
                peak = max(peak, col[run])
                run += 1
            pools.append((x, run - 1, peak))
            x = run
        else:
            x += 1

    print("  %d pool(s) on the road, peaks at %s"
          % (len(pools), ", ".join("%.2f" % p[2] for p in pools)))
    check("the road is lit in separate pools", len(pools) >= POOL_MIN,
          "%d pool(s) over the road's bright line, wanted %d" % (len(pools), POOL_MIN))

    if len(pools) >= 2:
        # The clearest gap between two adjacent pools: the darkest cell between
        # them against the dimmer of the two peaks. A wash never dips.
        best = min(min(col[pools[i - 1][1]:pools[i][0] + 1]) / min(pools[i - 1][2], pools[i][2])
                   for i in range(1, len(pools)))
        print("  the darkest gap between adjacent pools falls to %.0f%% of the pool" % (best * 100))
        check("and the dark between the pools is genuinely dark",
              best <= POOL_DIP_FRACTION,
              "the shallowest gap is %.0f%% of its pool's peak, wanted under %.0f%%"
              % (best * 100, POOL_DIP_FRACTION * 100))


def wet_road_reflection(engine, at=2.2):
    """That the wet road reflects the street's lamps, not just pools of fill.

    A wet road is a mirror held to the lights: each lamp overhead comes back as
    a bright reflection on the tarmac, brightest where the view grazes it. The
    road's reflectivity is turned off over the channel -- dropping it to a matte
    surface -- and the bright cells in the road band are counted, then turned
    back on and counted again. With it on the lamps return, so the road carries
    far more bright cells than the dry, matte version. Both are numbers off the
    grid; the road is left wet, the way the scene set it.
    """
    print("\n== the wet road reflects the lamps ==")
    engine("anim.set", time=at)
    models = engine("scene.tree", detail=True)["models"]
    wet = [i for i, m in enumerate(models)
           if m["name"] == "Street_Road" or m["name"] == "Street_Ground"]
    if not wet:
        check("the scene has a wet road to measure", False, "no road or ground model")
        return

    def set_reflectivity(value):
        for i in wet:
            engine("model.set", index=i, reflectivity=value)

    def road_bright():
        grid = engine("frame.grid", columns=64, rows=48)["cells"]
        band = [c for row in grid[int(len(grid) * 0.60):] for c in row]
        return sum(1 for c in band if _lum(c) >= LIGHT_SOURCE_LUM)

    set_reflectivity(0.0)
    dry = road_bright()
    set_reflectivity(REFLECTION_ON)
    wet_count = road_bright()

    print("  the road carries %d bright cells wet, %d dry" % (wet_count, dry))
    check("the wet road reflects the lamps",
          wet_count >= dry * REFLECTION_LAMP_LIFT,
          "%d bright cells with the reflection on against %d off (%.2fx, wanted %.2fx)"
          % (wet_count, dry, wet_count / float(max(dry, 1)), REFLECTION_LAMP_LIFT))


def wet_road_ssr(engine, at=2.2):
    """That screen-space reflection mirrors the scene's geometry onto the road.

    Where the analytic reflection mirrors the lights, SSR mirrors what is drawn
    -- the lit windows and lamp posts above the road come back on the wet
    tarmac. It is additive: it only adds a mirrored light and never darkens, so
    with it on the road carries more bright cells than without. It composites
    before the bloom rather than instead of it, so bloom is held off across the
    toggle only to read the reflection's own contribution cleanly. Vulkan-only
    for now, so it runs only where the backend is Vulkan. The scene's own SSR
    setting is captured and restored, so this reads the same whatever the demo
    left it at.
    """
    engine("anim.set", time=at)
    stats = engine("frame.stats")
    if stats.get("backend") != "vulkan":
        return
    print("\n== the wet road mirrors the scene (SSR) ==")
    was_bloom = stats.get("render", {}).get("bloom_intensity", 0.0)
    was_ssr = stats.get("render", {}).get("ssr", False)

    def road_bright():
        grid = engine("frame.grid", columns=64, rows=48)["cells"]
        band = [c for row in grid[int(len(grid) * 0.60):] for c in row]
        return sum(1 for c in band if _lum(c) >= LIGHT_SOURCE_LUM)

    engine("render.set", ssr=False, bloom_intensity=0.0)
    off = road_bright()
    engine("render.set", ssr=True, ssr_road_height=SSR_ROAD_HEIGHT, ssr_strength=SSR_STRENGTH)
    on = road_bright()
    # Leave it exactly as the scene had it: SSR and bloom back to their defaults.
    engine("render.set", ssr=was_ssr, bloom_intensity=was_bloom)

    print("  the road carries %d bright cells with SSR mirroring the scene, %d without" % (on, off))
    check("screen-space reflection mirrors the scene onto the wet road",
          on >= off * SSR_LAMP_LIFT,
          "%d bright cells with SSR on against %d off (%.2fx, wanted %.2fx)"
          % (on, off, on / float(max(off, 1)), SSR_LAMP_LIFT))


def ssr_bloom_coexist(engine, at=2.2):
    """That bloom composites on top of the reflection rather than replacing it.

    The reflective pass writes into an intermediate target the bloom pass then
    reads, so the mirrored lamps bloom along with the real ones. Were bloom
    replaced whenever SSR is on -- the earlier behaviour -- toggling bloom with
    SSR held on would change nothing. So with SSR on, the frame must carry more
    light with bloom than without: proof the two passes run in sequence. The
    scene's SSR and bloom settings are captured and restored. Vulkan-only.
    """
    engine("anim.set", time=at)
    stats = engine("frame.stats")
    if stats.get("backend") != "vulkan":
        return
    print("\n== SSR and bloom coexist ==")
    was_bloom = stats.get("render", {}).get("bloom_intensity", 0.0)
    was_ssr = stats.get("render", {}).get("ssr", False)
    lit_bloom = was_bloom if was_bloom > 0.0 else 0.85

    def frame_light():
        grid = engine("frame.grid", columns=64, rows=36)["cells"]
        return sum(_lum(c) for row in grid for c in row)

    engine("render.set", ssr=True, ssr_road_height=SSR_ROAD_HEIGHT, ssr_strength=SSR_STRENGTH,
           bloom_intensity=0.0)
    without = frame_light()
    engine("render.set", bloom_intensity=lit_bloom)
    withb = frame_light()
    engine("render.set", ssr=was_ssr, bloom_intensity=was_bloom)

    print("  with the reflection on, the frame carries %.1f of light with bloom, %.1f without"
          % (withb, without))
    check("bloom composites over the reflection (the two passes coexist)",
          withb >= without * SSR_BLOOM_COEXIST_LIFT,
          "%.1f with bloom against %.1f without (%.2fx, wanted %.2fx)"
          % (withb, without, withb / max(without, 1e-6), SSR_BLOOM_COEXIST_LIFT))


def bloom(engine, at=2.2):
    """That the bloom the scene turns on is doing measurable work.

    Read by turning it off. render.set dials the intensity to zero on the same
    render path -- every bloom term is scaled by it, so nothing about the
    pipeline changes, the glow simply goes to nothing -- and the frame is read
    again. Bloom lifts light and spreads it, so the lit frame carries more total
    luminance and more cells over the light-source line than the frame without
    it. Both are numbers off the grid, so both are standards rather than
    opinions. The intensity is read first and put back after, so nothing
    downstream sees the frame with its glow removed.
    """
    print("\n== the bloom earns its place ==")
    engine("anim.set", time=at)
    configured = engine("frame.stats")["render"].get("bloom_intensity")
    if not configured or configured <= 0.0:
        check("the scene has bloom to measure", False,
              "the renderer reports bloom_intensity %r" % configured)
        return

    def read():
        grid = engine("frame.grid", columns=64, rows=36)["cells"]
        lums = [_lum(c) for row in grid for c in row]
        return sum(lums), sum(1 for x in lums if x >= LIGHT_SOURCE_LUM)

    on_total, on_bright = read()
    engine("render.set", bloom_intensity=0.0)
    off_total, off_bright = read()
    engine("render.set", bloom_intensity=configured)

    print("  with bloom the frame carries %.1f of light across %d bright cells;"
          " with it off, %.1f across %d"
          % (on_total, on_bright, off_total, off_bright))
    check("bloom lifts the light in the frame",
          on_total >= off_total * BLOOM_TOTAL_LIFT,
          "with bloom %.1f, without %.1f (%.2fx, wanted %.2fx)"
          % (on_total, off_total, on_total / max(off_total, 1e-6), BLOOM_TOTAL_LIFT))
    check("and spreads it, so more of the frame burns bright",
          on_bright >= off_bright * BLOOM_BRIGHT_LIFT,
          "with bloom %d bright cells, without %d (%.2fx, wanted %.2fx)"
          % (on_bright, off_bright, on_bright / float(max(off_bright, 1)), BLOOM_BRIGHT_LIFT))


def shadows(engine, at=2.2):
    """That the shadows the scene casts are keeping light out of the frame.

    Read by turning them off. render.set drops the whole scene's shadowing, the
    frame is read, and it is put back. A shadow is light the scene is occluded
    from, so with shadows off that light returns: the frame carries more total
    luminance and far more cells burn over the light-source line once nothing is
    in shadow. Both are numbers off the grid. The state is read first and
    restored after, so nothing downstream sees the scene unshadowed.
    """
    print("\n== the shadows keep light out ==")
    engine("anim.set", time=at)
    was = engine("frame.stats")["render"].get("shadows")
    if not was:
        check("the scene is casting shadows to measure", False,
              "the renderer reports shadows %r" % was)
        return

    def read():
        grid = engine("frame.grid", columns=64, rows=36)["cells"]
        lums = [_lum(c) for row in grid for c in row]
        return sum(lums), sum(1 for x in lums if x >= LIGHT_SOURCE_LUM)

    on_total, on_bright = read()
    engine("render.set", shadows=False)
    off_total, off_bright = read()
    engine("render.set", shadows=True)

    print("  shadowed, the frame carries %.1f of light across %d bright cells;"
          " unshadowed, %.1f across %d"
          % (on_total, on_bright, off_total, off_bright))
    check("shadows take light out of the frame",
          off_total >= on_total * SHADOW_TOTAL_LIFT,
          "unshadowed %.1f against shadowed %.1f (%.2fx, wanted %.2fx)"
          % (off_total, on_total, off_total / max(on_total, 1e-6), SHADOW_TOTAL_LIFT))
    check("and hold back the light that would flood the occluded parts",
          off_bright >= on_bright * SHADOW_BRIGHT_LIFT,
          "unshadowed %d bright cells against shadowed %d (%.2fx, wanted %.2fx)"
          % (off_bright, on_bright, off_bright / float(max(on_bright, 1)), SHADOW_BRIGHT_LIFT))


def main(argv):
    parser = argparse.ArgumentParser(prog="critique_scene")
    parser.add_argument("--port", type=int, default=7911)
    parser.add_argument("--launch", metavar="BINARY")
    parser.add_argument("--figure", default="Zombie_")
    parser.add_argument("--walks", default="Zombie_Body",
                        help="the skinned model whose skeleton carries the walk")
    parser.add_argument("--feet", default="AnkleL,AnkleR,ToeL,ToeR")
    parser.add_argument("--road", type=float, default=0.115,
                        help="height of the surface the figure walks on, that its "
                             "planted foot should reach and not sink through")
    parser.add_argument("--vulkan", action="store_true",
                        help="drive the Vulkan backend rather than OpenGL")
    parser.add_argument("--frame-only", action="store_true", dest="frame_only",
                        help="skip the per-frame animation sampling; keep the "
                             "scene and lighting standards (for a slow backend)")
    args = parser.parse_args(argv)

    started = time.time()
    scene = None
    if args.launch:
        env = dict(os.environ)
        env["AE3D_AGENT"] = str(args.port)
        env["AE3D_FRAMES"] = "100000"
        log = tempfile.NamedTemporaryFile(prefix="ae3d_critique_", suffix=".log", delete=False)
        command = [args.launch] + (["vulkan"] if args.vulkan else [])
        scene = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        engine = None
        deadline = time.time() + 30.0
        while time.time() < deadline and engine is None:
            if scene.poll() is not None:
                # A backend the machine has no driver for -- Vulkan on a runner
                # without a loader -- is a skip, not a failure: there is nothing
                # to judge, and the scene said so on its way out.
                try:
                    said = open(log.name, encoding="utf-8", errors="replace").read()
                except OSError:
                    said = ""
                if "no Vulkan driver" in said or "no Vulkan" in said:
                    print("critique_scene: SKIP no Vulkan driver on this machine")
                    return 3
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

        # The whole critique reads the rendered frame. A backend that cannot
        # give it back -- software Vulkan on a headless runner has no swapchain
        # to read from -- cannot be judged, which is a skip rather than a
        # failure. Probed once, here, so it is caught before any standard runs.
        probe = engine("frame.grid", columns=8, rows=8).get("cells") or []
        readable = any(sum(c[:3]) > 0.02 for row in probe for c in row)
        if not readable:
            print("critique_scene: SKIP the frame could not be read back on this backend")
            if scene is not None:
                scene.terminate()
                try:
                    scene.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    scene.kill()
            return 3

        models = engine("scene.tree", detail=True, audit=True)["models"]
        texel_density(models)
        relief(models)
        surface_relief(engine, models)
        character(models, args.figure)
        proportion(models, args.figure)
        budget(engine)
        stability(engine)
        lighting(engine)
        light_pools(engine)
        wet_road_reflection(engine)
        wet_road_ssr(engine)
        ssr_bloom_coexist(engine)
        bloom(engine)
        shadows(engine)
        if args.walks:
            sampling_matches(engine, args.walks)
            # These read the pose through anim.track, which samples every time
            # in one answer without rendering a frame, so they cost the same on
            # the software Vulkan a headless runner falls back to as on a
            # workstation. That is what lets the Vulkan pass prove the walk on
            # the target rather than leaving it to the OpenGL run.
            spins(engine, args.walks)
            head_carriage(engine, args.walks)
            gait(engine, args.walks, args.feet.split(","), args.road, 2.6, 24)
            weight_shift(engine, args.walks)
            hand_to_target(engine, args.walks)
            secondary_motion(engine, args.walks)
            # These pose the figure and read the picture of it, a rendered frame
            # per sample. On software Vulkan a frame is a tenth of a second and
            # this runs for minutes, and the shape it judges is the same on
            # every backend, so --frame-only leaves it to the OpenGL run.
            if not args.frame_only:
                silhouette(engine, args.walks)
                limbs(engine, args.walks, args.figure)
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
