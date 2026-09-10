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
        floor = min(at[1] for at in rows) + 0.02
        run = []
        runs = []
        for at in rows:
            if at[1] < floor:
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
        character(models, args.figure)
        proportion(models, args.figure)
        if args.walks:
            silhouette(engine, args.walks)
            gait(engine, args.walks, args.feet.split(","), args.road, 2.6, 24)
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
