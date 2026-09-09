# The agent channel

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


| op | arguments | what it does |
|---|---|---|
| `help` |  | This table. Every op, its arguments and what it answers. |
| `ping` |  | Liveness. Answers immediately, on the frame it was drained. |
| `frame.stats` |  | frame, fps, delta, viewport, draw calls and instances for the last frame. |
| `camera.get` |  | Camera position, orientation, field of view and clip planes. |
| `scene.tree` |  | Every model the renderer holds: index, name, position and visibility. |
| `model.get` | `index` | One model in full: transform, bounds, material and mesh counts. |
| `light.list` |  | Every light: kind, position, direction, colour, intensity and ambient. |
| `model.set` | `index, [position], [rotation], [scale], [diffuse], [metallic], [roughness], [alpha], [visible], [casts_shadow], [name]` | Change a model. Only the fields present are written; answers with the model as it now is. |
| `camera.set` | `[position], [look_at], [fov], [near], [far]` | Move or reframe the camera. |
| `light.set` | `[index], [position], [direction], [color], [intensity], [ambient]` | Change a light. |
| `scene.save` | `path, [mesh_directory]` | Write the scene to JSON, with generated geometry beside it. |
| `scene.load` | `path` | Replace the scene with one from a file, and reframe the camera as it was saved. |
| `frame.capture` |  | Read the finished frame into the engine. Answers with its size once it is there. |
| `frame.pixel` | `x, y` | One pixel of the captured frame as r, g, b, a and hex. |
| `frame.region` | `x, y, width, height, [background], [tolerance]` | Mean colour and coverage over a rectangle, summarised in the engine rather than shipped as pixels. |
| `frame.hold` |  | Keep the captured frame as the reference frame.diff compares against. |
| `frame.diff` | `[tolerance]` | Changed pixel count, fraction and largest channel delta against the held reference. |
| `world` |  | Every entity and the relations between them: blend object, asset, model, mesh, clip, light, camera. One query instead of joining four. |
| `trace.model` | `id \| object \| index` | Follow one model from its Blender object to the pixels: source, asset, mesh, node, animation, visibility. Names the stage it stopped being right at. |
| `anim.list` |  | Every animation bound to a model: clip, playhead, duration, speed and the pose it produced. |
| `anim.get` | `name \| index` | One animation in full, including the transform its playhead currently produces. |
| `anim.set` | `name \| index, [time], [speed], [playing], [looping]` | Drive an animation. Setting time seeks and reposes at once, so a snapshot after it shows that pose. |
| `snapshot` | `path` | Write the next completed frame to a PNG. Answers once the file exists. |
| `frame.pause` |  | Hold the simulation still. The scene still renders and can be queried; time and the frame counter stop. |
| `frame.resume` |  | Let the simulation run again. |
| `frame.step` | `count` | Pause, run exactly count frames, pause again. Answers with stepped_from and stepped. |
| `quit` |  | Stop the engine's loop and let it shut down normally. |

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
