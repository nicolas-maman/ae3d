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


def render(cells, key=lambda c: (c[0] * 0.299 + c[1] * 0.587 + c[2] * 0.114),
           stretch=True):
    """The grid as characters, densest where the frame is brightest.

    Stretched across whatever range the frame actually occupies, because a
    night street is a low-key image: its median is 0.14 and its brightest cell
    0.62, and a ramp laid over 0 to 1 prints most of it as blank space. That is
    how a scene that was correctly exposed came to look empty, and how a figure
    that had come apart went unnoticed in it.
    """
    values = [[key(cell) for cell in row] for row in cells]
    flat = [v for row in values for v in row]
    low, high = (min(flat), max(flat)) if flat else (0.0, 1.0)
    if not stretch or high - low < 1e-6:
        low, high = 0.0, 1.0
    out = []
    for row in values:
        line = ""
        for value in row:
            scaled = max(0.0, min(0.999, (value - low) / (high - low)))
            line += RAMP[int(scaled * len(RAMP))]
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
    parser.add_argument("--absolute", action="store_true",
                        help="lay the ramp over 0 to 1 rather than over what the frame uses")
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
        stretch = not args.absolute
        lines = (render(grid["cells"], stretch=stretch) if key is None
                 else render(grid["cells"], key, stretch))
        for line in lines:
            print(line)
        print("frame %s, %dx%d cells" % (grid.get("frame"), grid["columns"], grid["rows"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
