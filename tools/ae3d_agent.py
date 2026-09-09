#!/usr/bin/env python3
"""Drive an ae3d engine's agent channel from the command line.

    ./build/spinning_cube &                     # with AE3D_AGENT=auto or a port
    python3 tools/ae3d_agent.py --port 7911 scene.tree
    python3 tools/ae3d_agent.py --port 7911 trace.model object=Spinner
    python3 tools/ae3d_agent.py --port 7911 --script session.ops

An op takes key=value arguments. Values are parsed as JSON when they look like
JSON and left as strings when they do not, so `index=0` is a number, `name=cube`
is a string, and `playing=false` is a boolean without anyone having to quote
anything for a shell to eat.

Exits non-zero when the engine answers `ok: false`, so a shell script can
branch on whether the thing it asked for worked.

Also generates docs/agent.md, from the same table `help` answers with:

    ./build/agent_schema | python3 tools/ae3d_agent.py --docs > docs/agent.md

Depends on nothing outside the standard library. Blender ships a Python that
runs it, which is the one this repository already needs.
"""

import argparse
import json
import os
import socket
import sys


class Channel:
    """One connection, one request at a time, answers matched by id."""

    def __init__(self, host, port, timeout):
        self.socket = socket.create_connection((host, port), timeout)
        self.stream = self.socket.makefile("rwb")
        self.next_id = 1

    def ask(self, op, arguments):
        request = {"id": self.next_id, "op": op}
        request.update(arguments)
        self.next_id += 1
        self.stream.write((json.dumps(request) + "\n").encode("utf-8"))
        self.stream.flush()
        line = self.stream.readline()
        if not line:
            raise SystemExit("ae3d-agent: the engine closed the connection")
        return json.loads(line.decode("utf-8"))

    def close(self):
        try:
            self.stream.close()
            self.socket.close()
        except OSError:
            pass


def parse_value(text):
    """`index=0` is a number, `name=cube` is a string, `on=true` is a boolean.

    Tried as JSON first so numbers, booleans, null, arrays and objects all work;
    anything JSON rejects is a plain string, which is what a bare word is.
    """
    try:
        return json.loads(text)
    except ValueError:
        return text


def parse_arguments(pairs):
    out = {}
    for pair in pairs:
        if "=" not in pair:
            raise SystemExit("ae3d-agent: expected key=value, got %r" % pair)
        key, _, value = pair.partition("=")
        out[key] = parse_value(value)
    return out


def resolve_port(explicit):
    """--port, then AE3D_AGENT_PORT_FILE, then AE3D_AGENT.

    The port file is what an engine started with AE3D_AGENT=auto writes, which
    is the only way to find a port the system chose.
    """
    if explicit:
        return explicit
    port_file = os.environ.get("AE3D_AGENT_PORT_FILE")
    if port_file and os.path.exists(port_file):
        with open(port_file) as handle:
            return int(handle.read().strip())
    spec = os.environ.get("AE3D_AGENT", "")
    if spec.isdigit():
        return int(spec)
    raise SystemExit(
        "ae3d-agent: no port. Pass --port, or set AE3D_AGENT_PORT_FILE when the\n"
        "            engine was started with AE3D_AGENT=auto."
    )


def render_help(result):
    """`help` as something readable rather than as the JSON it arrives in.

    The summary goes on its own indented line rather than in a third column.
    One op takes `name | index, [time], [speed], [playing], [looping]`, and
    padding every other row out to that width left a screen of whitespace with
    the useful part off the right-hand edge.
    """
    lines = ["protocol: %s" % result.get("protocol", "?"), ""]
    ops = result.get("ops", [])
    width = max((len(o["op"]) for o in ops), default=0)
    for op in ops:
        signature = "  %-*s" % (width, op["op"])
        args = op.get("args", "")
        if args:
            signature += "  " + args
        lines.append(signature.rstrip())
        lines.append("      " + op.get("summary", ""))
    return "\n".join(lines)


DOC_HEADER = """# The agent channel

An ae3d program started with `AE3D_AGENT` opens a line-oriented JSON channel on
loopback. Through it an agent reads the scene, changes it, holds a frame still,
reads the pixels that frame produced, and asks why a model is not on screen.

    AE3D_AGENT=7911 ./build/spinning_cube          # a fixed port
    AE3D_AGENT=auto ./build/spinning_cube          # one the system picks

`auto` prints the port it was given and writes it to `AE3D_AGENT_PORT_FILE`
when that variable is set, which is how a script finds it.

Nothing is opened unless `AE3D_AGENT` asks for it. A run without it pays one
load of a global per frame; `tests/test_agent_cost` measures what an open
channel costs.

## The protocol

One JSON object per line, in both directions. Every answer carries back the
`id` of the request it answers, so a client may pipeline:

    {"id": 1, "op": "scene.tree"}
    {"id": 1, "ok": true, "result": {"count": 1, "models": [...]}}

A failure is an answer, not a dropped connection:

    {"id": 2, "ok": false, "error": "no model at that index"}

`tools/ae3d_agent.py` is a client:

    python3 tools/ae3d_agent.py --port 7911 scene.tree
    python3 tools/ae3d_agent.py --port 7911 model.set index=0 position='[2,0,0]'
    python3 tools/ae3d_agent.py --port 7911 trace.model object=Spinner

Arguments are `key=value`, read as JSON where they parse as JSON. The client
exits non-zero when the engine answers `ok: false`.

One client at a time. A second is refused with a message rather than
interleaved with the first.

## Ops
"""

DOC_FOOTER = """
## Frames an agent can trust

A query against a running engine races it: the answer describes whichever frame
was in flight. `frame.pause` stops that. A held frame still draws, so the scene
stays queryable and can still be captured, but time and the frame counter stop.
Read the same frame twice and the answer is the same.

    frame.pause
    frame.step count=10      -> {"stepped_from": 411, "stepped": 10}
    snapshot path=/tmp/x.png -> answered once the file exists
    frame.resume

`frame.step` pauses first for the same reason: counting frames off a running
loop measures the round trip as much as the request.

`snapshot` answers when the file is on disk, not when the request was accepted.

## Reading pixels

`frame.capture` reads the finished frame into the engine; `frame.pixel` and
`frame.region` answer questions about it. Regions are summarised where the
pixels are, so a 1280x720 rectangle costs an answer of about twenty bytes
rather than 3.7MB.

    frame.capture                          -> {"width": 1024, "height": 768}
    frame.pixel x=512 y=384                -> {"r": 133, "g": 85, "b": 70}
    frame.region x=412 y=284 width=200 height=200
                                           -> {"coverage": 0.991, "mean": [...]}

`frame.hold` keeps the current capture as a reference and `frame.diff` reports
what changed against it, which turns "did that command do anything" into a
number:

    model.set index=0 visible=false
    frame.step count=2
    frame.capture
    frame.diff  -> {"changed": 67263, "fraction": 0.086}

Capture needs a frame it can read. The OpenGL backend and the editor both
provide one; a windowed Vulkan engine has no swapchain readback and says so.

## Why a model is not on screen

`trace.model` follows one model from the Blender object it was authored as to
the pixels it produced, and names the first stage where it stopped being right:

    $ python3 tools/ae3d_agent.py --port 7911 trace.model object=Spinner
    ok=True  broke_at=-
      source      ok   {"object": "Spinner", "blend": "spin.blend"}
      asset       ok   {"exported_triangles": 12}
      mesh        ok   {"triangles": 12, "matches_export": true}
      node        ok   {"index": 0, "visible_flag": true}
      animation   ok   {"bound": true, "clip": "SpinnerAction"}
      visibility  ok   {"in_frustum": true, "on_screen": true}
      pixels      ok   {"coverage": 0.557}

The seven stages are `source`, `asset`, `mesh`, `node`, `animation`,
`visibility` and `pixels`. Visibility says a model should be on screen; pixels
says whether anything was drawn where it projects. Only the second is evidence.

A stage carries `applicable: false` where it does not apply: a cube built by
`loader.cube` has no Blender object behind it, and that is not a fault.

The trace exists to separate bugs that share one symptom. A model that cannot
be seen was exported empty, or loaded and never added, or added and hidden, or
bound to a clip with no channels, or behind the camera, or drawn at a third of
a pixel. Those are six different repairs.

## The whole scene at once

`world` returns every entity and the relations between them, so an agent does
not join `scene.tree`, `anim.list` and `trace.model` by hand:

    blend:c8eb41af  --exported_to--> asset:c8eb41af
    asset:c8eb41af  --loaded_as-->   model:0
    model:0         --has_mesh-->    mesh:0
    clip:spin       --drives-->      model:0
    camera          --sees-->        model:0

## Blender

`tools/blender/ae3d_agent_server.py` opens the same protocol inside a running
Blender, so the client above drives both:

    blender --background scene.blend --python tools/blender/ae3d_agent_server.py -- --port 7800 --serve 60

    python3 tools/ae3d_agent.py --port 7800 file.info
    python3 tools/ae3d_agent.py --port 7800 frame.set frame=13
    python3 tools/ae3d_agent.py --port 7800 object.get name=Spinner
    python3 tools/ae3d_agent.py --port 7800 export out=build/assets

Reads after `frame.set` are of the evaluated pose, so an agent can seek and
look rather than reason about what a curve would produce. `export` runs
`ae3d_export.py` against the open file in the same process.

Requests are served on Blender's own thread. `bpy` is not thread-safe, so the
reader thread only queues lines and the main thread drains them. Background
Blender runs no timers, which is what `--serve` is for.

---

This page is generated from the engine's own command table:

    ./scripts/gen_agent_docs.sh

`ci.sh` checks it is current. Do not edit it by hand.
"""


def escape_cell(text):
    return text.replace("|", "\\|")


def render_docs(schema):
    lines = [DOC_HEADER]
    lines.append("| op | arguments | what it does |")
    lines.append("|---|---|---|")
    for op in schema.get("ops", []):
        # A pipe inside a cell ends the cell, and `id | object | index` is a
        # real argument spelling. Both columns are escaped, not just the one
        # that happened to contain a pipe first.
        args = escape_cell(op.get("args", ""))
        lines.append("| `%s` | %s | %s |" % (
            op["op"],
            ("`%s`" % args) if args else "",
            escape_cell(op.get("summary", "")),
        ))
    lines.append(DOC_FOOTER)
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(prog="ae3d-agent", description=__doc__.split("\n")[0])
    parser.add_argument("--port", type=int, default=None)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--raw", action="store_true",
                        help="print the whole envelope rather than the result")
    parser.add_argument("--script", default=None,
                        help="a file of ops, one per line; - for stdin")
    parser.add_argument("--docs", action="store_true",
                        help="read a schema on stdin and write docs/agent.md to stdout")
    parser.add_argument("op", nargs="?", default=None)
    parser.add_argument("args", nargs="*", default=[])
    args = parser.parse_args(argv)

    if args.docs:
        # Written as bytes with explicit newlines. Python's text stdout
        # translates "\n" to "\r\n" on Windows, so the same generator on two
        # platforms produced two files that differed on every line -- and the
        # --check that compares a fresh render against the committed one would
        # fail on Linux for a doc generated on Windows, saying nothing about
        # what had actually changed.
        text = render_docs(json.load(sys.stdin))
        sys.stdout.buffer.write(text.encode("utf-8"))
        return 0

    if not args.op and not args.script:
        parser.error("give an op, or --script")

    channel = Channel(args.host, resolve_port(args.port), args.timeout)
    failures = 0
    try:
        requests = []
        if args.script:
            source = sys.stdin if args.script == "-" else open(args.script)
            with source if args.script != "-" else _nullcontext(source) as handle:
                for line in handle:
                    line = line.split("#", 1)[0].strip()
                    if line:
                        parts = line.split()
                        requests.append((parts[0], parse_arguments(parts[1:])))
        if args.op:
            requests.append((args.op, parse_arguments(args.args)))

        for op, arguments in requests:
            answer = channel.ask(op, arguments)
            if args.raw:
                print(json.dumps(answer, indent=2))
            elif not answer.get("ok"):
                print("%s: %s" % (op, answer.get("error", "failed")), file=sys.stderr)
                failures += 1
            elif op == "help":
                print(render_help(answer.get("result", {})))
            else:
                print(json.dumps(answer.get("result", {}), indent=2))
    finally:
        channel.close()
    return 1 if failures else 0


class _nullcontext:
    def __init__(self, value):
        self.value = value

    def __enter__(self):
        return self.value

    def __exit__(self, *exc):
        return False


if __name__ == "__main__":
    sys.exit(main())
