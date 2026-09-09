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
import tempfile
import time
import urllib.error
import urllib.request

ACCENT = "#2e6eeb"
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
    raise SystemExit("editor driver never answered %s: %s%s"
                     % (path, last, editor_output()))


EDITOR_LOG = [None]
EDITOR_PROC = [None]


def editor_sample(pid):
    """Where the editor is stuck, from the platform sampler if there is one."""
    try:
        out = subprocess.run(["sample", str(pid), "1", "-mayDie"],
                             capture_output=True, text=True, timeout=30).stdout
    except Exception:
        return ""
    if not out.strip():
        return ""
    kept = tempfile.NamedTemporaryFile(prefix="ae3d_sample_", suffix=".txt",
                                       delete=False, mode="w")
    kept.write(out)
    kept.close()

    # The main thread, which is the one that stopped answering. Its block
    # starts at the line naming it and runs to the next blank line.
    frames = []
    started = False
    for line in out.splitlines():
        if not started:
            started = "com.apple.main-thread" in line
            if started:
                frames.append(line.rstrip())
            continue
        if not line.strip():
            break
        frames.append(line.rstrip())
    body = "\n  stuck at:\n    " + "\n    ".join(frames[:24]) if frames else ""
    return body + "\n  full sample: " + kept.name


def editor_output():
    """What the editor said before it stopped answering, if anything."""
    note = ""
    proc = EDITOR_PROC[0]
    if proc is not None:
        code = proc.poll()
        if code is None:
            # Still there and not answering, which is a blocked main thread
            # rather than a crash. Sampled here because this is the only
            # moment it can be: the process is killed on the way out of this
            # script, and by the time anything outside can look it is gone.
            note = "\n  the editor is still running" + editor_sample(proc.pid)
        else:
            note = "\n  the editor had already exited, status %s" % code
    path = EDITOR_LOG[0]
    if not path or not os.path.exists(path):
        return note
    with open(path) as f:
        tail = f.read().strip().splitlines()[-12:]
    if not tail:
        return note + "\n  (the editor said nothing)"
    return note + "\n  editor said:\n    " + "\n    ".join(tail)


def post(port, path):
    req = urllib.request.Request("http://127.0.0.1:%d%s" % (port, path),
                                 data=b"", method="POST")
    with urllib.request.urlopen(req, timeout=3) as r:
        return r.read()


def tree(port):
    return {w["id"]: w for w in get(port, "/widgets")}


def wait_rows(port, scene, count, seconds=8.0):
    """Wait for the scene list to hold `count` rows, then report what it holds.

    Every count in here is asserted after an action the editor performs in its
    own time. Sleeping first and asserting second passes on an idle machine and
    fails inside a full ci run, and the failure names the check rather than the
    machine, which is the worst way to learn about a timing assumption.
    """
    widgets, _ = wait_for(port,
                          lambda ws: len(rows_under(ws, scene)) == count,
                          seconds)
    return len(rows_under(widgets, scene))


def wait_file(path, seconds=8.0, newer_than=None):
    """Wait for a file to exist, or to have been written again since.

    Existence alone is not enough for a second save: the file is already there
    from the first one, so the wait returns at once and a Load that follows can
    read what was on disk before rather than what was just asked for. That is a
    race that passes almost every time and fails on a busy machine, which is
    the worst kind to have in a suite.
    """
    deadline = time.time() + seconds
    while time.time() < deadline:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            if newer_than is None or os.path.getmtime(path) > newer_than:
                return True
        time.sleep(0.1)
    return (os.path.exists(path) and os.path.getsize(path) > 0
            and (newer_than is None or os.path.getmtime(path) > newer_than))


def wait_for(port, predicate, seconds=8.0):
    """Poll the tree until predicate(widgets) holds, or give up.

    A fixed sleep is a guess about how long the editor takes to build a water
    surface and lay the panel out again, and a guess that is usually right is
    the worst kind: the check passes on this machine and fails on a slower one
    for reasons that have nothing to do with the editor.
    """
    deadline = time.time() + seconds
    widgets = tree(port)
    while time.time() < deadline:
        if predicate(widgets):
            return widgets, True
        time.sleep(0.25)
        widgets = tree(port)
    return widgets, predicate(widgets)


def find(widgets, kind, text):
    for w in widgets.values():
        if w["type"] == kind and w["text"].strip() == text:
            return w["id"]
    return None


def rows_under(widgets, parent_id):
    return [w for w in widgets.values() if w["parent"] == parent_id]


def descendants(widgets, parent_id):
    """Every widget under one, nearest first."""
    out = []
    queue = [w for w in widgets.values() if w["parent"] == parent_id]
    while queue:
        w = queue.pop(0)
        out.append(w)
        queue.extend(k for k in widgets.values() if k["parent"] == w["id"])
    return out


def in_panel(widgets, w):
    """The ancestor of a widget that sits directly in a panel's stack.

    A section caption is nested inside its bar, so it is not a sibling of the
    thing the section holds. Found by climbing rather than by depth, because
    the depth is a detail of how the bar is built.
    """
    while w is not None:
        parent = widgets.get(w["parent"])
        if parent is None:
            return None
        if "panel-body" in (parent.get("classes") or ""):
            return w
        w = parent
    return None


def scene_list_id(widgets):
    # The list inside the SCENE section. Found by structure rather than by a
    # fixed id, because ids move whenever the panel gains a widget, and by
    # looking for the list itself rather than for whatever follows the bar: the
    # section's body is a stack, so "the next container" is the body and the
    # rows are a level further down.
    for w in widgets.values():
        if w["type"] == "text" and w["text"].strip() == "SCENE":
            bar = in_panel(widgets, w)
            if bar is None:
                continue
            after = sorted([s for s in widgets.values()
                            if s["parent"] == bar["parent"] and s["id"] > bar["id"]],
                           key=lambda s: s["id"])
            for body in after:
                # The section's body, and the list is the first container in
                # it. The toolkit reports a listbox as a stack, so this cannot
                # ask for the type by name and has to take the first one.
                for kid in sorted([k for k in widgets.values()
                                   if k["parent"] == body["id"]],
                                  key=lambda k: k["id"]):
                    if kid["type"] in ("vstack", "listbox"):
                        return kid["id"]
                break
    return None


def row_name(widgets, row):
    """The object's name, past the one-character kind marker the row carries.

    The list draws " ~  water" so the kind is readable before anything is
    selected. A check that compared the whole label against "water" was
    reading the presentation, and broke the moment the presentation improved.
    """
    kids = [c["text"].strip() for c in descendants(widgets, row["id"])
            if c["text"].strip()]
    if not kids:
        return "?"
    parts = kids[0].split(None, 1)
    if len(parts) == 2 and len(parts[0]) == 1:
        return parts[1].strip()
    return kids[0]


def water_section_visible(widgets):
    caps = [w for w in widgets.values()
            if w["type"] == "text" and w["text"].strip() == "wave height"]
    if not caps:
        return None
    frame = widgets.get(caps[0]["parent"])
    return frame["visible"] if frame else None


def audit_kinds(port, scene, step):
    """Every row has to report its own kind, whatever happened before it.

    The model, the component saying what it is, and the script sit at the same
    index in three lists. When only some of them move, every object past that
    point answers for another one, and the way that shows is an object whose
    inspector belongs to something else. Selecting each row and asking whether
    the water section is open is the cheapest question that notices.
    """
    widgets = tree(port)
    rows = rows_under(widgets, scene)
    rows.sort(key=lambda w: w["id"])
    for row in rows:
        name = row_name(widgets, row)
        post(port, "/widget/%d/click" % row["id"])
        time.sleep(0.45)
        shown = water_section_visible(tree(port))
        if shown != (name == "water"):
            return "after %s, the row %r reports the water section as %s" % (
                step, name, shown)
    return None


def on_screen(widgets, w):
    """Whether a widget and every container above it is showing."""
    while w is not None:
        if not w.get("visible"):
            return False
        w = widgets.get(w["parent"])
    return True


def row_box(widgets, caption):
    """The number box on an inspector row, given the row's caption."""
    return [w for w in widgets.values()
            if w["parent"] == caption["parent"] and w["type"] == "textfield"]


def layout_faults(widgets):
    """Things a panel gets wrong that a person reads past.

    Both of these were found by measuring the tree by hand and neither by
    looking at screenshots, across several passes over the same panels. They
    are mechanical properties, so they are checked rather than remembered.
    """
    faults = []
    children = {}
    for w in widgets.values():
        children.setdefault(w["parent"], []).append(w)

    for parent, kids in children.items():
        shown = sorted([k for k in kids if k.get("visible") and k["h"] > 0],
                       key=lambda k: k["y"])

        # Two rules with nothing between them. A section that hides its heading
        # and its rows and keeps its rule leaves one stacked against the next
        # section's, which is what the water settings did.
        previous = None
        for kid in shown:
            if previous is not None and previous["type"] == "divider" \
                    and kid["type"] == "divider":
                faults.append("two rules with nothing between them at y=%d and y=%d"
                              % (previous["y"], kid["y"]))
            previous = kid

        # Rows of buttons in one stack start at one x. A row that carries its
        # own inset over the panel's starts further in than the full-width
        # buttons above and below it: the same control with two edges.
        #
        # Only where the children are stacked vertically. Two buttons side by
        # side in a row have different left edges because that is what a row
        # is, and the first version of this check flagged every pair in the
        # panel for it.
        container = widgets.get(parent)
        if container is None or container["type"] != "vstack":
            continue
        starts = {}
        for kid in shown:
            buttons = [w for w in widgets.values()
                       if w["type"] == "button" and w.get("visible") and w["w"] > 0
                       and (w["id"] == kid["id"] or w["parent"] == kid["id"])]
            if buttons:
                starts[kid["id"]] = min(b["x"] for b in buttons)
        edges = sorted(set(starts.values()))
        if len(edges) > 1:
            faults.append("button rows in one stack start at %s" % edges)

    # A control with no size is a control nobody can use, and the tree is the
    # only place it shows.
    for w in widgets.values():
        if w.get("visible") and w["type"] in ("button", "textfield") \
                and (w["w"] <= 0 or w["h"] <= 0):
            faults.append("%s %r has no size" % (w["type"], w["text"][:20]))

    # A frame inside a frame of the same colour, shorter than the one around
    # it, has its bottom edge somewhere in the middle: a line drawn across the
    # panel with nothing under it, and the column reads as a card that stopped
    # short of the window.
    for w in widgets.values():
        parent = widgets.get(w["parent"])
        if parent is None or not w.get("visible") or not w.get("borderWidth"):
            continue
        if parent.get("borderColor") == w.get("borderColor") \
                and w["h"] < parent["h"]:
            faults.append("%s %d is framed inside its parent's frame and stops "
                          "at y=%d" % (w["type"], w["id"], w["y"] + w["h"]))
    return faults


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8791)
    ap.add_argument("--binary", default="build/ae3d_editor")
    ap.add_argument("--backend", default="",
                    help="opengl or vulkan; the editor's default when unset")
    args = ap.parse_args()

    env = dict(os.environ)
    env["AETHER_UI_TEST_PORT"] = str(args.port)
    # NOT headless, though it should be: this run wants no window in front of
    # whoever is using the machine, and AETHER_UI_HEADLESS gives exactly that
    # everywhere else. Under it the editor stops answering part way through a
    # run, stuck inside a CoreAnimation layer display that never returns, with
    # vImage reporting a destination buffer too small for what it was asked to
    # composite. Same driver and same binary: headless hangs, visible passes.
    # Filed as aether-lang-dev/aether-ui#123.
    if args.backend:
        env["AE3D_EDITOR_BACKEND"] = args.backend
    # Long enough that the run outlives this script; it is killed at the end.
    env["AE3D_EDITOR_FRAMES"] = "100000"
    # Kept, not discarded. When the editor stops answering, its own output is
    # the only account of why, and this threw it away: a run that died mid-way
    # reported "never answered /widgets" and nothing else.
    editor_log = tempfile.NamedTemporaryFile(prefix="ae3d_editor_", suffix=".log",
                                             delete=False)
    EDITOR_LOG[0] = editor_log.name
    editor = subprocess.Popen([args.binary], env=env,
                              stdout=editor_log, stderr=subprocess.STDOUT)
    EDITOR_PROC[0] = editor
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
            after = wait_rows(args.port, scene, before + 1)
            widgets = tree(args.port)
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
            undone = wait_rows(args.port, scene, before)
            widgets = tree(args.port)
            check("clicking Undo takes the object back off", undone == before,
                  "%d rows, expected %d" % (undone, before))

        # A number field, typed into. The value has to reach the model, which
        # is only proved by selecting away and back: what comes back is the
        # model's own formatting, not the text that was typed.
        # Only the ones actually on screen. A section is hidden by hiding the
        # rows, and a hidden row's children still report visible along with
        # whatever geometry they last had, so the water settings sit at the top
        # of the panel by their coordinates while being nowhere on it: sorting
        # by position picked one of those to type into.
        fields = sorted([w for w in widgets.values() if w["type"] == "textfield"
                         and on_screen(widgets, w)],
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
        # The menus, and only what the driver can actually answer for.
        #
        # Their items are not activated: the driver runs a menu item's closure
        # on its own HTTP thread rather than bouncing it to the main queue the
        # way it does every widget route (aether-lang-dev/aether-ui#116), and
        # an item that adds a model touches the GL context and segfaults. Each
        # item calls the same function its button does, and the buttons are
        # pressed above.
        #
        # Nor is attachment checked, though the name of this check said so
        # until it was sabotaged: /menus reports every menu that was built,
        # with no record of which ones reached the bar, so dropping the
        # menu_bar_add for a whole menu left it green. What it does catch is an
        # action going missing from the menus, which is the regression that
        # happens when an action is added or renamed.
        menus = get(args.port, "/menus")
        listed = {}
        for menu in menus:
            for item in menu["items"]:
                listed[item.split("  ")[0]] = menu["handle"]
        wanted = ["Save Scene", "Load Scene", "Undo", "Redo", "Duplicate",
                  "Delete", "Cube", "Sphere", "Plane", "Water", "Light",
                  "Terrain",
                  "Move", "Rotate", "Scale", "Frame Selection",
                  "Fast", "Balanced", "Quality"]
        missing = [w for w in wanted if w not in listed]
        check("every action the editor has is on a menu",
              len(menus) == 4 and not missing,
              "%d menus, missing %s" % (len(menus), missing[:4]))

        # A section folds when its bar is clicked. Read as the body going away
        # and the caret turning, not as one row disappearing: a hidden row's
        # own visible flag stays true, which is why on_screen walks upward.
        widgets = tree(args.port)
        caps = [w for w in widgets.values()
                if w["type"] == "text" and w["text"].strip() == "MATERIAL"]
        check("the inspector's sections have headers", len(caps) == 1)
        if caps:
            strip = widgets[caps[0]["parent"]]
            bar = widgets[strip["parent"]]
            caret = [w for w in widgets.values()
                     if w["parent"] == strip["id"] and w["type"] == "text"
                     and w["id"] < caps[0]["id"]]
            red = [w for w in widgets.values()
                   if w["type"] == "text" and w["text"].strip() == "red"]
            check("and a caret that says which way the section is",
                  len(caret) == 1 and caret[0]["text"].strip() == "\u25be",
                  repr(caret[0]["text"]) if caret else "none")
            if red and caret:
                post(args.port, "/widget/%d/click" % bar["id"])
                widgets, ok = wait_for(
                    args.port,
                    lambda ws: not on_screen(ws, ws[red[0]["id"]]))
                check("clicking a section header folds it away", ok)
                check("and the caret turns with it",
                      widgets[caret[0]["id"]]["text"].strip() == "\u25b8",
                      repr(widgets[caret[0]["id"]]["text"]))

                post(args.port, "/widget/%d/click" % bar["id"])
                widgets, ok = wait_for(
                    args.port,
                    lambda ws: on_screen(ws, ws[red[0]["id"]]))
                check("and clicking it again brings it back", ok)

        # The shading switches, pressed rather than called. The report's own
        # check flips them through set_shading; this is the half that proves a
        # switch on the screen is wired to that at all.
        widgets = tree(args.port)
        wanted = ["Clearcoat", "Sheen", "Ambient occlusion", "Volumetric light",
                  "Global illumination", "Soft shadows"]
        captions = {w["text"].strip(): w for w in widgets.values()
                    if w["type"] == "text" and w["text"].strip() in wanted}
        check("the shading section offers every feature the presets disagree on",
              len(captions) == len(wanted),
              "missing %s" % [n for n in wanted if n not in captions])
        if "Ambient occlusion" in captions:
            switch = [w for w in widgets.values()
                      if w["parent"] == captions["Ambient occlusion"]["parent"]
                      and w["type"] == "toggle"]
            check("and each has a switch", len(switch) == 1)
            if switch:
                post(args.port, "/widget/%d/click" % switch[0]["id"])
                widgets, ok = wait_for(
                    args.port,
                    lambda ws: any(w["type"] == "text"
                                   and "ambient occlusion" in w["text"]
                                   for w in ws.values()))
                check("pressing one is heard by the editor", ok)
                post(args.port, "/widget/%d/click" % switch[0]["id"])

        # A terrain is one object whose shape is a property of it, rather than
        # five buttons in the panel that each add a different thing. The panel
        # says a terrain is a thing you can have; which shape it takes is
        # chosen on the object, the way Unreal and Unity both do it.
        widgets = tree(args.port)
        add = find(widgets, "button", "Terrain")
        check("the panel offers a terrain, not a list of biomes", add is not None)
        if add is not None:
            post(args.port, "/widget/%d/click" % add)

            def terrain_shown(ws):
                caps = [w for w in ws.values() if w["type"] == "text"
                        and w["text"].strip() == "TERRAIN"]
                return bool(caps) and on_screen(ws, caps[0])

            widgets, ok = wait_for(args.port, terrain_shown)
            check("adding one shows its own section", ok)

            shapes = {n: w for n, w in
                      ((w["text"].strip(), w) for w in widgets.values()
                       if w["type"] == "button")
                      if n in ("Plains", "Hills", "Desert", "Islands", "Caves")}
            check("the section offers every shape terrain can build",
                  len(shapes) == 5, sorted(shapes))
            if len(shapes) == 5:
                lit = [n for n, w in shapes.items() if w.get("bg") == ACCENT]
                check("and says which one this terrain is", lit == ["Plains"],
                      "lit: %s" % lit)

                post(args.port, "/widget/%d/click" % shapes["Desert"]["id"])

                # The console says what was built, so this reads that the
                # terrain was regenerated rather than that a button lit up.
                widgets, ok = wait_for(
                    args.port,
                    lambda ws: any(w["type"] == "text" and "desert:" in w["text"]
                                   for w in ws.values()),
                    seconds=12.0)
                check("choosing a shape rebuilds the terrain", ok)

        # A row of choices says which one is on. Read as "the accent moved",
        # not "this button is blue": the tree reports the colour a widget was
        # given rather than the colour it has (aether-lang-dev/aether-ui#111),
        # and a reading that never changes would pass against a highlight that
        # is painted nowhere.
        widgets = tree(args.port)
        segments = {w["text"]: w for w in widgets.values()
                    if w["type"] == "button"
                    and w["text"] in ("None", "Spin", "Bob", "Orbit")}
        check("the behaviour row has all four choices", len(segments) == 4)
        if len(segments) == 4:
            lit = [t for t, w in segments.items() if w.get("bg") == ACCENT]
            check("exactly one behaviour is lit, and it is the one in effect",
                  lit == ["None"], "lit: %s" % lit)

            post(args.port, "/widget/%d/click" % segments["Spin"]["id"])

            def moved(ws):
                return [w["text"] for w in ws.values()
                        if w["type"] == "button"
                        and w["text"] in ("None", "Spin", "Bob", "Orbit")
                        and w.get("bg") == ACCENT] == ["Spin"]

            widgets, ok = wait_for(args.port, moved)
            check("choosing a behaviour moves the highlight to it", ok,
                  "lit: %s" % [w["text"] for w in widgets.values()
                               if w["type"] == "button"
                               and w["text"] in ("None", "Spin", "Bob", "Orbit")
                               and w.get("bg") == ACCENT])
            post(args.port, "/widget/%d/click" % segments["None"]["id"])
            wait_for(args.port, lambda ws: not moved(ws))

        # A bounded row is two ways into one value. Typing an exact number is
        # the half a slider cannot do, and the slider beside it has to follow,
        # or the panel shows the same setting as two different numbers.
        widgets = tree(args.port)
        caps = [w for w in widgets.values()
                if w["type"] == "text" and w["text"].strip() == "roughness"]
        check("the material rows are in the tree", len(caps) == 1)
        if caps:
            box = row_box(widgets, caps[0])
            bar = [w for w in widgets.values()
                   if w["parent"] == caps[0]["parent"] and w["type"] == "slider"]
            check("roughness has both a slider and a box",
                  len(box) == 1 and len(bar) == 1)
            if box and bar:
                post(args.port, "/widget/%d/set_text?v=0.75" % box[0]["id"])

                def slider_followed(ws):
                    return abs(ws[bar[0]["id"]]["value"] - 0.75) < 0.001

                widgets, ok = wait_for(args.port, slider_followed)
                check("typing a value moves the slider beside it", ok,
                      "slider at %s" % widgets[bar[0]["id"]]["value"])

                # And the other way: dragging writes the number in the box.
                post(args.port, "/widget/%d/set_value?v=0.20" % bar[0]["id"])
                widgets, ok = wait_for(
                    args.port,
                    lambda ws: ws[box[0]["id"]]["text"].strip() == "0.20")
                check("dragging the slider writes the box", ok,
                      repr(widgets[box[0]["id"]]["text"]))

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

        # And appears again when there is something to edit, with its sliders
        # reaching the simulation rather than only their own readouts.
        water = find(widgets, "button", "Water")
        if water is not None and captions:
            post(args.port, "/widget/%d/click" % water)

            def section_open(ws):
                caps = [w for w in ws.values() if w["type"] == "text"
                        and w["text"].strip() == "wave height"]
                if not caps:
                    return False
                frame = ws.get(caps[0]["parent"])
                return frame is not None and frame["visible"]

            widgets, ok = wait_for(args.port, section_open)
            check("adding water shows the section", ok)
            caption = [w for w in widgets.values()
                       if w["type"] == "text" and w["text"].strip() == "wave height"][0]

            readout = row_box(widgets, caption)
            later = sorted([w for w in widgets.values()
                            if w["type"] == "slider" and w["id"] > caption["parent"]],
                           key=lambda w: w["id"])
            if readout and later:
                post(args.port, "/widget/%d/set_value?v=17.5" % later[0]["id"])
                time.sleep(1.0)
                # Away and back, so what is read comes from the simulation.
                scene_rows = rows_under(tree(args.port), scene)
                scene_rows.sort(key=lambda w: w["id"])
                post(args.port, "/widget/%d/click" % scene_rows[0]["id"])
                time.sleep(0.7)
                post(args.port, "/widget/%d/click" % scene_rows[-1]["id"])
                time.sleep(0.7)
                back = tree(args.port)[readout[0]["id"]]["text"].strip()
                check("a water slider reaches the simulation", back == "17.50",
                      repr(back))

        # The same operations interleaved. A check that passes on its own can
        # fail in sequence: adding water showed its section every time until an
        # Undo had happened earlier in the run, and that was three lists going
        # out of step rather than anything about water.
        ids = {w["text"].strip(): w["id"] for w in tree(args.port).values()
               if w["type"] == "button"}
        mismatch = None
        for step, button, pause in (("add cube", "Cube", 1.2),
                                    ("delete", "Delete", 1.2),
                                    ("undo the delete", "Undo", 1.5)):
            post(args.port, "/widget/%d/click" % ids[button])
            time.sleep(pause)
            mismatch = audit_kinds(args.port, scene, step)
            if mismatch:
                break
        check("every row reports its own kind through a mixed sequence",
              mismatch is None, mismatch or "")

        faults = layout_faults(tree(args.port))
        check("the panels are laid out on one grid", not faults,
              "; ".join(faults[:3]))

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
            check("Save writes a scene file", wait_file(scene_file))

            post(args.port, "/widget/%d/click" % find(tree(args.port), "button", "Cube"))
            grew = wait_rows(args.port, scene, saved_rows + 1)
            post(args.port, "/widget/%d/click" % load)
            # Polled, not slept on. Reading a scene back off disk takes as long
            # as the machine takes, and a sleep that is long enough here is a
            # guess that fails on a busier one: this check passed alone and
            # failed inside a full ci run, which is exactly that.
            widgets, _ = wait_for(args.port,
                                  lambda ws: len(rows_under(ws, scene)) == saved_rows)
            loaded = len(rows_under(widgets, scene))
            check("Load brings back what was saved",
                  loaded == saved_rows and grew == saved_rows + 1,
                  "%d saved, %d after adding, %d after loading"
                  % (saved_rows, grew, loaded))

            # A value has to survive the file, not just the row count. It is
            # changed after saving on purpose: left alone, it would come back
            # whatever load did, and the check would only be proving that
            # memory keeps its contents.
            caps = [w for w in tree(args.port).values()
                    if w["type"] == "text" and w["text"].strip() == "wave height"]
            if caps:
                readout = row_box(tree(args.port), caps[0])
                later = sorted([w for w in tree(args.port).values()
                                if w["type"] == "slider" and w["id"] > caps[0]["parent"]],
                               key=lambda w: w["id"])
                if readout and later:
                    # Select the water first. A water row applies to whichever
                    # water is selected, and after a load the selection is
                    # elsewhere: moving the slider then updates the readout and
                    # reaches no simulation at all, which is exactly how the
                    # first version of this check fooled itself.
                    for row in sorted(rows_under(tree(args.port), scene),
                                      key=lambda w: w["id"]):
                        if row_name(tree(args.port), row) == "water":
                            post(args.port, "/widget/%d/click" % row["id"])
                            time.sleep(0.8)
                            break
                    post(args.port, "/widget/%d/set_value?v=9.25" % later[0]["id"])
                    time.sleep(0.9)
                    stamped = os.path.getmtime(scene_file)
                    post(args.port, "/widget/%d/click" % save)
                    wait_file(scene_file, newer_than=stamped)
                    post(args.port, "/widget/%d/set_value?v=3.0" % later[0]["id"])
                    time.sleep(0.9)
                    moved = tree(args.port)[readout[0]["id"]]["text"].strip()
                    post(args.port, "/widget/%d/click" % load)
                    wait_for(args.port,
                             lambda ws: len(rows_under(ws, scene)) == saved_rows)
                    water_rows = rows_under(tree(args.port), scene)
                    water_rows.sort(key=lambda w: w["id"])
                    for row in water_rows:
                        if row_name(tree(args.port), row) == "water":
                            post(args.port, "/widget/%d/click" % row["id"])
                            time.sleep(0.9)
                            break
                    restored = tree(args.port)[readout[0]["id"]]["text"].strip()
                    check("a setting survives the scene file",
                          moved == "3.00" and restored == "9.25",
                          "changed to %s, came back as %s" % (moved, restored))

        # The sky survives the file, and the renderer clears to it. The scene
        # format has always carried a sky and the editor had no control over
        # one, so every scene it saved recorded a colour nobody could choose.
        widgets = tree(args.port)
        sky = [w for w in widgets.values()
               if w["type"] == "text" and w["text"].strip() == "SKY"]
        if sky:
            bar = widgets[widgets[sky[0]["parent"]]["parent"]]
            body = sorted([w for w in widgets.values()
                           if w["parent"] == bar["parent"] and w["id"] > bar["id"]],
                          key=lambda w: w["id"])[0]
            channels = [w for r in widgets.values() if r["parent"] == body["id"]
                        for w in widgets.values()
                        if w["parent"] == r["id"] and w["type"] == "slider"]
            check("the sky has a channel for each colour", len(channels) == 3,
                  "%d" % len(channels))
            if len(channels) == 3:
                for channel in channels:
                    post(args.port, "/widget/%d/set_value?v=0.75" % channel["id"])

                def sky_at(ws, want):
                    return all(abs(ws[c["id"]]["value"] - want) < 0.01
                               for c in channels)

                widgets, ok = wait_for(args.port, lambda ws: sky_at(ws, 0.75))
                check("setting the sky moves all three channels", ok)

                stamp = os.path.getmtime(scene_file)
                post(args.port, "/widget/%d/click" % save)
                wait_file(scene_file, newer_than=stamp)
                for channel in channels:
                    post(args.port, "/widget/%d/set_value?v=0.10" % channel["id"])
                wait_for(args.port, lambda ws: sky_at(ws, 0.10))
                post(args.port, "/widget/%d/click" % load)
                widgets, back = wait_for(args.port, lambda ws: sky_at(ws, 0.75))
                check("and the sky comes back with the scene", back)

        # The post chain survives the file. It is written into the scene by
        # the same record that carries the camera, and the editor filled none
        # of it: every scene it saved recorded the defaults, so a scene saved
        # with bloom on came back with it off.
        widgets = tree(args.port)
        bloom = [w for w in widgets.values()
                 if w["type"] == "text" and w["text"].strip() == "Bloom"]
        save_btn = save
        load_btn = load
        if bloom and save_btn and load_btn:
            switch = [w for w in widgets.values()
                      if w["parent"] == bloom[0]["parent"] and w["type"] == "toggle"]
            if switch:
                def bloom_on(ws):
                    return bool(ws[switch[0]["id"]].get("active"))

                post(args.port, "/widget/%d/click" % switch[0]["id"])
                widgets, on = wait_for(args.port, bloom_on)
                check("the bloom switch turns on", on)
                written = os.path.getmtime(scene_file)
                post(args.port, "/widget/%d/click" % save_btn)
                check("the scene is written again",
                      wait_file(scene_file, newer_than=written))
                post(args.port, "/widget/%d/click" % switch[0]["id"])
                wait_for(args.port, lambda ws: not bloom_on(ws))
                post(args.port, "/widget/%d/click" % load_btn)
                widgets, back = wait_for(args.port, bloom_on)
                check("and the post chain comes back with the scene", back)

            before_delete = len(rows_under(tree(args.port), scene))
            post(args.port, "/widget/%d/click" % delete)
            after_delete = wait_rows(args.port, scene, before_delete - 1)
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
