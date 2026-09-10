"""Print a frame the channel answered with, as something a person can read.

    python tools/ae3d_view.py --port 7911

frame.grid gives the whole picture as cell means in one round trip. This turns
that into a page of characters, densest where the frame is brightest, which is
the closest an agent gets to looking at what it drew -- and, unlike a
screenshot, every character is a number it can also test.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ae3d_session import Session

RAMP = " .:-=+*#%@"


def render(cells, key=lambda c: (c[0] * 0.299 + c[1] * 0.587 + c[2] * 0.114)):
    out = []
    for row in cells:
        line = ""
        for cell in row:
            value = max(0.0, min(0.999, key(cell)))
            line += RAMP[int(value * len(RAMP))]
        out.append(line)
    return out


def main(argv):
    parser = argparse.ArgumentParser(prog="ae3d_view")
    parser.add_argument("--port", type=int, default=7911)
    parser.add_argument("--columns", type=int, default=96)
    parser.add_argument("--rows", type=int, default=36)
    parser.add_argument("--time", type=float, default=None)
    parser.add_argument("--isolate", default=None,
                        help="show only this model, so what prints is its silhouette")
    parser.add_argument("--coverage", action="store_true",
                        help="print how much of each cell is not the background")
    args = parser.parse_args(argv)

    with Session(args.port) as engine:
        engine("frame.pause")
        if args.time is not None:
            engine("anim.set", time=args.time)
        if args.isolate:
            engine("scene.isolate", object=args.isolate)
        grid = engine("frame.grid", columns=args.columns, rows=args.rows)
        if args.isolate:
            engine("scene.isolate")
        key = (lambda c: c[3]) if args.coverage else None
        lines = render(grid["cells"]) if key is None else render(grid["cells"], key)
        for line in lines:
            print(line)
        print("frame %s, %dx%d cells" % (grid.get("frame"), grid["columns"], grid["rows"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
