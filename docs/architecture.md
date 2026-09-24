# Architecture

How the engine is put together: what a program is, what a frame is, which
module does what, and where the line between Aether and C now runs.

## The shape of a program

A program is written the way a Unity or gopher3D game is. Game objects
live in the engine's scene; scripts on them have Unity's phases under
Unity's names; the engine calls the phases and the program never calls
one itself.

```aether
struct Spinner { speed: float }
start(state: ptr, go: *GameObject) { ... }                  // once, the first frame
update(state: ptr, go: *GameObject, delta: float) { ... }   // every frame

e = engine.engine_new()
cube = engine.object(e, "Cube", loader.cube(1.0))          // a game object with a mesh
engine.script(cube, "Spinner", &spinner, start, update)      // AddComponent
engine.engine_run(e)
```

`engine.object` puts an object in the scene and its model in the renderer;
`engine.script` puts a component on it. A component is a vtable of phases
over a state pointer, which is how Aether says what Go said with an
interface and Unity with a class. A game object keeps no transform of its
own: it points at the model that already owns one, so nothing is copied
between two positions every frame.

Underneath the scene is a *behaviour*: the same phases with the engine
handed to each, which is what the engine's own systems are written as. The
weather, the physics world, the crowd and the scene of game objects are
behaviours on the engine, added and removed like any other.

## A frame

`engine_run` owns the loop. Each frame, in order:

1. **Input.** The window's events are polled and `ae3d.input` resolves the
   actions and axes a program bound (`pressed`, `held`, `axis`) before any
   script asks for them. The camera's own controls are actions too, so a
   gamepad flies every example.
2. **`fixed_update`** as many times as the frame's time covers at the fixed
   step (1/60 s by default, `engine_set_fixed_step`). Simulation goes here:
   the physics world steps, the horde separates and moves, the weather's
   particles fall.
3. **The clips advance** by the frame's delta, then **`update`** runs once
   with it: game logic, cameras, anything that reads the simulation.
4. **The clips are applied** to their skins, then **`pose`** runs:
   inverse kinematics and whatever else puts a bone where the frame needs
   it rather than where the clip left it. Both run on a held frame too,
   so an agent looking at a pose looks at a solved one.
5. **The draw.** The backend renders the scene (below).
6. **`late_update`** after the draw, for anything that wants the picture:
   reading the frame back, recording, the agent channel's snapshot.

A phase left null is not taken part in. `AE3D_FRAMES=n` stops the loop
after `n` frames, which is what makes every example a smoke test.

## The renderers

`ae3d.core` defines the scene types -- models, meshes, materials, lights,
the camera -- and a `Backend` vtable. Two backends implement it:

- **`ae3d.vulkan`** (Vulkan, the default), the renderer the engine is built
  toward: shadow pass, camera-depth prepass, sky, opaque, occlusion,
  transparent and post, with ray queries where the device has them, the
  crowd sorted by compute and drawn through indirect commands, DLSS
  through Streamline.
- **`ae3d.gl`** (OpenGL 4.1 core), kept at parity: `tests/test_backend_parity`
  draws the same scene through both and holds them to 0.7% of channels.

Which one runs is a constructor argument (`engine_new_with`) or
`AE3D_API=opengl`; nothing else in a program changes. The GLSL lives in
`ae3d.shaders`, and `tools/generate_shaders.ae` derives the Vulkan
SPIR-V and the uniform block layout from it, so one source feeds both
(`ci.sh` fails if the derived files are stale). DirectX 12 and Metal are on
the roadmap as further implementations of the same vtable
([#311](https://github.com/nicolas-maman/ae3d/issues/311),
[#312](https://github.com/nicolas-maman/ae3d/issues/312)). What each pass
does and why is [rendering.md](rendering.md).

## The job pool

`ae3d.jobs` is one scheduler for everything a frame spreads over the
cores: the horde's passes, the weather's step, the flow field's steer and
the physics world's stages. The scheduler is aephysics's (Box3D's
`scheduler.c` in Aether: threads made once, waiting on a semaphore, tasks
claimed by compare-and-swap, the calling thread helping while it waits),
and the physics world is lent it rather than making its own, so the engine
has one set of worker threads and not one per subsystem competing for the
same cores. `AE3D_JOBS=n` sets the count; the default is the machine's.

A pass is `jobs.parallel_for(body, count, grain, context)`: blocks of at
least `grain` items claimed by workers and the calling thread alike, the
body told which worker it is on so per-worker scratch needs no lock.
`parallel_for_fixed` cuts the range into a fixed number of runs for passes
that index a slot by their run (the tier sort). Every pass is written so
that one thread and many give the same answer; the tests run at both.

## The modules

One directory a module under `src/ae3d/`, each with a header comment that
says what it is for and why it is shaped as it is.

| Area | Modules |
|---|---|
| Foundation | `core` (linear algebra, scene types, camera, the backend vtable), `platform` (the window, input and timing, over GLFW called directly), `engine` (the loop, behaviours, the window), `behaviour` (game objects and components), `input` (actions and axes), `jobs` (the pool) |
| Rendering | `vulkan`, `gl`, `vkmeter` (the frame's light, on contrib.vulkan.vk), `shaders` (the GLSL), `vkscene` (generated), `rendering` (shading presets), `offscreen` (a frame to a buffer), `sky` (the sun by the hour), `cloudnoise` (the clouds' textures), `water` (a Gerstner sea), `weather` (rain, snow, dust, storm) |
| Geometry and assets | `geometry` (the mesh and instance stores the renderers read), `posing` (bone palettes, pose banks), `loader` (OBJ, primitives), `gltf`, `assets` (the Blender export), `blob` (a file as bytes), `png` (a frame as a file), `noise`, `voxel`, `terrain`, `raycast`, `skin`, `anim`, `ik` |
| Simulation | `physics` (aephysics in the loop), `crowd` (pose banks, the device crowd), `horde` (the crowd's kernels), `nav` (the flow field), `ecs` (dense columns for crowds too large to be objects) |
| Tooling | `agent` (the channel's requests), `channel` (its socket and thread), `probe` (the channel, asking side), `script` (a script as a shared library), `scene` (scene files), `history` (undo) |

## What is still C, and why

The engine is written in Aether. `native/` holds the C that remains, by
role, and each folder is there for one reason, stated in
[native/README.md](../native/README.md):

| Folder | Why |
|---|---|
| `gpu/` | the two renderers, their offscreen targets and frame readback, still C while they move to Aether in slices (#398). The stores they draw from are already Aether's (`ae3d.geometry`), read in place through `gpu/stores.h`, whose layout `tests/test_geometry` holds to the Aether structs. Aether's 32-bit float ([aether#2134](https://github.com/aether-lang-dev/aether/issues/2134)) is what made that possible. |
| `image/` | a third-party decoder (stb_image); a decoder of our own is a project of its own |
| `platform/` | the crash handler (a signal handler may call only what is async-signal-safe, and it is installed when the library loads), and the Objective-C surface MoltenVK draws into on macOS |
| `dlss/` | the Streamline SDK's interface is C++ |

What was C for any other reason has moved to Aether on this branch, each
port measured against the C it replaced in the same run on the same
machine: the crowd's kernels (`ae3d.horde`: half a million separated in
51.4 ms against the C's 51.5), the flow field (`ae3d.nav`: the flood 8.2 ms
against 8.3), the weather's particles, the clouds' noise (the weather map
byte for byte the same, the shape within one count in 22 texels of a
million), the PNG writer, the file reader, the script loader, the
channel's asking side (a request and its answer 359 ms against 356) and
the window, input and timing layer, which calls GLFW itself.

`native/ae3d.h` is the C API the Aether modules bind through `extern`;
`native/internal.h` is what the C files share with each other. Every C
file compiles under `-Wall -Wextra -Werror` on all three platforms, and
`ci.sh` compiles each one alone to prove it.

## Dependencies

- [Aether](https://github.com/aether-lang-dev/aether), the language and
  its standard library (`std.list`, `std.heap`, `std.tcp`, `std.zlib`,
  `std.dl`, `std.worker`, ...).
- [aephysics](https://github.com/aether-lang-dev/aephysics), the physics
  engine, as the submodule `deps/aephysics`; its scheduler is the engine's
  pool.
- GLFW for the window, the Vulkan loader at run time, zlib through
  `std.zlib`, stb_image vendored in `native/image/`; Streamline when DLSS
  is built in. The editor adds [aether-ui](https://github.com/aether-lang-dev/aether-ui).

## Where a design decision lives

The reasoning is kept next to the thing it explains: a module's header
comment says why the module has the shape it has; a feature's page in
[rendering.md](rendering.md) says what it costs and what it was measured
against; a pull request's description says what changed and what proved
it. When a decision is about the language rather than the engine, it is in
[writing-aether.md](writing-aether.md).
