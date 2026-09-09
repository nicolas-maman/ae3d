#!/usr/bin/env python3
"""Refuse a name the editor shares with the toolkit it imports.

The editor defines undo() and redo(); aether-ui exports names of its own for
stepping the toolkit's command stack. Inside the ui.window block a bare call
bound to the toolkit's, so the Undo button stepped an empty stack and did
nothing, while the editor's own check called the same name from the top level,
got its own function, and reported a working undo.

Nothing warns about that, in either direction, and it comes back whenever
either side gains a name. So it is checked: no function the editor defines may
share a name with something `ui` exports.

    tools/check_ui_name_collisions.py [--ui PATH] [--editor PATH]
"""

import argparse
import os
import re
import sys


def exported_names(path):
    src = open(path).read()
    block = re.search(r"exports\s*\((.*?)\n\)", src, re.S)
    if not block:
        raise SystemExit("no exports block in %s" % path)
    return set(n.strip() for n in block.group(1).replace("\n", " ").split(",")
               if n.strip())


def defined_names(path):
    return set(re.findall(r"^([a-z_][a-z0-9_]*)\s*\(", open(path).read(), re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", default=os.environ.get("AETHER_UI_ROOT", "../aether-ui")
                    + "/ui/module.ae")
    ap.add_argument("--editor", default="editor/editor.ae")
    args = ap.parse_args()

    if not os.path.exists(args.ui):
        print("skip: aether-ui not found at %s" % args.ui)
        return 0

    exports = exported_names(args.ui)
    # The check can only work if the exports parsed; a silent empty set would
    # make it pass forever.
    if len(exports) < 50:
        raise SystemExit("only %d exports parsed from %s; the check would be "
                         "vacuous" % (len(exports), args.ui))

    clashes = sorted(defined_names(args.editor) & exports)
    if not clashes:
        print("no editor function shares a name with a ui export "
              "(%d exports checked)" % len(exports))
        return 0

    print("%d name(s) the editor defines and ui also exports:" % len(clashes))
    for name in clashes:
        print("    %s" % name)
    print("A bare call to one of these inside the ui.window block binds the "
          "toolkit's, not the editor's, and nothing says so.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
