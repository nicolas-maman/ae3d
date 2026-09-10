"""Measure a running scene through the agent channel.

    AE3D_AGENT=7911 ./build/zombie_street &
    python scripts/measure_scene.py --port 7911

Nothing here is read off a screenshot. Every number comes from the engine that
drew the frame: what each model is made of, where it ended up on screen, what
colour it arrived at, and whether the picture is stable under a movement too
small to change what is in it.

One connection for the whole run, and the questions that can be asked once are
asked once: a round trip costs a frame, because the answer describes a frame
that has to have been drawn first.

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

FAILURES = []

# What launch() answers with instead of a session when the scene stopped
# without a word, which is the engine reporting it has nowhere to draw.
NO_WINDOW = object()


def check(name, ok, detail=""):
    print("  %-4s %s%s" % ("ok" if ok else "FAIL", name, (" (%s)" % detail) if detail else ""))
    if not ok:
        FAILURES.append(name)


def pipeline(engine):
    """Every model, in one question."""
    print("== every part, from the .blend to the pixels ==")
    scene = engine("scene.tree", detail=True)
    models = scene["models"]
    named = [m for m in models if m.get("texture")]
    unloaded = [m["name"] for m in named if not m.get("texture_loaded")]
    offscreen = [m["name"] for m in models if not m.get("screen_region")]
    check("every model in view lands on screen",
          len(offscreen) + len([m for m in models if m.get("screen_region")]) == scene["count"],
          "%d of %d; off screen: %s"
          % (scene["count"] - len(offscreen), scene["count"], ", ".join(offscreen) or "none"))
    check("every image a material names was loaded", not unloaded,
          ", ".join(unloaded) if unloaded else "%d textured materials" % len(named))
    return {m["name"]: m for m in models}


def traced(engine, names):
    """The full trace for a few models, which is one round trip each."""
    print("\n== the stages each part came through ==")
    for name in names:
        stages = {s["stage"]: s for s in engine("trace.model", object=name)["stages"]}
        reached = [n for n in ("source", "asset", "mesh", "node", "animation",
                               "visibility", "pixels") if stages.get(n, {}).get("reached")]
        check("  %s reaches the pixels it covers" % name,
              "pixels" in reached, "got as far as %s" % (reached[-1] if reached else "nothing"))


def surfaces(engine, models, sky, wanted):
    """Each surface alone against the sky, so the mean is the surface."""
    print("\n== the material each surface arrived with ==")
    packed = (int(sky[0] * 255) << 24) | (int(sky[1] * 255) << 16) | (int(sky[2] * 255) << 8)
    measured = {}
    for name, (label, material) in wanted.items():
        model = models.get(name)
        if model is None or not model.get("screen_region"):
            print("  %-9s not in view" % label)
            continue
        engine("scene.isolate", object=name)
        box = engine("trace.model", object=name)["stages"]
        box = {s["stage"]: s for s in box}["visibility"]["detail"].get("screen_region")
        if box is None:
            print("  %-9s not in view" % label)
            continue
        got = engine("frame.region", x=box["x"], y=box["y"],
                     width=box["width"], height=box["height"],
                     background=packed, tolerance=12)
        covered = got.get("coverage", 0.0)
        # A surface that fills too little of its own box cannot be separated
        # from what is behind it: the background is subtracted in proportion to
        # what it does not cover, and below a third that estimate is worth less
        # than the measurement. Thin trim and small panes land here, and saying
        # so is better than reporting a colour with a negative in it.
        if covered < 0.30:
            print("  %-9s covers %.1f%% of its own box" % (label, covered * 100))
            continue
        colour = tuple((got["mean"][i] - sky[i] * (1 - covered)) / covered for i in range(3))
        measured[label] = colour
        print("  %-9s %-14s r=%.3f g=%.3f b=%.3f  r/g=%.2f g/b=%.2f  covers %3.0f%%  stddev %.4f"
              % (label, model.get("material", "?"), colour[0], colour[1], colour[2],
                 colour[0] / max(colour[1], 1e-6), colour[1] / max(colour[2], 1e-6),
                 covered * 100, got["stddev"]))
        check("  %s is the material Blender gave it" % label,
              model.get("material") == material, model.get("material"))
    engine("scene.isolate")
    return measured


def depth_is_decided(engine, position, target):
    print("\n== the depth test never has to choose ==")
    engine("camera.set", position=list(position), look_at=list(target))
    engine("frame.hold")
    still = engine("frame.diff", tolerance=2)
    check("the same camera draws the same frame", still.get("changed") == 0,
          "%d pixels of %d" % (still.get("changed", -1), still.get("pixels", 0)))

    # Two surfaces fighting over a depth swap which of them wins when the
    # camera moves by less than the geometry cares about. Resampling moves a
    # few edge pixels; a fight flips areas.
    for axis, label in ((0, "sideways"), (1, "upwards")):
        nudged = list(position)
        nudged[axis] += 0.0005
        engine("frame.hold")
        engine("camera.set", position=nudged, look_at=list(target))
        moved = engine("frame.diff", tolerance=24)
        check("half a millimetre %s moves almost nothing" % label,
              moved.get("fraction", 1.0) < 0.01,
              "%.3f%% of the frame" % (moved.get("fraction", 1.0) * 100))
    engine("camera.set", position=list(position), look_at=list(target))


def animation_arrives(engine, part, rest, moment, width):
    print("\n== the animation reaches the picture ==")
    engine("anim.set", time=rest)
    engine("frame.hold")
    before = engine("trace.model", object=part)
    engine("anim.set", time=moment)
    changed = engine("frame.diff", tolerance=16)
    after = engine("trace.model", object=part)

    def where(answer):
        detail = {s["stage"]: s for s in answer["stages"]}["visibility"]["detail"]
        return detail.get("screen_x"), detail.get("screen_y")

    (x0, y0), (x1, y1) = where(before), where(after)
    moved = 0.0
    if None not in (x0, y0, x1, y1):
        moved = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
    # How far a hand swings is a fraction of the picture, not a number of
    # pixels: the same scene drawn at a CI's size has to answer the same.
    want = width * 0.03
    print("  %s moved %.0f pixels between %ss and %ss (%.1f%% of the width)"
          % (part, moved, rest, moment, moved * 100.0 / width))
    check("seeking changes the picture", changed.get("fraction", 0.0) > 0.005,
          "%.2f%% of the frame" % (changed.get("fraction", 0.0) * 100))
    check("and the part that should have moved did", moved > want,
          "%.0f pixels, wanted more than %.0f" % (moved, want))


def launch(binary, port):
    """Start the scene with the channel open, and wait for it to answer."""
    env = dict(os.environ)
    env["AE3D_AGENT"] = str(port)
    env["AE3D_FRAMES"] = "100000"
    log = tempfile.NamedTemporaryFile(prefix="ae3d_measure_", suffix=".log", delete=False)
    scene = subprocess.Popen([binary], env=env, stdout=log, stderr=subprocess.STDOUT)
    deadline = time.time() + 30.0
    while time.time() < deadline:
        if scene.poll() is not None:
            # A scene that stops of its own accord without complaining is one
            # that could not open a window: the engine says so and returns,
            # which on a runner with no display is the right thing to do and
            # nothing this script can measure. Anything else is a failure.
            said = open(log.name).read()
            print("measure_scene: %s exited with %d before it opened the channel"
                  % (binary, scene.returncode))
            print(said[-2000:])
            return scene, log.name, None if scene.returncode else NO_WINDOW
        try:
            return scene, log.name, Session(port)
        except OSError:
            time.sleep(0.25)
    print("measure_scene: %s never answered on port %d" % (binary, port))
    return scene, log.name, None


def stop(scene):
    scene.terminate()
    try:
        scene.wait(timeout=5)
    except subprocess.TimeoutExpired:
        scene.kill()


def main(argv):
    parser = argparse.ArgumentParser(prog="measure_scene")
    parser.add_argument("--port", type=int, default=7911)
    parser.add_argument("--launch", metavar="BINARY",
                        help="start this scene first, and stop it at the end")
    args = parser.parse_args(argv)

    started = time.time()
    scene = None
    if args.launch:
        scene, log, engine = launch(args.launch, args.port)
        if engine is NO_WINDOW:
            stop(scene)
            return 3
        if engine is None:
            stop(scene)
            return 2
    else:
        try:
            engine = Session(args.port)
        except OSError as failure:
            print("measure_scene: nothing answering on port %d (%s)" % (args.port, failure))
            return 2

    with engine:
        stats = engine("frame.stats")
        render = stats["render"]
        print("%s, %dx%d, %d draws, shadows %s, fog %s, %d lights, %.2f ms a frame"
              % (stats["backend"], stats["width"], stats["height"], stats["draw_calls"],
                 render["shadows"], render["fog"], render["lights"],
                 stats.get("render_ms", 0.0)))

        engine("frame.pause")
        engine("anim.set", time=3.35)
        models = pipeline(engine)
        traced(engine, ["Zombie_Body", "Zombie_Clothes", "Street_Road", "Street_LampHead0"])
        surfaces(engine, models, (0.075, 0.088, 0.125), {
            "Street_BlockL0": ("brick", "WallBrick"),
            "Street_BlockL1": ("concrete", "WallConcrete"),
            "Street_PathL": ("paving", "PathPaving"),
            "Street_Road": ("tarmac", "RoadTarmac"),
            "Zombie_Body": ("skin", "ZombieSkin"),
            "Zombie_Clothes": ("cloth", "ZombieCloth"),
            "Street_LampHead0": ("lamp", "LampGlow"),
        })
        depth_is_decided(engine, (0.35, 1.34, 1.55), (-3.5, 1.1, -0.2))
        animation_arrives(engine, "Zombie_Body", 1.4, 3.35, stats["width"])
        engine("frame.resume")
    if scene is not None:
        stop(scene)

    print("\n%s in %.1fs"
          % ("measure_scene: everything measured passed" if not FAILURES
             else "measure_scene: %d measurement(s) failed" % len(FAILURES),
             time.time() - started))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except AgentError as refused:
        print("measure_scene: %s" % refused)
        sys.exit(2)
