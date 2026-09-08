#!/usr/bin/env python3
"""Drive the editor through its own widgets and check what happens.

Every other check on the editor reads the report it writes about itself, and
that report is produced by calling the handlers directly. A button that is
never hit-tested, a field whose callback is not wired, a row that cannot be
clicked: all of them pass those checks, because none of them go anywhere near
a widget.

aether-ui's driver does. It answers /widgets with the live tree and takes
clicks and keystrokes on any of it, so this presses the real buttons and types
into the real fields, then asks the tree what changed.

    tools/drive_editor.py [--port N] [--binary build/ae3d_editor]

Exits non-zero with a line saying what failed.
"""

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

FAILURES = []


def check(name, ok, detail=""):
    if ok:
        print("   ok    %s" % name)
    else:
        print("   FAIL  %s%s" % (name, (" (%s)" % detail) if detail else ""))
        FAILURES.append(name)


def get(port, path, tries=40):
    last = None
    for _ in range(tries):
        try:
            with urllib.request.urlopen("http://127.0.0.1:%d%s" % (port, path),
                                        timeout=3) as r:
                return json.loads(r.read().decode())
        except Exception as e:            # the window takes a moment to exist
            last = e
            time.sleep(0.5)
    raise SystemExit("editor driver never answered %s: %s" % (path, last))


def post(port, path):
    req = urllib.request.Request("http://127.0.0.1:%d%s" % (port, path),
                                 data=b"", method="POST")
    with urllib.request.urlopen(req, timeout=3) as r:
        return r.read()


def tree(port):
    return {w["id"]: w for w in get(port, "/widgets")}


def find(widgets, kind, text):
    for w in widgets.values():
        if w["type"] == kind and w["text"].strip() == text:
            return w["id"]
    return None


def rows_under(widgets, parent_id):
    return [w for w in widgets.values() if w["parent"] == parent_id]


def scene_list_id(widgets):
    # The hierarchy sits directly under the SCENE heading, and the heading and
    # the list share a parent. Found by structure rather than by a fixed id,
    # because ids move whenever the panel gains a widget.
    for w in widgets.values():
        if w["type"] == "text" and w["text"].strip() == "SCENE":
            siblings = [s for s in widgets.values() if s["parent"] == w["parent"]]
            after = [s for s in siblings if s["id"] > w["id"]]
            for s in sorted(after, key=lambda s: s["id"]):
                if s["type"] in ("vstack", "listbox"):
                    return s["id"]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8791)
    ap.add_argument("--binary", default="build/ae3d_editor")
    args = ap.parse_args()

    env = dict(os.environ)
    env["AETHER_UI_TEST_PORT"] = str(args.port)
    # Long enough that the run outlives this script; it is killed at the end.
    env["AE3D_EDITOR_FRAMES"] = "100000"
    editor = subprocess.Popen([args.binary], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        widgets = tree(args.port)

        scene = scene_list_id(widgets)
        if scene is None:
            raise SystemExit("could not find the scene list in the widget tree")
        before = len(rows_under(widgets, scene))

        # A button, pressed where a person would press it.
        cube = find(widgets, "button", "Cube")
        check("the Cube button is in the tree", cube is not None)
        if cube is not None:
            post(args.port, "/widget/%d/click" % cube)
            time.sleep(1.0)
            widgets = tree(args.port)
            after = len(rows_under(widgets, scene))
            check("clicking Cube adds an object", after == before + 1,
                  "%d rows before, %d after" % (before, after))

        # Undo, pressed rather than called. The report's own undo check calls
        # the editor's function from the top level; the button reaches it
        # through a closure built inside the ui.window block, which is a
        # different scope and was binding a different function entirely.
        undo = find(widgets, "button", "Undo")
        check("the Undo button is in the tree", undo is not None)
        if undo is not None and cube is not None:
            post(args.port, "/widget/%d/click" % undo)
            time.sleep(1.0)
            widgets = tree(args.port)
            undone = len(rows_under(widgets, scene))
            check("clicking Undo takes the object back off", undone == before,
                  "%d rows, expected %d" % (undone, before))

        # A number field, typed into. The value has to reach the model, which
        # is only proved by selecting away and back: what comes back is the
        # model's own formatting, not the text that was typed.
        fields = sorted([w for w in widgets.values() if w["type"] == "textfield"],
                        key=lambda w: (w["y"], w["x"]))
        check("the inspector has number fields", len(fields) >= 4)
        if fields:
            x_field = fields[0]["id"]
            post(args.port, "/widget/%d/set_text?v=-150.0" % x_field)
            time.sleep(0.6)
            typed = tree(args.port)[x_field]["text"]
            check("the field takes what is typed", typed == "-150.0", repr(typed))

            rows = sorted(rows_under(tree(args.port), scene), key=lambda w: w["id"])
            if len(rows) >= 2:
                post(args.port, "/widget/%d/click" % rows[0]["id"])
                time.sleep(0.6)
                other = tree(args.port)[x_field]["text"]
                check("selecting another object rereads the field",
                      other != "-150.0", repr(other))

                post(args.port, "/widget/%d/click" % rows[-1]["id"])
                time.sleep(0.6)
                back = tree(args.port)[x_field]["text"]
                # number() writes two decimals; "-150.0" is what was typed.
                check("what was typed reached the model", back == "-150.00",
                      repr(back))
        # A section with nothing to edit hides whole. Hiding a control and its
        # readout but not the row leaves the caption behind, and the water
        # settings read as four stranded words with no heading over them.
        widgets = tree(args.port)
        captions = [w for w in widgets.values()
                    if w["type"] == "text" and w["text"].strip() == "wave height"]
        check("the water section is in the tree", len(captions) == 1)
        if captions:
            frame = widgets.get(captions[0]["parent"])
            check("with no water, its rows are hidden and not just their controls",
                  frame is not None and not frame["visible"],
                  "row %s visible=%s" % (frame["id"] if frame else "?",
                                         frame["visible"] if frame else "?"))

        # Save, Load and Delete, pressed. A scene that never reaches disk and
        # a Load that brings back nothing both look like a working editor from
        # inside: the buttons return, the report is unchanged.
        scene_file = "build/editor_scene.json"
        if os.path.exists(scene_file):
            os.remove(scene_file)
        save = find(widgets, "button", "Save")
        load = find(widgets, "button", "Load")
        delete = find(widgets, "button", "Delete")
        if save and load and delete:
            saved_rows = len(rows_under(tree(args.port), scene))
            post(args.port, "/widget/%d/click" % save)
            time.sleep(1.2)
            check("Save writes a scene file",
                  os.path.exists(scene_file) and os.path.getsize(scene_file) > 0)

            post(args.port, "/widget/%d/click" % find(tree(args.port), "button", "Cube"))
            time.sleep(1.0)
            grew = len(rows_under(tree(args.port), scene))
            post(args.port, "/widget/%d/click" % load)
            time.sleep(1.8)
            loaded = len(rows_under(tree(args.port), scene))
            check("Load brings back what was saved",
                  loaded == saved_rows and grew == saved_rows + 1,
                  "%d saved, %d after adding, %d after loading"
                  % (saved_rows, grew, loaded))

            before_delete = len(rows_under(tree(args.port), scene))
            post(args.port, "/widget/%d/click" % delete)
            time.sleep(1.0)
            after_delete = len(rows_under(tree(args.port), scene))
            check("Delete takes an object out",
                  after_delete == before_delete - 1,
                  "%d rows, expected %d" % (after_delete, before_delete - 1))
    finally:
        editor.terminate()
        try:
            editor.wait(timeout=5)
        except subprocess.TimeoutExpired:
            editor.kill()

    if FAILURES:
        print("editor driver: %d failure(s)" % len(FAILURES))
        return 1
    print("editor driver: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
