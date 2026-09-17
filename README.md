# ae3d

**A 3D engine built to be driven by programs.** One scene API over OpenGL 4.1
and Vulkan, a Blender-to-engine asset pipeline, and a control channel through
which a program, a test, or an AI agent can build a scene, read back what was
drawn, and check the frame by number instead of by eye.

![A horde of skinned zombies shambling down a lamp-lit street at night, the wet road reflecting the lamps](docs/zombie-city.png)

*`examples/zombie_city.ae`: a street of seven blocks and a crowd of the same
skinned zombie, every one posed from a shared pose bank and drawn in two
instanced calls. Everything in the frame was modelled, textured and animated
by a script in Blender and exported through the pipeline below.*

ae3d is written in [Aether](https://github.com/aether-lang-dev/aether) with a
thin C layer for the GPU, windowing and image decoding. It continues
[Gopher3D](https://github.com/nicolas-maman/gopher3D), the same author's
earlier Go engine, rebuilt and taken past where that one stopped: a Vulkan
renderer at parity with OpenGL, skinned instanced crowds, and an engine that
can be interrogated while it runs. See [Credits](#credits).

## Highlights

- **Two renderers behind one interface.** `Backend` is a vtable both the OpenGL
  and Vulkan renderers fill in; a program picks its renderer with a constructor
  argument and nothing else changes. `tests/test_backend_parity` draws the same
  scene through both and compares them channel by channel across materials,
  textures, instancing, transparency, skybox, FXAA, bloom, shadows, multiple
  lights, a Gerstner ocean and an instanced voxel chunk. They agree to within
  0.7% of channels.
- **Skinned crowds in one draw.** A figure's walk is baked once into a pose
  bank (a texture of bone palettes); every instance carries its own phase and
  is posed from the bank in the vertex shader. A crowd of the real 23,680-
  triangle zombie is one instanced call per distance tier, and the bake is
  *in place*: the clip's travel is taken out of the poses and handed back to
  the simulation as speed, so feet plant instead of skating and nothing
  snaps at the loop.
- **A data-oriented ECS** (`ae3d.ecs`): entities as integer handles, components
  in dense columns a system walks in one pass, a crowd rendering straight from
  the position column's buffer. The native crowd step, separation grid and
  distance bucketing are C over those columns.
- **Physically based shading**: metallic/roughness materials, up to four
  directional or point lights, normal mapping, baked per-vertex occlusion,
  shadow mapping with a texel-snapped light box (both backends), fog applied
  after tone mapping, MSAA, FXAA and bloom. Screen-space reflections on wet
  surfaces on Vulkan.
- **Models compose.** A model keeps its own transform and composes it onto its
  parent's, so bones are ordinary models: a clip exported from Blender drives a
  bone exactly as it drives a part, and `ae3d.ik` solves a limb of bones
  without being told they belong to a skin.
- **Also:** Gerstner ocean, Perlin terrain, voxel worlds as a single instanced
  draw, surface nets over a signed distance field, an OBJ/MTL loader, ray
  casting, keyframe animation in glTF's shape (step, linear, cubic), scenes
  that save and load with everything attached to them, and a scene editor.

![Twenty thousand zombies filling the street from end to end, seen from above the pavement](docs/zombie-horde.png)

*`AE3D_CROWD=20000 AE3D_NEAR=28 ./build/zombie_city`: the near tier draws the
full mesh, the far tier a bone-aware decimation of it (130 triangles), and the
draw count does not change with the crowd. Twenty thousand hold ~36 fps on an
RTX 4070 Ti at 1280x720 with the GPU shared.*

## Quick start

Requirements: the [Aether toolchain](https://github.com/aether-lang-dev/aether)
(`ae`, `aetherc`) on `PATH`, a C compiler, GLFW 3, zlib, `pkg-config`, and the
Vulkan headers (the loader is opened at runtime, so a driver is optional).

```bash
brew install glfw                                          # macOS
brew install molten-vk vulkan-loader                       # macOS, to run Vulkan
sudo apt install libglfw3-dev libvulkan-dev mesa-vulkan-drivers   # Debian/Ubuntu
# Windows, from an MSYS2 UCRT64 shell (match the Aether install's C runtime):
pacman -S mingw-w64-ucrt-x86_64-{gcc,glfw,zlib,pkgconf,vulkan-headers,vulkan-loader}
```

```bash
./build.sh examples/spinning_cube.ae && ./build/spinning_cube
./build.sh examples/zombie_city.ae   && ./build/zombie_city
./build/zombie_city vulkan
```

`build.sh` compiles the native layer once, runs `aetherc` over the Aether
sources and links. Every program honours a few environment variables:

| Variable | Effect |
|---|---|
| `AE3D_FRAMES=n` | stop after `n` frames, so any example is a smoke test |
| `AE3D_SNAPSHOT=path.png` | write the last frame; `AE3D_SNAPSHOT_BURST=k` writes the last `k` |
| `AE3D_HIDDEN=1` | no window on screen (rendering still happens) |
| `AE3D_AGENT=port` | open the control channel on loopback |

`./ci.sh` builds with warnings as errors, type-checks every module, runs every
test, benchmark and example, checks each headless one for leaks, runs the
scene critique on both backends and holds the demo scene to its recorded frame
cost.

## The Blender pipeline

The zombie street is not a downloaded asset. `tools/blender/make_zombie_street.py`
builds the whole scene in a headless Blender: the terrace of buildings, the
street furniture, the road with its camber, the zombie as a skin-modifier
body over a 29-bone rig, its clothes, the textures (generated with numpy,
brick and paving and dead skin and cloth, each with a normal map), and the
walk cycle, lunge and recovery as a footstep plan solved onto the legs.
`ae3d_export.py` then writes each object's geometry, material, animation and
occlusion beside a manifest the engine loads.

```bash
blender --background --factory-startup --python tools/blender/make_zombie_street.py -- --out resources/blender/zombie_street.blend
./scripts/export_assets.sh resources/blender/zombie_street.blend resources/blender/zombie_street
```

![The hero zombie walking a night street under a lamp, its shadow on the wet road](docs/zombie-street.png)

*`examples/zombie_street.ae`: the same export as one figure, the scene the
engine is measured against. 177 objects, every surface textured to one texel
density, the figure one skinned surface with a face, and the wet road taking
the lamp.*

What makes the pipeline usable by a program rather than a person:

- **The export is deterministic.** Blender is not: regenerating a scene gives
  a different triangulation and polygon order. The exporter makes its output a
  function of the geometry (canonical diagonals and winding, sorted triangles
  and vertex tables), so two `.blend` files built from the same script export
  byte-identical assets.
- **The build says what the animation is.** Timeline markers (`gait`,
  `gait_end`, `lunge`, `recover`) go into the manifest, and a scene asks for
  them by name: the crowd loops exactly one gait cycle, which repeats without a
  seam, instead of hard-coding a frame range.
- **Textures can be looked at without Blender.**
  `py tools/blender/preview_textures.py brick out/brick.png` renders any
  generator to a PNG (within ~2% of what Blender exports), so a texture is
  tuned by looking at it and a change is verified rather than trusted.
- **Bezier easing is preserved and checked.** The exporter converts Blender's
  curves to cubic segments and records what Blender evaluated them to;
  `tests/test_assets` holds the engine to it (currently within 1.4e-4).
- **Coplanar faces are caught at export.** `tools/blender/check_coplanar.py`
  reads the exported scene back with its transforms and reports any pair of
  faces sharing a plane, which is what z-fighting is; `ci.sh` runs it on every
  exported scene.

### Holding the scene to a standard

Two scripts run on every build. `scripts/measure_scene.py` asks the engine
what it drew; `scripts/critique_scene.py` asks whether it is any good, which a
screenshot cannot answer:

```
critique_scene: the scene meets every standard
  ok   no surface is softer than a texel every four millimetres (607, wanted 256)
  ok   every surface big enough to stand next to has a normal map (0 without)
  ok   the figure carries the geometry a figure needs (27408 triangles, wanted 20000)
  ok   the street has its lamps and windows alight (88 cells over 0.55, wanted 3)
  ok   and lit in pools rather than flooded flat (7% of the frame burns that bright)
  ok   no foot sinks through the road (the lowest foot is +0.116, allowed 0.030)
  ok   a planted foot stays planted (worst 0.000 m in a frame, allowed 0.025)
  ok   the strike reaches past anything the walk does (0.138 m past the walk, wanted 0.120)
  ok   the head follows the body rather than leading it (a lag of 11 frames)
```

`tools/ae3d_bench.ae` records what a frame of the scene costs (draws,
triangles, program and material binds, GPU pass times) in
`resources/zombie_street.<backend>.budget.json`; a build that draws one more
triangle than the record fails until the record is deliberately re-taken with
`--record`.

The crowd scene logs everything it decided under `zombie_city[diag]`: the
crowd's tiers and triangle counts, the walk the bank carries and the speed it
sets, every light, the shadow and fog settings, the camera, each tile's props
and tint, each material's texture and normal map with the GPU id it resolved
to, and a per-60-frame check that no zombie ever moved further than it can
walk. The knobs it exposes (`AE3D_VIEW`, `AE3D_CAMX/Y/Z`, `AE3D_CROWD`,
`AE3D_NEAR`, `AE3D_MOON`, `AE3D_LAMP`, `AE3D_AMBIENT`, ...) are how the scene
is swept from many camera positions and lighting states, because a single
still is blind to a zombie vanishing on a zoom or a shadow sliding with the
camera.

## Driving it from a program

An engine started with `AE3D_AGENT` answers questions about itself over a
JSON protocol on loopback: read the scene, change it, seek an animation, hold
a frame still, read the pixels it produced, and trace a model from the Blender
object it was authored as to the pixels it landed on:

```bash
AE3D_AGENT=7911 ./build/zombie_street &
python3 tools/ae3d_agent.py --port 7911 trace.model object=Zombie_Body
```

```json
{ "name": "Zombie_Body", "ok": true, "stages": [
  { "stage": "source",     "detail": { "blend": "zombie_street.blend", "id": "a0612e051dc3f268" } },
  { "stage": "asset",      "detail": { "mesh_file": "Zombie_Body.obj", "exported_vertices": 11895 } },
  { "stage": "mesh",       "detail": { "triangles": 23680, "matches_export": true } },
  { "stage": "node",       "detail": { "index": 174, "visible_flag": true, "position": [0, 0, 0] } },
  { "stage": "visibility", "detail": { "in_frustum": true, "in_front_of_camera": true } },
  { "stage": "pixels",     "detail": { "region": { "x": 383, "y": 143, "width": 469, "height": 469 } } }
] }
```

Each stage is checked in order and the first one that fails is named, which
separates half a dozen bugs that otherwise share the symptom "I cannot see
it"; the last stage reads the frame rather than reasoning about it. The same protocol runs inside Blender
(`tools/blender/ae3d_agent_server.py`), so one client drives the modelling
tool and the engine. The protocol is documented in [docs/agent.md](docs/agent.md).

## Editor

![the editor viewport](docs/editor-viewport.png)

`editor/` is a scene editor whose chrome is
[aether-ui](https://github.com/aether-lang-dev/aether-ui): a hierarchy, an
asset browser, a console, an inspector that changes with the selection, a
viewport you orbit and click to select in, a transform gizmo, and undo. Objects
are meshes, water, voxel worlds or lights, each carrying a component the
inspector shows the right section for; a behaviour can be attached to any
object and runs in the frame loop. It runs on either renderer
(`AE3D_EDITOR_BACKEND=vulkan`). See [docs/editor.md](docs/editor.md).

```bash
git clone https://github.com/aether-lang-dev/aether-ui.git ../aether-ui
./editor/build_editor.sh && ./build/ae3d_editor
```

## Examples

| Example | What it shows |
|---|---|
| `spinning_cube.ae` | The smallest complete program: window, light, one model |
| `backend_switch.ae` | The same scene through either renderer, `./build/backend_switch vulkan` |
| `zombie_street.ae` | The hero scene: a skinned figure walking and attacking in a lamp-lit street, 177 objects from one `.blend` |
| `zombie_city.ae` | A city of the street's blocks and a horde of the figure, posed from a pose bank in two instanced draws; `AE3D_CROWD` sets the count |
| `zombie_horde.ae` | The crowd alone on open ground, one draw, `AE3D_CROWD=200000` |
| `blender_pipeline.ae` | A model authored and keyed in Blender, exported, loaded and played |
| `models.ae` | OBJ loading, including a multi-material model drawn as one group per material |
| `lights.ae` | Material presets cycling with the light type, bloom, transparency |
| `water.ae` | A 256x256 Gerstner ocean, 65536 vertices |
| `voxel_world.ae` | 960464 voxels of Perlin terrain, 93030 visible, one draw call |
| `smooth_terrain.ae` | The same terrain meshed with surface nets |
| `black_hole.ae` | Kerr geodesics integrated per pixel: a spinning hole, its asymmetric shadow, a lensed disc and sky; `tests/test_blackhole` measures the shadow against sqrt(27) M. [docs/black-hole.md](docs/black-hole.md) |
| `particle_disc.ae` | 200000 particles under Verlet integration, one instanced draw |
| `sand.ae` | 250000 grains falling and settling |
| `zombie_crowd.ae` | The ECS on its own: a million entities advanced in ~2.8 ms and drawn as one instanced call |

The bigger examples each push one part of the engine harder than anything
else does and have an answer of their own to check: the black hole's shadow
diameter, the street's frame budget, the crowd's step length. A regression
shows up as a number, not as a picture somebody has to notice.

## Layout

```
native/      C: window and input, OpenGL entry points, Vulkan backend, mesh
             and instance buffers, pose banks, the crowd step and separation
             grid, image decoding, the agent channel's socket
src/ae3d/    Aether modules
  core         vectors, quaternions, matrices, scene types, camera, frustum
  gl, vk       the two renderers;  shaders  the GLSL programs
  engine       window, main loop, backend selection
  skin, anim   skeletons and palettes; clips, channels, samplers, playback
  crowd        pose-bank baking, the crowd systems
  ecs          the entity store
  assets       manifests, exported clips and markers, provenance
  agent        the control channel
  loader, noise, voxel, water, scene, rendering, behaviour, raycast, ik
tools/       ae3d_agent.py (client), ae3d_bench.ae (frame budget)
  blender/     make_zombie_street.py, zombie_figure.py, zombie_street_textures.py,
               preview_textures.py, ae3d_export.py, check_coplanar.py,
               ae3d_agent_server.py
scripts/     export_assets.sh, critique_scene.py, measure_scene.py
tests/       one program per suite, each printing its own verdict
benchmarks/  per-frame cost measured without a window
examples/    runnable scenes
```

## How it is put together

- **Aether never handles float32.** A model owns a native mesh holding
  interleaved position, uv and normal data and, when instanced, a native buffer
  of per-instance matrices. Aether's `float` is a C double; the columns the
  crowd systems walk are doubles and the GPU buffers are floats, converted
  once at upload.
- **The frame's uniforms go up once per program, not once per model.** Of the
  uniforms a draw needs, all but one are the same for every model in the
  frame. Draws that share geometry and a material are merged into one
  instanced draw automatically (`core.set_draw_merging(false)` turns it off).
- **The frame allocates nothing.** `benchmarks/bench_frame.ae` runs the
  heaviest per-frame work two thousand times with no window: 578us to upload
  two hundred thousand instance matrices, under a microsecond each for
  transforms, camera, frustum and water, zero leaked bytes.
- **Only exposed voxels become instances**, and **the viewport readback is
  pipelined** (two pixel buffers, so the CPU never waits on the GPU; the editor
  runs a frame behind, anything comparing what it just drew takes the waiting
  read).
- **Vulkan links nothing.** The loader is opened at runtime, so a program built
  with the Vulkan backend still starts where no driver exists and says so.

## Credits

ae3d continues [Gopher3D](https://github.com/nicolas-maman/gopher3D) (MIT),
the same author's earlier Go engine. The architecture, the GLSL programs, the
material and lighting model and the OBJ loader's behaviour carry over from it;
the Go served as the reference and none of it was copied. The Vulkan renderer,
the agent channel, the Blender pipeline, the ECS, the pose-bank crowd and the
screen-space reflections are new here.

Built with [GLFW](https://www.glfw.org/), [Vulkan](https://www.vulkan.org/)
(via MoltenVK on macOS) and [stb_image](https://github.com/nothings/stb). See
[NOTICE](NOTICE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## License

MIT, see [LICENSE](LICENSE).
