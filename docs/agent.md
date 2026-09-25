# The agent channel

An ae3d program started with `AE3D_AGENT` opens a line-oriented JSON channel on
loopback. Through it an agent reads the scene, changes it, holds a frame still,
reads the pixels that frame produced, and asks why a model is not on screen
or why it is dark or the wrong colour there.

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

`tools/ae3d_agent.ae` is a client, built with `./build.sh tools/ae3d_agent.ae`:

    ./build/ae3d_agent --port 7911 scene.tree
    ./build/ae3d_agent --port 7911 model.set index=0 position=[2,0,0]
    ./build/ae3d_agent --port 7911 trace.model object=Spinner

Arguments are `key=value`, read as JSON where they parse as JSON. The client
exits non-zero when the engine answers `ok: false`.

One client at a time. A second is refused with a message rather than
interleaved with the first.

## Ops

| op | arguments | what it does |
|---|---|---|
| `help` |  | This table. Every op, its arguments and what it answers. |
| `ping` |  | Liveness. Answers immediately, on the frame it was drained. |
| `frame.stats` |  | frame, fps, delta, viewport, draw calls and instances for the last frame, the mean milliseconds a frame has cost to submit, and on Vulkan the scene split by stage (stages_ms: camdepth, sky, opaque, occlusion, transparent). |
| `camera.get` |  | Camera position, orientation, field of view and clip planes. |
| `scene.tree` | `[detail], [audit]` | Every model the renderer holds: index, name, position and visibility. With detail, also its material, texture, parent, world position, triangles and where it lands on screen -- one round trip instead of one per model. With audit, what the surface is made of: its area in metres, its size along each axis, how many ways its faces point, how often its texture repeats across a metre and how many texels that is. |
| `render.set` | `[normal_strength], [occlusion_strength], [bloom_intensity], [shadows], [ssr], [ssr_road_height], [ssr_strength], [clouds], [cloud_wind], [overcast], [overcast_color]` | How hard normal maps push, how much baked occlusion is applied, how strong the bloom is, and whether shadows are cast, for the whole scene. Zero on any strength, or shadows off, is the frame this renderer draws without that -- bloom_intensity of zero is no glow, shadows false is no shadow at all -- which is what makes each one's difference measurable rather than a matter of opinion. The sky's clouds as the share of it they cover (0 none, 1 overcast with holes) with how fast they drift, and the overcast, 0..1, that pulls the sky toward a flat colour ([r, g, b], grey when not given): the sky a weather brings, set by hand so a frame under it can be compared with the clear one. |
| `scene.skeleton` | `object` | Every bone of a skinned model: name, parent, where it is in the world and how it is turned. A skinned mesh stays in its bind pose and its bones carry it, so this is the only thing that says where a figure's hand or foot actually is. Answered at the frame boundary, so it describes a pose that is finished rather than one part way through being built. |
| `scene.isolate` | `[object]` | Show only this model, or show every model again when given no name. |
| `model.get` | `index` | One model in full: transform, bounds, material and mesh counts. |
| `light.list` |  | Every light: kind, position, direction, colour, intensity and ambient. |
| `model.set` | `index, [position], [rotation], [scale], [diffuse], [metallic], [roughness], [reflectivity], [alpha], [visible], [casts_shadow], [name]` | Change a model. Only the fields present are written; answers with the model as it now is. |
| `camera.set` | `[position], [look_at], [fov], [near], [far]` | Move or reframe the camera. |
| `input.set` | `[key], [mouse], [button], [axis], [value], [down], [clear]` | Press or release a key, mouse button or pad button, hold a pad axis at a value, or clear every injection: what the engine's input reads next frame beside the real devices. |
| `input.get` |  | Every bound action with its state this frame: down, pressed, released, value. |
| `light.set` | `[index], [position], [direction], [color], [intensity], [ambient]` | Change a light. |
| `scene.save` | `path, [mesh_directory]` | Write the scene to JSON, with generated geometry beside it. |
| `scene.load` | `path` | Replace the scene with one from a file, and reframe the camera as it was saved. |
| `frame.capture` |  | Read the finished frame into the engine. Answers with its size once it is there. |
| `frame.pixel` | `x, y` | One pixel of the captured frame as r, g, b, a and hex. |
| `frame.region` | `x, y, width, height, [background], [tolerance]` | Mean colour, coverage, and the luminance spread over a rectangle. A flat surface has a stddev near zero; stipple and shadow acne do not. |
| `frame.grid` | `[x], [y], [width], [height], [columns], [rows], [background], [tolerance]` | A window of the frame as a grid of cells, each with its mean colour and how much of it is not the background; the whole frame by default. One answer instead of a round trip a pixel, which is what makes a picture something an agent can read. Given a window no larger than the grid is fine, every cell is one pixel. |
| `frame.hold` |  | Keep the captured frame as the reference frame.diff compares against. |
| `frame.diff` | `[tolerance]` | Changed pixel count, fraction and largest channel delta against the held reference. |
| `world` |  | Every entity and the relations between them: blend object, asset, model, mesh, clip, light, camera. One query instead of joining four. |
| `trace.model` | `id \| object \| index` | Follow one model from its Blender object to the pixels: source, asset, mesh, node, animation, visibility. Names the stage it stopped being right at. |
| `explain.model` | `id \| object \| index` | Why a model looks the way it does on screen: its material, its texture's mean colour, the light each light and the ambient bring to its top and to the side facing the camera, whether its centre can see the key light or which model is in the way, the exposure, and the pixels it produced beside what it would show fully lit. The verdict names the first stage that accounts for a dark or wrong-coloured look, or says it is as lit. |
| `anim.list` |  | Every animation bound to a model: clip, playhead, duration, speed and the pose it produced. |
| `anim.get` | `name \| index` | One animation in full, including the transform its playhead currently produces. |
| `anim.set` | `[name \| index], [time], [speed], [playing], [looping]` | Drive an animation, or every animation at once when no name or index is given. Setting time seeks and reposes together, so a snapshot after it shows that pose. |
| `anim.track` | `object, times` | Where every bone of a skinned model is at each of a list of animation times, in one answer. Each time is posed the way a drawn frame is -- the clips applied, then the scene's own solve on top -- and every bone's world position read, with no frame rendered between them: a path sampled a thousand times over that would otherwise be a thousand round trips. The playheads are left where they were. |
| `snapshot` | `path` | Write the next completed frame to a PNG. Answers once the file exists. |
| `frame.pause` |  | Hold the simulation still. The scene still renders and can be queried; time and the frame counter stop. |
| `frame.resume` |  | Let the simulation run again. |
| `frame.step` | `count` | Pause, run exactly count frames, pause again. Answers with stepped_from and stepped. |
| `quit` |  | Stop the engine's loop and let it shut down normally. |

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

    $ ./build/ae3d_agent --port 7911 trace.model object=Spinner
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

## Why a model is dark or the wrong colour

`explain.model` takes the same arguments and answers the question after that
one: the model is on screen, so why does it look the way it does. It walks six
stages, each with its numbers, and the verdict names the first stage whose
finding accounts for what the pixels show. This is the cube under the roof in
`tests/test_agent_explain.ae`, on Vulkan:

    $ ./build/ae3d_agent --port 7911 explain.model index=2
    {"name": "shaded", "look": "dark", "verdict": "dark: in the shadow of roof", "blamed": "shadow",
     "stages": [
      {"stage": "material", "flagged": false, "detail": {"name": "default", "diffuse": [0.7, 0.7, 0.7],
         "luminance": 0.7, "metallic": 0, "roughness": 0.9, "alpha": 1, "reflectivity": 0, "emissive": false}},
      {"stage": "texture", "flagged": false, "detail": {"path": "", "bound": false,
         "note": "no texture: the material's colour is drawn over the default white"}},
      {"stage": "light", "flagged": false, "detail": {"side_normal": [0, 0, 1],
         "lights": [
           {"index": 0, "name": "sun", "mode": "directional", "radiance": 1.386, "onto_top": 0.346, "onto_side": 0.26},
           {"index": 1, "name": "lamp", "mode": "point", "distance": 23.324, "reach": 16, "onto_top": 0, "onto_side": 0,
            "note": "out of reach: 23.324 m away, its light stops at 16 m"}],
         "direct_top": [0.375, 0.342, 0.3], "direct_side": [0.281, 0.257, 0.225], "ambient": [0.08, 0.073, 0.064],
         "direct_top_luminance": 0.346, "direct_side_luminance": 0.26, "ambient_luminance": 0.074}},
      {"stage": "shadow", "flagged": true, "finding": "in the shadow of roof", "detail": {"shadows": true,
         "key_light": "sun", "centre_blocked_by": "roof", "top_blocked_by": "roof"}},
      {"stage": "exposure", "flagged": false, "detail": {"material": 1, "frame": 1, "eye_adaptation": false, "total": 1}},
      {"stage": "pixels", "flagged": false, "detail": {"region": {"x": 134, "y": 94, "width": 50, "height": 50},
         "coverage": 1, "centre": {"x": 151, "y": 111, "width": 17, "height": 17},
         "mean": [0.314, 0.294, 0.271], "luminance": 0.297,
         "predicted": [0.642, 0.618, 0.581], "predicted_luminance": 0.621}}]}

The stages:

- `material`: the linear albedo, metallic, roughness, alpha and reflectivity.
  Flagged when the albedo's luminance is under 0.03, the alpha under 0.1, or
  the metallic 0.9 or more (a metal has no diffuse colour and shows only what
  it reflects).
- `texture`: whether one is bound and whether it loaded (one that did not is
  drawn as the default white, and says so), and its mean colour as the shader
  reads it, from up to 64 by 64 of its texels read again from its file; `mean`
  is `unknown` when the file does not decode. Flagged when the mean is near
  black, or leans to a colour the material does not.
- `light`: every light at the model's centre, onto its top and onto the
  surface the camera sees there (`side_normal`, the face a ray from the eye
  through the centre meets first), as it multiplies the albedo: a light's
  colour by its temperature, a point light's fall-off, the reach past which
  the shader skips it, a spot light's cone, and the ambient the key light
  brings. A light that gives nothing says why: out of reach, outside its
  cone, behind the model, below the horizon. Flagged when no direct light
  reaches the side the camera sees.
- `shadow`: whether the model's centre and top can see the key light, by a
  ray toward it against every other model's triangles; `centre_blocked_by`
  names the model in the way. Flagged when the centre is blocked and there
  was direct light for the shadow to take.
- `exposure`: the material's, the frame's (the eye adaptation's), and their
  product. Flagged under 0.6.
- `pixels`: the coverage where the model projects, the mean display colour
  about its centre, and `predicted`, what that side would show with every
  light reaching it and no shadow: the lit colour through the tone curve.

The look is read from the pixels: `dark` under a display luminance of 0.15 or
under 0.6 of the prediction, `wrong colour` when the mean leans to a colour the
material does not, `missing` when nothing was drawn (and `trace.model` is the
next question), otherwise `as lit`. The verdict is the first flagged stage
that accounts for that look. A look no stage accounts for says so with the
two luminances; a finding the pixels do not show is named after `as lit` --
on OpenGL the same cube reads `as lit; not shown in the pixels: in the shadow
of roof`, because that renderer drew no shadow on it.

## The whole scene at once

`world` returns every entity and the relations between them, so an agent does
not join `scene.tree`, `anim.list` and `trace.model` by hand:

    blend:c8eb41af  --exported_to--> asset:c8eb41af
    asset:c8eb41af  --loaded_as-->   model:0
    model:0         --has_mesh-->    mesh:0
    clip:spin       --drives-->      model:0
    camera          --sees-->        model:0

## Recording a session and replaying it

`AE3D_AGENT_RECORD=path` writes the whole session to a file as it happens:
every request as the frame takes it and every answer as it goes, one line
each. A line is `in` or `out`, the milliseconds since the channel opened, and
the line itself. Each is flushed as it is written, so a session the program
died in is on disk up to its last word:

    in 1438 {"op":"scene.tree","id":1}
    out 1439 {"id":1,"ok":true,"result":{"count":1,"models":[{"index":0,"name":"probe","position":[0,0,0],"visible":true}]}}

`tools/agent_replay.ae` asks a running program the same questions in order
and compares every answer with the recorded one (`ae3d.replay`). A change
that alters what the pipeline sees is found by replaying a session that went
right:

    AE3D_AGENT_RECORD=build/scratch/good.log AE3D_AGENT=7920 ./build/zombie_street
    AE3D_AGENT=7921 ./build/zombie_street &        # after the change
    ./build/agent_replay build/scratch/good.log --port 7921

How answers are compared:
- **Numbers** match within a tolerance: relative, a part in a million by
  default (`--tolerance`).
- **Everything else** must match exactly: strings, flags, nulls, array
  lengths and the set of keys.
- **Keys left out:** the request id and the clocks (`ms`, `fps`,
  `frame_ms`, `elapsed_ms`), plus any key given with `--ignore`.
- **Requests and answers are paired by id**, because an answer read from a
  later frame comes out after requests that arrived behind it.

Every difference is printed with its place in the answer, up to the first 64:

    #0 scene.tree.result.models[0].position[0]: 0 then, 5 now

The tool exits 0 when every answer is the same, 1 when one differs or goes
unanswered, and 2 when it cannot start.

`tests/test_agent_record.ae` records a session against one scene and replays
it twice:
- **On the same scene:** 4 of 4 answers are the same.
- **With the cube started 5 m along x:** the two answers that say where it
  started differ, and the two after the write that moves it agree.

## Blender

`tools/blender/ae3d_agent_server.py` opens the same protocol inside a running
Blender, so the client above drives both:

    blender --background scene.blend --python tools/blender/ae3d_agent_server.py -- --port 7800 --serve 60

    ./build/ae3d_agent --port 7800 file.info
    ./build/ae3d_agent --port 7800 frame.set frame=13
    ./build/ae3d_agent --port 7800 object.get name=Spinner
    ./build/ae3d_agent --port 7800 export out=build/assets

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
