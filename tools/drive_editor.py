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
import glob
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ACCENT = "#2e6eeb"
# What a loadable library is called here, which the editor asks the loader for.
LIB_SUFFIX = {"darwin": ".dylib", "win32": ".dll"}.get(sys.platform, ".so")
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


def post(port, path, quiet=False):
    """POST to the driver. A route that answers 404 is a check that failed, not
    a run that ends: one unreachable route used to abort the script and take
    the twenty checks after it with it. `quiet` is for a probe that expects
    to miss."""
    req = urllib.request.Request("http://127.0.0.1:%d%s" % (port, path),
                                 data=b"", method="POST")
    try:
        with urllib.request.urlopen(req, timeout=3) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        if not quiet:
            print("   note  %s answered %d" % (path, e.code))
        return b""




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


def wait_rows_settled(port, scene, seconds=12.0):
    """Wait until the scene list stops changing size.

    A load rebuilds the list, and a row clicked while it is still being
    rebuilt is a row that is about to be replaced: the selection goes with it,
    and what follows asks the editor about whatever ended up selected instead.
    """
    deadline = time.time() + seconds
    count = -1
    while time.time() < deadline:
        rows = len(rows_under(tree(port), scene))
        if rows > 0 and rows == count:
            return rows
        count = rows
        time.sleep(0.2)
    return count


def wait_text(port, widget, wanted, seconds=8.0):
    """Wait until a widget spells this, and answer what it spells.

    A fixed sleep after an action is a guess about how long the editor takes to
    do it, and a guess that is usually right is the worst kind: the check
    passes on an idle machine and fails inside a full run, naming the check
    rather than the assumption.
    """
    widgets, _ = wait_for(port,
                          lambda ws: ws.get(widget, {}).get("text", "").strip() == wanted,
                          seconds)
    return widgets.get(widget, {}).get("text", "").strip()


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


def editor_binary(path):
    """The built editor, whatever the platform calls it, as an absolute path.

    Two Windows details, both of which fail as a file-not-found naming nothing
    useful: the binary is called .exe, and CreateProcess will not take a
    relative path written with forward slashes.
    """
    for candidate in (path, path + ".exe"):
        if os.path.exists(candidate):
            return os.path.abspath(candidate)
    raise SystemExit("no editor at %s: build it with editor/build_editor.sh" % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8791)
    ap.add_argument("--binary", default="build/ae3d_editor")
    ap.add_argument("--backend", default="",
                    help="opengl or vulkan; the editor's default when unset")
    args = ap.parse_args()

    env = dict(os.environ)
    env["AETHER_UI_TEST_PORT"] = str(args.port)
    # Never onto the desktop. The driver presses widgets over HTTP and reads
    # the tree back, none of which needs the window in front of whoever is
    # using the machine.
    env.setdefault("AETHER_UI_HEADLESS", "1")
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
    editor = subprocess.Popen([editor_binary(args.binary)], env=env,
                              stdout=editor_log, stderr=subprocess.STDOUT)
    EDITOR_PROC[0] = editor
    try:
        widgets = tree(args.port)

        scene = scene_list_id(widgets)
        if scene is None:
            raise SystemExit("could not find the scene list in the widget tree")

        # Wait for the scene to be there before counting it. The GPU viewport
        # builds the renderer when aether-ui hands over its context, which is
        # after the window exists, and the scene is filled then: a count taken
        # the instant the tree first answers is a count of an editor still
        # starting up.
        wait_rows_settled(args.port, scene)
        widgets = tree(args.port)
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
        # The menus. Attachment is not checked, though the name of this check
        # said so until it was sabotaged: /menus reports every menu that was
        # built, with no record of which ones reached the bar, so dropping the
        # menu_bar_add for a whole menu left it green. What it does catch is an
        # action going missing, which is the regression that happens when one
        # is added or renamed.
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

        # And a menu item does what its button does. The driver used to be
        # unable to press one: it ran the closure on its own HTTP thread rather
        # than the main queue, so an item that adds a model touched the GL
        # context off-thread and took the editor down. Fixed upstream, so the
        # menus are driven like everything else now.
        add_menu = listed.get("Cube")
        if add_menu is not None:
            before_menu = len(rows_under(tree(args.port), scene))
            post(args.port, "/menu/%d/activate?label=Cube" % add_menu)
            after_menu = wait_rows(args.port, scene, before_menu + 1)
            check("a menu item adds an object the way its button does",
                  after_menu == before_menu + 1,
                  "%d rows before, %d after" % (before_menu, after_menu))
            # Put it back with the button rather than the Undo menu item: that
            # item's label carries its accelerator, and the driver matches a
            # menu item by its exact label without decoding what the query
            # escaped, so asking for it by name is a 404.
            undo_now = find(tree(args.port), "button", "Undo")
            if undo_now is not None:
                post(args.port, "/widget/%d/click" % undo_now)
                wait_rows(args.port, scene, before_menu)

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

        # The bar under the viewport says what the last frame cost on the
        # device, pass by pass: a rate says a scene is slow, the split says
        # which pass made it so. Read as numbers, not as the presence of a
        # label a bar could carry with nothing behind it.
        def costs(ws):
            for w in ws.values():
                if w["type"] == "text" and "shadow " in w["text"] and " ms" in w["text"]:
                    return w["text"]
            return ""

        widgets, ok = wait_for(args.port, lambda ws: bool(costs(ws)), seconds=10.0)
        check("the stats bar splits the frame by pass", ok)
        if ok:
            words = costs(widgets).split()
            try:
                scene_ms = float(words[words.index("scene") + 1])
            except (ValueError, IndexError):
                scene_ms = -1.0
            check("and the scene pass has a cost", scene_ms >= 0.0, costs(widgets))

        # The shading switches, pressed rather than called. The report's own
        # check flips them through set_shading; this is the half that proves a
        # switch on the screen is wired to that at all.
        widgets = tree(args.port)
        wanted = ["Clearcoat", "Sheen", "Volumetric light",
                  "Global illumination", "Soft shadows", "Ambient occlusion"]
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
                    lambda ws: any(w["type"] == "text" and "desert " in w["text"]
                                   for w in ws.values()),
                    seconds=12.0)
                check("choosing a shape rebuilds the terrain", ok)

            # Voxels are one way of meshing a terrain, not what a terrain is.
            # The same object, the same place in the scene, meshed as a surface
            # instead of a cube per cell.
            styles = {n: w for n, w in
                      ((w["text"].strip(), w) for w in widgets.values()
                       if w["type"] == "button")
                      if n in ("Blocks", "Smooth")}
            check("a terrain can be blocks or smooth", len(styles) == 2)
            if len(styles) == 2:
                lit = [n for n, w in styles.items() if w.get("bg") == ACCENT]
                check("and starts as blocks", lit == ["Blocks"], "lit: %s" % lit)

                def triangles(ws):
                    for w in ws.values():
                        if w["type"] == "text" and "triangles" in w["text"]:
                            return int(w["text"].split()[-2])
                    return 0

                blocky = triangles(widgets)
                post(args.port, "/widget/%d/click" % styles["Smooth"]["id"])
                # The title counts the geometry the model actually holds, so
                # this reads that the mesh was replaced rather than that a
                # second button lit up.
                widgets, ok = wait_for(args.port,
                                       lambda ws: triangles(ws) > blocky * 10,
                                       seconds=20.0)
                check("smoothing one gives it a surface of its own", ok,
                      "%d triangles then %d" % (blocky, triangles(widgets)))

            # The brush. Raise is pressed, the canvas is pressed where the
            # terrain stands after framing it, and the console says how many
            # columns moved: a stroke that reached the ground and not just a
            # button that lit.
            widgets = tree(args.port)
            raise_btn = find(widgets, "button", "Raise")
            # The viewport's overlay canvas is the big one, and its size is
            # where the centre of the view is. The canvas routes take the
            # canvas's own handle, not its widget id: handles count from one
            # in the order the canvases were made, so the one with the
            # editor's hit-test closures is found by asking each in turn.
            canvas = sorted([w for w in widgets.values() if w["type"] == "canvas"],
                            key=lambda w: w["w"] * w["h"], reverse=True)
            check("the terrain section offers a brush", raise_btn is not None)
            handle = None
            for candidate in range(1, 6):
                if post(args.port, "/canvas/%d/key?name=f" % candidate, quiet=True):
                    handle = candidate
                    break
            check("the viewport takes keys over the channel", handle is not None)
            if raise_btn is not None and canvas and handle is not None:
                post(args.port, "/widget/%d/click" % raise_btn)
                cx = canvas[0]["w"] / 2
                cy = canvas[0]["h"] / 2
                post(args.port, "/canvas/%d/click?x=%d&y=%d" % (handle, cx, cy))
                post(args.port, "/canvas/%d/move?x=%d&y=%d" % (handle, cx + 12, cy + 4))
                post(args.port, "/canvas/%d/release?x=%d&y=%d" % (handle, cx + 12, cy + 4))
                def sculpted(ws):
                    for w in ws.values():
                        if w["type"] == "text" and "sculpted " in w["text"]:
                            try:
                                return int(w["text"].split("sculpted ")[1].split()[0])
                            except (IndexError, ValueError):
                                return 0
                    return 0

                widgets, ok = wait_for(args.port, lambda ws: sculpted(ws) > 0, seconds=20.0)
                check("a stroke on the terrain moves its columns", ok,
                      "sculpted %d" % sculpted(widgets))
                # A stroke is one undo step from press to release, and undoing
                # it puts the ground back: the surface's triangle count after
                # the stroke is not the count before it, and after undo it is.
                raised = triangles(widgets)
                undo_btn = find(widgets, "button", "Undo")
                if undo_btn is not None and blocky > 0:
                    post(args.port, "/widget/%d/click" % undo_btn)
                    widgets, ok = wait_for(args.port,
                                           lambda ws: triangles(ws) != raised,
                                           seconds=20.0)
                    check("undoing the stroke rebuilds the terrain as it was", ok,
                          "%d triangles after the stroke, %d after undo"
                          % (raised, triangles(widgets)))
                off_btn = find(widgets, "button", "Off")
                if off_btn is not None:
                    post(args.port, "/widget/%d/click" % off_btn)

        # Two objects selected at once is checked by the editor's own report,
        # not here: the test server clicks without modifiers and ui.modifiers()
        # reads the real keyboard, so a shift-click cannot be driven over HTTP.
        # The report calls the selection routine directly with the intent a
        # modifier would have carried, and asserts an edit reaches both.

        # A behaviour is a script the project has, not a case in the editor.
        # The buttons are the files in resources/scripts, so this reads the
        # names off disk rather than expecting any particular one: adding a
        # script is adding a file, and this check should not need editing when
        # someone does.
        scripts_dir = "resources/scripts"
        on_disk = sorted(name[:-3] for name in os.listdir(scripts_dir)
                         if name.endswith(".ae"))
        widgets = tree(args.port)
        buttons = {w["text"].strip() for w in widgets.values() if w["type"] == "button"}
        check("every script in the project has a button",
              all(name in buttons for name in on_disk),
              "on disk %s, missing %s" % (on_disk, [n for n in on_disk
                                                    if n not in buttons]))

        # A row of choices says which one is on. Read as "the accent moved"
        # rather than "this button is blue", which is the stronger claim: a
        # fixed colour is a constant the editor could satisfy while the
        # highlight never follows the choice.
        widgets = tree(args.port)
        choices = ["None"] + on_disk
        segments = {w["text"]: w for w in widgets.values()
                    if w["type"] == "button" and w["text"] in choices}
        check("the behaviour row offers none and every script",
              len(segments) == len(choices),
              "have %s, want %s" % (sorted(segments), choices))
        if len(segments) == len(choices):
            lit = [t for t, w in segments.items() if w.get("bg") == ACCENT]
            check("exactly one behaviour is lit, and it is the one in effect",
                  lit == ["None"], "lit: %s" % lit)

            wanted = on_disk[0]
            post(args.port, "/widget/%d/click" % segments[wanted]["id"])

            def moved(ws):
                return [w["text"] for w in ws.values()
                        if w["type"] == "button" and w["text"] in choices
                        and w.get("bg") == ACCENT] == [wanted]

            widgets, ok = wait_for(args.port, moved)
            check("choosing a behaviour moves the highlight to it", ok,
                  "lit: %s" % [w["text"] for w in widgets.values()
                               if w["type"] == "button" and w["text"] in choices
                               and w.get("bg") == ACCENT])

            # That the script then moves the object is not checked here. The
            # inspector shows a position and a scale, and a script is free to
            # move neither: spin only turns. The editor's own report drives
            # every script it has and asserts the model ended up somewhere
            # else, which is the check that does not depend on what a
            # particular script happens to do.

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

        # Reflectivity is a material row like roughness: what the wet road's
        # mirror is scaled by. It was settable over the channel and nowhere
        # in the panel, and a scene saved with one came back matte.
        widgets = tree(args.port)
        caps = [w for w in widgets.values()
                if w["type"] == "text" and w["text"].strip() == "reflectivity"]
        check("the reflectivity row is in the tree", len(caps) == 1)
        if caps:
            box = row_box(widgets, caps[0])
            bar = [w for w in widgets.values()
                   if w["parent"] == caps[0]["parent"] and w["type"] == "slider"]
            check("reflectivity has both a slider and a box",
                  len(box) == 1 and len(bar) == 1)
            if box and bar:
                post(args.port, "/widget/%d/set_value?v=0.60" % bar[0]["id"])
                widgets, ok = wait_for(
                    args.port,
                    lambda ws: ws[box[0]["id"]]["text"].strip() == "0.60")
                check("the reflectivity slider writes the box", ok,
                      repr(widgets[box[0]["id"]]["text"]))

        # The rest of the water and the sky's cover are rows too: the wave
        # scale and the shore were file-only knobs, the cloud cover a
        # constant behind a switch. Each is a slider with a box, and moving
        # the cover's slider writes its box like any other row's.
        widgets = tree(args.port)
        for caption in ("wave scale", "shore fade", "shore foam", "cloud cover",
                        "occlusion", "occlusion reach", "time of day"):
            caps = [w for w in widgets.values()
                    if w["type"] == "text" and w["text"].strip() == caption]
            check("the %s row is in the tree" % caption, len(caps) == 1)
            if caps and caption == "cloud cover":
                box = row_box(widgets, caps[0])
                bar = [w for w in widgets.values()
                       if w["parent"] == caps[0]["parent"] and w["type"] == "slider"]
                if box and bar:
                    post(args.port, "/widget/%d/set_value?v=0.70" % bar[0]["id"])
                    widgets, ok = wait_for(
                        args.port,
                        lambda ws: ws[box[0]["id"]]["text"].strip() == "0.70")
                    check("the cloud cover slider writes the box", ok,
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
                    # Saved once the readout says the value arrived: saving
                    # before it has is what left the file holding the number
                    # the row started at.
                    post(args.port, "/widget/%d/set_value?v=9.25" % later[0]["id"])
                    showed = wait_text(args.port, readout[0]["id"], "9.25")
                    check("the wave height takes the value it was given",
                          showed == "9.25", showed)
                    stamped = os.path.getmtime(scene_file)
                    post(args.port, "/widget/%d/click" % save)
                    wait_file(scene_file, newer_than=stamped)
                    post(args.port, "/widget/%d/set_value?v=3.0" % later[0]["id"])
                    moved = wait_text(args.port, readout[0]["id"], "3.00")
                    post(args.port, "/widget/%d/click" % load)
                    wait_for(args.port,
                             lambda ws: len(rows_under(ws, scene)) == saved_rows)
                    water_rows = rows_under(tree(args.port), scene)
                    water_rows.sort(key=lambda w: w["id"])
                    for row in water_rows:
                        if row_name(tree(args.port), row) == "water":
                            post(args.port, "/widget/%d/click" % row["id"])
                            break
                    restored = wait_text(args.port, readout[0]["id"], "9.25")
                    check("a setting survives the scene file",
                          moved == "3.00" and restored == "9.25",
                          "changed to %s, came back as %s" % (moved, restored))

        # New script writes a file that compiles. A template that does not is
        # worse than none: the first thing anyone does with it is build it, and
        # a first script is mostly a guess at what the two functions are called.
        widgets = tree(args.port)
        new_button = find(widgets, "button", "New script")
        check("the editor can write a new script", new_button is not None)
        if new_button is not None:
            before_files = set(os.listdir(scripts_dir))
            post(args.port, "/widget/%d/click" % new_button)

            def written(_ws):
                return set(os.listdir(scripts_dir)) - before_files

            widgets, ok = wait_for(args.port, written)
            made = sorted(written(None))
            check("pressing it leaves a file to edit", bool(made), str(made))
            if made:
                # Through bash by name: Windows will not execute a shell
                # script as a program, and reports it as a file that is not a
                # Win32 application, which is true and unhelpful.
                built = subprocess.run(
                    ["bash", "scripts/build_script.sh",
                     os.path.join(scripts_dir, made[0])],
                    capture_output=True, text=True)
                check("and what it wrote compiles", built.returncode == 0,
                      built.stderr.strip().splitlines()[-1:] or "")
                os.remove(os.path.join(scripts_dir, made[0]))
                for leftover in glob.glob("build/scripts/%s.*" % made[0][:-3]):
                    os.remove(leftover)

        # A script rebuilt while the editor is open is picked up without
        # restarting it. The editor compiles nothing: the same step that built
        # the script builds it again, and the editor notices the library.
        widgets = tree(args.port)
        if on_disk:
            target = os.path.join("build", "scripts", on_disk[0] + LIB_SUFFIX)
            if os.path.exists(target):
                os.utime(target, None)

                def reloaded(ws):
                    return any(w["type"] == "text" and "reloaded" in w["text"]
                               for w in ws.values())

                widgets, saw = wait_for(args.port, reloaded, seconds=15.0)
                check("a rebuilt script is picked up while the editor runs", saw)

        # A scene remembers which script each object was given, by name. That
        # is what makes an assignment worth making: the editor knows nothing
        # about what the script does, so the name is the whole of what it can
        # record, and a project with the script still in it gets it back.
        widgets = tree(args.port)
        ids = {w["text"].strip(): w["id"] for w in widgets.values()
               if w["type"] == "button"}
        if on_disk and on_disk[-1] in ids:
            chosen = on_disk[-1]
            # Which object is being given the script. A load puts the selection
            # back at the start, and the lit button is the selected object's
            # script, so without re-selecting this one afterwards the check
            # reads a different object and calls the assignment lost.
            carrier = None
            for row in sorted(rows_under(widgets, scene), key=lambda w: w["id"]):
                if row.get("classes", "").find("selected") >= 0:
                    carrier = row_name(widgets, row)
            if carrier is None:
                rows_now = sorted(rows_under(widgets, scene), key=lambda w: w["id"])
                if rows_now:
                    carrier = row_name(widgets, rows_now[-1])
                    post(args.port, "/widget/%d/click" % rows_now[-1]["id"])
            post(args.port, "/widget/%d/click" % ids[chosen])
            wait_for(args.port,
                     lambda ws: ws[ids[chosen]].get("bg") == ACCENT)
            stamped = os.path.getmtime(scene_file)
            post(args.port, "/widget/%d/click" % save)
            wait_file(scene_file, newer_than=stamped)

            with open(scene_file) as f:
                saved_scene = json.load(f)
            recorded = [m.get("script") for m in saved_scene.get("models", [])
                        if m.get("script")]
            check("the scene records the script by name", chosen in recorded,
                  "recorded %s" % recorded)

            post(args.port, "/widget/%d/click" % ids["None"])
            wait_for(args.port, lambda ws: ws[ids["None"]].get("bg") == ACCENT)
            post(args.port, "/widget/%d/click" % load)
            wait_rows_settled(args.port, scene)

            # Back to the object that was given the script, because the button
            # says what the selected object carries.
            for row in sorted(rows_under(tree(args.port), scene),
                              key=lambda w: w["id"]):
                if row_name(tree(args.port), row) == carrier:
                    post(args.port, "/widget/%d/click" % row["id"])
                    break
            widgets, back = wait_for(
                args.port, lambda ws: ws[ids[chosen]].get("bg") == ACCENT,
                seconds=12.0)
            check("and gives it back when the scene is loaded", back,
                  "on %s" % carrier)

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
            # The section also carries the hour's slider, under the sun by
            # time switch; the three colour channels are the rows above it.
            channels = [w for r in widgets.values() if r["parent"] == body["id"]
                        for w in widgets.values()
                        if w["parent"] == r["id"] and w["type"] == "slider"
                        and any(c["parent"] == r["id"] and c["type"] == "text"
                                and c["text"].strip() in ("red", "green", "blue")
                                for c in widgets.values())]
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

        # Reflections the same way: the switch is a Vulkan feature and inert
        # on OpenGL, but the scene carries it on either backend, so a scene
        # authored with it on comes back with it on wherever it is opened.
        widgets = tree(args.port)
        ssr = [w for w in widgets.values()
               if w["type"] == "text" and w["text"].strip() == "Reflections"]
        if ssr and save_btn and load_btn:
            switch = [w for w in widgets.values()
                      if w["parent"] == ssr[0]["parent"] and w["type"] == "toggle"]
            if switch:
                def ssr_on(ws):
                    return bool(ws[switch[0]["id"]].get("active"))

                post(args.port, "/widget/%d/click" % switch[0]["id"])
                widgets, on = wait_for(args.port, ssr_on)
                check("the reflections switch turns on", on)
                written = os.path.getmtime(scene_file)
                post(args.port, "/widget/%d/click" % save_btn)
                check("the scene is written with reflections on",
                      wait_file(scene_file, newer_than=written))
                with open(scene_file) as handle:
                    saved = json.load(handle)
                check("and the file says so",
                      bool(saved.get("view", {}).get("rendering", {}).get("ssr")),
                      json.dumps(saved.get("view", {}).get("rendering", {})))
                post(args.port, "/widget/%d/click" % switch[0]["id"])
                wait_for(args.port, lambda ws: not ssr_on(ws))
                post(args.port, "/widget/%d/click" % load_btn)
                widgets, back = wait_for(args.port, ssr_on)
                check("and reflections come back with the scene", back)

        # The sun by the hour: switched on, the key light's intensity row
        # follows the hour, which is read back as the row's box changing when
        # the hour is dragged from noon to dusk.
        widgets = tree(args.port)
        sun = [w for w in widgets.values()
               if w["type"] == "text" and w["text"].strip() == "Sun by time"]
        check("the sky section offers the sun by time", len(sun) == 1)
        if sun:
            switch = [w for w in widgets.values()
                      if w["parent"] == sun[0]["parent"] and w["type"] == "toggle"]
            caps = [w for w in widgets.values()
                    if w["type"] == "text" and w["text"].strip() == "time of day"]
            if switch and caps:
                post(args.port, "/widget/%d/click" % switch[0]["id"])
                widgets, on = wait_for(
                    args.port, lambda ws: bool(ws[switch[0]["id"]].get("active")))
                check("the sun by time turns on", on)
                bar = [w for w in widgets.values()
                       if w["parent"] == caps[0]["parent"] and w["type"] == "slider"]
                intensity = [w for w in widgets.values()
                             if w["type"] == "text" and w["text"].strip() == "intensity"]
                if bar and intensity:
                    box = row_box(widgets, intensity[0])
                    if box:
                        noon = box[0]["text"]
                        post(args.port, "/widget/%d/set_value?v=18.5" % bar[0]["id"])
                        widgets, ok = wait_for(
                            args.port,
                            lambda ws: ws[box[0]["id"]]["text"] != noon)
                        check("dragging the hour to dusk moves the light's intensity", ok,
                              "intensity stayed %r" % noon)
                post(args.port, "/widget/%d/click" % switch[0]["id"])
                wait_for(args.port, lambda ws: not ws[switch[0]["id"]].get("active"))

        # Clouds the same way: marched by either backend, carried by the scene.
        widgets = tree(args.port)
        clouds = [w for w in widgets.values()
                  if w["type"] == "text" and w["text"].strip() == "Clouds"]
        check("the clouds switch is in the tree", len(clouds) == 1)
        if clouds and save_btn and load_btn:
            switch = [w for w in widgets.values()
                      if w["parent"] == clouds[0]["parent"] and w["type"] == "toggle"]
            if switch:
                def clouds_on(ws):
                    return bool(ws[switch[0]["id"]].get("active"))

                post(args.port, "/widget/%d/click" % switch[0]["id"])
                widgets, on = wait_for(args.port, clouds_on)
                check("the clouds switch turns on", on)
                written = os.path.getmtime(scene_file)
                post(args.port, "/widget/%d/click" % save_btn)
                check("the scene is written with clouds on",
                      wait_file(scene_file, newer_than=written))
                with open(scene_file) as handle:
                    saved = json.load(handle)
                rendering = saved.get("view", {}).get("rendering", {})
                check("and the file says so", bool(rendering.get("clouds")),
                      json.dumps(rendering))
                check("with the cover the slider was left at",
                      abs(float(rendering.get("cloud_cover", 0.0)) - 0.70) < 0.01,
                      json.dumps(rendering))
                post(args.port, "/widget/%d/click" % switch[0]["id"])
                wait_for(args.port, lambda ws: not clouds_on(ws))
                post(args.port, "/widget/%d/click" % load_btn)
                widgets, back = wait_for(args.port, clouds_on)
                check("and clouds come back with the scene", back)

        if delete and scene:
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
