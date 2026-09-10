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

  Silhouette. How many directions a shape's faces point. A box has six however
  many triangles it is cut into, and everything that makes a building read as a
  building -- a recessed window, a sill, a course line, a doorway -- is a
  normal the box does not have. This is the measure that says "it is a box"
  without anybody having to look at it.

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
# A shape with no more ways to face than a cube has is a cube.
BOX_NORMALS = 6
# What a figure has to carry to read as one rather than as a stack of props.
CHARACTER_TRIANGLES = 20000
CHARACTER_NORMALS = 2000

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


def silhouette(models):
    print("\n== what the shapes are, rather than what they are painted with ==")
    boxes = [m for m in models
             if m.get("normals", 0) <= BOX_NORMALS and m.get("area_m2", 0.0) > 1.0]
    for m in sorted(boxes, key=lambda m: -m.get("area_m2", 0.0))[:6]:
        print("  %-22s %6d triangles, %3d normals, %8.1f m2"
              % (m["name"], m.get("triangles", 0), m.get("normals", 0), m["area_m2"]))
    check("nothing large enough to be looked at is a bare box", not boxes,
          "%d of them, %s" % (len(boxes), ", ".join(m["name"] for m in boxes[:4])))


def character(models, prefix):
    print("\n== the figure ==")
    parts = [m for m in models if m["name"].startswith(prefix)]
    if not parts:
        check("the scene has a figure in it", False, "nothing named %s" % prefix)
        return
    triangles = sum(m.get("triangles", 0) for m in parts)
    normals = sum(m.get("normals", 0) for m in parts)
    area = sum(m.get("area_m2", 0.0) for m in parts)
    print("  %d part(s), %d triangles, %d normals, %.2f m2 of surface"
          % (len(parts), triangles, normals, area))
    check("the figure carries the geometry a figure needs",
          triangles >= CHARACTER_TRIANGLES,
          "%d triangles, wanted %d" % (triangles, CHARACTER_TRIANGLES))
    check("and enough of a silhouette to read as one",
          normals >= CHARACTER_NORMALS,
          "%d distinct normals, wanted %d" % (normals, CHARACTER_NORMALS))
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


def main(argv):
    parser = argparse.ArgumentParser(prog="critique_scene")
    parser.add_argument("--port", type=int, default=7911)
    parser.add_argument("--launch", metavar="BINARY")
    parser.add_argument("--figure", default="Zombie_")
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
        silhouette(models)
        character(models, args.figure)
        proportion(models, args.figure)
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
