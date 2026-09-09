"""Measure a running scene through the agent channel.

    AE3D_AGENT=7911 ./build/zombie_street &
    python scripts/measure_scene.py --port 7911

Nothing here is read off a screenshot. Every number comes from the engine that
drew the frame: what each model is made of, where it ended up on screen, what
colour it arrived at, and whether the picture is stable under a movement too
small to change what is in it.

Exits non-zero with a line naming what failed.
"""

import argparse
import json
import subprocess
import sys

FAILURES = []


def check(name, ok, detail=""):
    print("  %-4s %s%s" % ("ok" if ok else "FAIL", name, (" (%s)" % detail) if detail else ""))
    if not ok:
        FAILURES.append(name)


class Channel:
    def __init__(self, port):
        self.port = str(port)

    def __call__(self, *args):
        done = subprocess.run([sys.executable, "tools/ae3d_agent.py", "--port", self.port, *args],
                              capture_output=True, text=True)
        if done.returncode != 0:
            return {"ok": False, "error": (done.stdout or done.stderr).strip()}
        return json.loads(done.stdout)

    def stages(self, name):
        answered = self("trace.model", "object=%s" % name)
        return {s["stage"]: s for s in answered.get("stages", [])}

    def camera(self, position, target):
        self("camera.set", "position=[%r,%r,%r]" % position, "look_at=[%r,%r,%r]" % target)


def pipeline(ask):
    print("== every part, from the .blend to the pixels ==")
    tree = ask("scene.tree")
    reached, behind, named, unloaded = 0, [], 0, []
    for model in tree["models"]:
        stages = ask.stages(model["name"])
        if stages.get("pixels", {}).get("reached"):
            reached += 1
        else:
            behind.append(model["name"])
        material = stages.get("node", {}).get("detail", {}).get("material", {})
        if material.get("texture"):
            named += 1
            if not material.get("texture_loaded"):
                unloaded.append(model["name"])
    check("every model in view traces through to pixels",
          reached == tree["count"] - len(behind),
          "%d of %d; behind the camera: %s" % (reached, tree["count"], ", ".join(behind) or "none"))
    check("every image a material names was loaded", not unloaded,
          ", ".join(unloaded) if unloaded else "%d textured materials" % named)
    return {m["name"]: m["index"] for m in tree["models"]}


def surfaces(ask, index_of, sky, wanted):
    """Each surface alone against the sky, so the mean is the surface."""
    print("\n== the material each surface arrived with ==")
    packed = (int(sky[0] * 255) << 24) | (int(sky[1] * 255) << 16) | (int(sky[2] * 255) << 8)
    measured = {}
    for name, (label, material_name) in wanted.items():
        for other, index in index_of.items():
            ask("model.set", "index=%d" % index, "visible=%s" % ("true" if other == name else "false"))
        stages = ask.stages(name)
        box = stages.get("visibility", {}).get("detail", {}).get("screen_region")
        if not box:
            print("  %-9s not in view" % label)
            continue
        got = ask("frame.region", "x=%d" % box["x"], "y=%d" % box["y"],
                  "width=%d" % box["width"], "height=%d" % box["height"],
                  "background=%d" % packed, "tolerance=12")
        covered = got.get("coverage", 0.0)
        if "mean" not in got or covered < 0.02:
            print("  %-9s covers %.1f%% of its own box" % (label, covered * 100))
            continue
        colour = tuple((got["mean"][i] - sky[i] * (1 - covered)) / covered for i in range(3))
        measured[label] = colour
        node = stages.get("node", {}).get("detail", {}).get("material", {})
        print("  %-9s %-14s r=%.3f g=%.3f b=%.3f  r/g=%.2f g/b=%.2f  covers %3.0f%%  stddev %.4f"
              % (label, node.get("name", "?"), colour[0], colour[1], colour[2],
                 colour[0] / max(colour[1], 1e-6), colour[1] / max(colour[2], 1e-6),
                 covered * 100, got["stddev"]))
        check("  %s is the material Blender gave it" % label,
              node.get("name") == material_name, node.get("name"))
    for index in index_of.values():
        ask("model.set", "index=%d" % index, "visible=true")
    return measured


def depth_is_decided(ask, position, target):
    print("\n== the depth test never has to choose ==")
    ask.camera(position, target)
    ask("frame.hold")
    ask.camera(position, target)
    still = ask("frame.diff", "tolerance=2")
    check("the same camera draws the same frame", still.get("changed") == 0,
          "%d pixels of %d" % (still.get("changed", -1), still.get("pixels", 0)))

    # Two surfaces fighting over a depth swap which of them wins when the
    # camera moves by less than the geometry cares about. Resampling moves a
    # few edge pixels; a fight flips areas.
    for axis, label in ((0, "sideways"), (1, "upwards")):
        nudged = list(position)
        nudged[axis] += 0.0005
        ask("frame.hold")
        ask.camera(tuple(nudged), target)
        moved = ask("frame.diff", "tolerance=24")
        check("half a millimetre %s moves almost nothing" % label,
              moved.get("fraction", 1.0) < 0.01,
              "%.3f%% of the frame" % (moved.get("fraction", 1.0) * 100))
    ask.camera(position, target)


def animation_arrives(ask, part, rest, moment):
    print("\n== the animation reaches the picture ==")
    ask("anim.set", "time=%r" % rest)
    ask("frame.hold")
    before = ask.stages(part)
    ask("anim.set", "time=%r" % moment)
    changed = ask("frame.diff", "tolerance=16")
    after = ask.stages(part)

    def where(stages):
        detail = stages.get("visibility", {}).get("detail", {})
        return detail.get("screen_x"), detail.get("screen_y")

    (x0, y0), (x1, y1) = where(before), where(after)
    moved = 0.0
    if None not in (x0, y0, x1, y1):
        moved = ((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5
    print("  %s moved %.0f pixels between %ss and %ss" % (part, moved, rest, moment))
    check("seeking changes the picture", changed.get("fraction", 0.0) > 0.005,
          "%.2f%% of the frame" % (changed.get("fraction", 0.0) * 100))
    check("and the part that should have moved did", moved > 60, "%.0f pixels" % moved)


def main(argv):
    parser = argparse.ArgumentParser(prog="measure_scene")
    parser.add_argument("--port", type=int, default=7911)
    args = parser.parse_args(argv)
    ask = Channel(args.port)

    stats = ask("frame.stats")
    if not stats or "render" not in stats:
        print("measure_scene: nothing answering on port %d" % args.port)
        return 2
    print("%s, %d draws, shadows %s, fog %s, %d lights"
          % (stats["backend"], stats["draw_calls"], stats["render"]["shadows"],
             stats["render"]["fog"], stats["render"]["lights"]))

    ask("frame.pause")
    ask("anim.set", "time=3.35")
    index_of = pipeline(ask)
    surfaces(ask, index_of, (0.055, 0.065, 0.095), {
        "Street_BlockL0": ("brick", "WallBrick"),
        "Street_BlockL1": ("concrete", "WallConcrete"),
        "Street_PathL": ("paving", "PathPaving"),
        "Street_Road": ("tarmac", "RoadTarmac"),
        "Zombie_Head": ("skin", "ZombieSkin"),
        "Zombie_Spine": ("cloth", "ZombieCloth"),
        "Street_LampHead0": ("lamp", "LampGlow"),
    })
    depth_is_decided(ask, (0.35, 1.34, 1.55), (-3.5, 1.1, -0.2))
    animation_arrives(ask, "Zombie_HandR", 1.4, 3.35)
    ask("frame.resume")

    print("\n%s" % ("measure_scene: everything measured passed" if not FAILURES
                    else "measure_scene: %d measurement(s) failed" % len(FAILURES)))
    return 1 if FAILURES else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
