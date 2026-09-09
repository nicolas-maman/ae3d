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

An ae3d program with `AE3D_AGENT` set opens a line-oriented JSON channel on
loopback, and an agent drives the engine through it: query the scene, seek an
animation, hold a frame still, take a snapshot of a known frame, or ask why a
model is not on screen.

    AE3D_AGENT=7911 ./build/spinning_cube          # a fixed port
    AE3D_AGENT=auto ./build/spinning_cube          # one the system picks

`auto` prints the port it was given and writes it to `AE3D_AGENT_PORT_FILE`
when that is set, which is how a script finds it.

Nothing is open unless `AE3D_AGENT` asks for it. A build without it pays one
load of a global per frame; `tests/test_agent_cost` measures the rest.

## Talking to it

One JSON object per line, in and out. Every answer carries back the `id` of the
request it answers, so a client may pipeline:

    {"id": 1, "op": "scene.tree"}
    {"id": 1, "ok": true, "result": {"count": 1, "models": [...]}}

A failure is an answer too, never a dropped connection:

    {"id": 2, "ok": false, "error": "no model at that index"}

There is a client in `tools/ae3d_agent.py`:

    python3 tools/ae3d_agent.py --port 7911 scene.tree
    python3 tools/ae3d_agent.py --port 7911 trace.model object=Spinner
    python3 tools/ae3d_agent.py --port 7911 anim.set name=spin time=0.5 playing=false

One client at a time. A second is told so rather than silently interleaved with
the first.

## Ops
"""

DOC_FOOTER = """
## Frames an agent can trust

A query against a free-running engine races it: the answer describes whichever
frame happened to be in flight. `frame.pause` stops that. A held frame still
draws, so the scene stays queryable and snapshottable, but time and the frame
counter stop -- read the same frame twice and get the same answer.

    frame.pause
    frame.step count=10      -> {"stepped_from": 411, "stepped": 10}
    snapshot path=/tmp/x.png -> answered once the file exists
    frame.resume

`frame.step` pauses first for the same reason. Counting frames off a loop that
is still running measures the round trip as much as the request.

`snapshot` answers when the file is on disk, not when the request was accepted.
An `ok` before that would be a claim about a file the client would then fail to
open.

## Finding out why something is not on screen

`trace.model` follows one model through every stage between the Blender object
it was authored as and the pixels it should have produced, and names the first
stage where it stopped being right:

    $ python3 tools/ae3d_agent.py --port 7911 trace.model object=Spinner
    ok: True   broke_at: -
      source      n/a  no manifest entry, so it has no Blender source
      asset       n/a  no exported files to check
      mesh        ok   {"triangles": 12}
      node        ok   {"index": 0, "visible_flag": true}
      animation   ok   {"bound": false}
      visibility  ok   {"in_frustum": true, "on_screen": true}

The six stages are `source`, `asset`, `mesh`, `node`, `animation` and
`visibility`. A stage carries `applicable: false` where it does not apply --
a cube built by `loader.cube` has no Blender object behind it and never will,
and that is not a fault.

The distinction the trace exists to make is between bugs that share one
symptom. A model that is not visible was exported empty, or loaded and never
added, or added and hidden, or behind the camera, or on screen and a third of
a pixel across. Those are five different things to fix.

---

Generated from the engine's own command table by

    ./build/agent_schema | python3 tools/ae3d_agent.py --docs > docs/agent.md

so this page and the `help` an engine answers with cannot describe different
engines. Do not edit it by hand.
"""


def escape_cell(text):
    return text.replace("|", "\\|")


def render_docs(schema):
    lines = [DOC_HEADER]
    lines.append("")
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
