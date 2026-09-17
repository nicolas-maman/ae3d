# ae3d

**A 3D engine built to be driven by programs.** One scene API over OpenGL 4.1
and Vulkan, a Blender-to-engine asset pipeline, and a control channel through
which a program, a test, or an AI agent can build a scene, read back what was
drawn, and check the frame by number instead of by eye.

![A horde of skinned zombies shambling down a lamp-lit street at night, the wet road reflecting the lamps](docs/zombie-city.png)

*`examples/zombie_city.ae`: a street of seven blocks and a horde of two
skinned figures, each walking its own gait, every one posed from a baked pose
bank and drawn in two instanced calls per figure. Everything in the frame was
modelled, textured and animated by a script in Blender and exported through
the pipeline below; the sky was painted by the engine.*

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
  lights, a Gerstner ocean, an instanced voxel chunk, the clouds, the
  occlusion and instances placed as points. They agree to within 0.7% of
  channels, and CI fails if the generated Vulkan shaders fall behind the GLSL
  they are made from.
- **Instances as matrices or as points.** An instanced model carries a
  matrix, a colour and a phase per instance, or -- `model_enable_point_instancing`
  -- a position, a scale, a colour and a phase in eight floats, with the
  model's own rotation and scale applied to all of them in the shader. A
  million grains of sand that all move in a frame are a 32 MB stream
  instead of an 80 MB one built matrix by matrix, on both backends.
- **Skinned crowds in one draw.** A figure's walk is baked once into a pose
  bank (a texture of bone palettes); every instance carries its own phase and
  is posed from the bank in the vertex shader. A crowd of the real 26,636-
  triangle zombie is one instanced call per distance tier, and the bake is
  *in place*: the clip's travel is taken out of the poses and handed back to
  the simulation as speed, so feet plant instead of skating and nothing
  snaps at the loop.
- **A data-oriented ECS** (`ae3d.ecs`): entities as integer handles, components
  in dense columns a system walks in one pass, a crowd rendering straight from
  the position column's buffer. The native crowd step, separation grid and
  distance bucketing are C over those columns.
- **Physically based shading**: metallic/roughness materials, up to four
  directional or point lights, normal mapping, baked per-vertex occlusion and
  screen-space ambient occlusion from the scene's depth (`engine_set_ssao`,
  both backends), shadow mapping with a texel-snapped light box (both
  backends), volumetric clouds and their shadows, fog applied after tone
  mapping, MSAA, FXAA and bloom. Screen-space reflections on wet surfaces on
  Vulkan.
- **Models compose.** A model keeps its own transform and composes it onto its
  parent's, so bones are ordinary models: a clip exported from Blender drives a
  bone exactly as it drives a part, and `ae3d.ik` solves a limb of bones
  without being told they belong to a skin.
- **Water that is water.** A Gerstner sea with deep-water dispersion, shaded
  as one physically based surface: Schlick fresnel between the body of the
  water and the reflected sky (the scene's own skybox image, where it has
  one), GGX glitter from the sun, light through the crests, whitecaps on the
  steep faces, ripples finer than the mesh from scrolling noise slopes, tone
  mapped and fogged the same way as the shore beside it. The shore itself
  comes from the scene's depth, captured after the opaque pass on both
  backends: shallows go clear over the sand and a foam line runs along the
  waterline. From underneath, the
  surface is the sky through the swell and the seabed is lit by a two-scale
  caustic web with a chromatic fringe.
- **Volumetric clouds.** A layer of cumulus marched in the sky shader over
  whatever sky is set: coverage from a drifting noise field gathered into
  banks and clearings, bodies eroded by 3D noise, lit by the scene's key
  light through a short march toward it, grey underneath and bright on
  top. The ground reads the same coverage field where the sun's ray meets
  the layer, so their shadows cross the terrain as they drift. One call,
  `engine_set_clouds(cover, wind)`, on either backend.
- **Voxel worlds as a face mesh.** Only the faces that show, each corner
  carrying the sky it can see from the three voxels that crowd it -- the
  darkening in a crevice and the light on an edge a voxel world reads by --
  with the block kinds told apart through a palette image the world registers
  in memory. Surface nets over a signed distance field for the smooth kind.
- **Images the engine paints.** The skies, the sand and its normal map, a
  voxel palette, an island's albedo baked from its own height and slope: made
  by the engine from its own noise, registered under a name any texture path
  can use, nothing downloaded and nothing written to disk that need not be.
- **Also:** Perlin terrain, an OBJ/MTL loader, ray casting, keyframe animation
  in glTF's shape (step, linear, cubic), scenes that save and load with
  everything attached to them, a scene editor, and `AE3D_API=vulkan` to run
  any program on the other renderer.

![Twenty thousand zombies filling the street from end to end, seen from above the pavement](docs/zombie-horde.png)

*`AE3D_CROWD=20000 AE3D_NEAR=28 ./build/zombie_city`: the near tier draws the
full mesh, the far tier the build's own 168-triangle stand-in, and the draw
count does not change with the crowd. Twenty thousand hold ~38 fps on an
RTX 4070 Ti at 1280x720 with the GPU shared.*

## The scenes

| | |
|---|---|
| ![The seabed under the swell: a caustic web over rippled sand and rocks, the surface seen from below](docs/caustics.png) | ![A heap of a million grains of sand in a desert, a furrow ploughed across it](docs/sand.png) |
| *`caustics`: the seabed under the swell, a diver's height off the sand* | *`sand`: a million grains on a heap you dig into, in a desert the engine painted* |
| ![A volcanic island in a sea under an afternoon sky, grass on its flanks and rock at its summit](docs/smooth-terrain.png) | ![A voxel island with a forest on it, under the sun](docs/voxel-world.png) |
| *`smooth_terrain`: a signed distance field meshed with surface nets, its albedo baked from its own slope* | *`voxel_world`: 3.9 million voxels as 259,000 faces with baked corner sky, and a forest* |
| ![Five spheres on a floor under a night sky, one shadow each](docs/materials.png) | ![A Kerr black hole: its asymmetric shadow, a lensed disc and a lensed sky](docs/black-hole.png) |
| *`lights`: the material presets, on a floor, under the painted night* | *`black_hole`: Kerr geodesics per pixel, the shadow checked against sqrt(27) M* |

Every scene is verified the same way the engine is: from a sweep of camera
positions and by numbers read back over the channel, not from one still.

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
AE3D_API=vulkan ./build/zombie_city
```

`build.sh` compiles the native layer once, runs `aetherc` over the Aether
sources and links. Every program honours a few environment variables:

| Variable | Effect |
|---|---|
| `AE3D_FRAMES=n` | stop after `n` frames, so any example is a smoke test |
| `AE3D_SNAPSHOT=path.png` | write the last frame; `AE3D_SNAPSHOT_BURST=k` writes the last `k` |
| `AE3D_HIDDEN=1` | no window on screen (rendering still happens) |
| `AE3D_API=vulkan` | run through the other renderer, whatever the program asked for (`opengl` the other way) |
| `AE3D_AGENT=port` | open the control channel on loopback |

`./ci.sh` builds with warnings as errors, type-checks every module, runs every
test, benchmark and example, checks each headless one for leaks, runs the
scene critique on both backends and holds the demo scene to its recorded frame
cost.

## The Blender pipeline

The zombie street is not a downloaded asset. `tools/blender/make_zombie_street.py`
builds the whole scene in a headless Blender: the terrace of buildings, the
street furniture, the road with its camber, the zombie as a skin-modifier
body over a 24-bone rig with a sculpted face, its clothes, the textures (generated with numpy,
brick and paving and dead skin and cloth, each with a normal map), and the
walk cycle, lunge and recovery as a footstep plan solved onto the legs.
`ae3d_export.py` then writes each object's geometry, material, animation and
occlusion beside a manifest the engine loads.

```bash
blender --background --factory-startup --python tools/blender/make_zombie_street.py -- --out resources/blender/zombie_street.blend
./scripts/export_assets.sh resources/blender/zombie_street.blend resources/blender/zombie_street
```

![The hero zombie walking a night street under a lamp, its shadow on the wet road](docs/zombie-street.png)

*`tools/zombie_street.ae`: the same export as one figure, the rig the
engine is measured on. 225 objects, every surface textured to one texel
density, the figure one skinned surface with a face, and the wet road taking
the lamp.*

![The same street through Vulkan: the wet road mirrors the lit windows, the lamp and the figure](docs/zombie-street-vulkan.png)

*The same rig through Vulkan, where the wet road is a screen-space reflection
of what is drawn: the windows, the lamp and the figure, mirrored. The march has
a thickness, so a figure reflects as a figure and not as a stripe; the critique
measures that the rows under its feet hold the feet mirrored, not the torso
smeared.*

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
- **The engine paints its own skies and ground.** `tools/make_sky.ae` writes
  the desert's afternoon sky, the city's night sky (moon, stars, the town's
  glow at the horizon) and a tiling sand texture from the engine's own noise
  through its own PNG writer -- no downloads, no second language.
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

`scripts/contact_sheet.sh <scene> out.png` renders a scene's default view, a
low grazing view and a view toward the sun, on both backends, and lays the
six frames out as one picture (`tools/montage.ae`). One camera flatters a
scene: a sea that reads as water from the shore reads as stripes from a low
one, and a cloud that reads as a cloud looking up reads as a die looking
toward the sun. The sheet is what a look is judged from; a detail on it is
judged at full size with `tools/crop.ae` (a region of a frame, by pixel),
and in numbers with `tools/probe_image.ae` (the mean colour and greyness of
each band of a frame, and what moved between two).

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
viewport you orbit and click to select in, a transform gizmo, a sculpting
brush that raises, lowers and smooths a terrain under the cursor, and undo.
Objects are meshes, water, voxel worlds or lights, each carrying a component
the inspector shows the right section for; a behaviour can be attached to any
object and runs in the frame loop. It runs on either renderer
(`AE3D_EDITOR_BACKEND=vulkan`). See [docs/editor.md](docs/editor.md).

```bash
git clone https://github.com/aether-lang-dev/aether-ui.git ../aether-ui
./editor/build_editor.sh && ./build/ae3d_editor
```

## Examples

| Example | What it shows |
|---|---|
| `zombie_city.ae` | The city: seven blocks of terraces under a painted night sky, and a horde of the same skinned figure posed from a pose bank in two instanced draws; `AE3D_CROWD` sets the count |
| `caustics.ae` | The seabed under the swell: a diver's height off the sand, murk with distance, the water's light web on the sand and the rocks |
| `sand.ae` | A desert: a million grains with a tint each, falling onto a heap of sand that is itself a heightfield; hold the button and a ball under the cursor ploughs it, the sand out to a rim that slumps to its angle of repose, the grains shoved aside. Grains at rest live in a tier uploaded only when it changes, so the settled pile costs the draw and nothing else, and every grain is a point instance, so the pour streams eight floats a grain: ~85 fps with all million falling and ~140 settled on an RTX 4070 Ti (OpenGL; Vulkan close behind) |
| `black_hole.ae` | Kerr geodesics integrated per pixel: a spinning hole, its asymmetric shadow, a lensed disc and sky; `tests/test_blackhole` measures the shadow against sqrt(27) M. [docs/black-hole.md](docs/black-hole.md) |
| `voxel_world.ae` | 3.9 million voxels of Perlin terrain as an island in a sea, meshed as the 259,000 faces that show with the sky each corner sees baked in, a forest on its grass, one draw call |
| `smooth_terrain.ae` | A volcanic island meshed with surface nets, its albedo baked from its own height and slope, in a sea that mirrors the painted sky |
| `models.ae` | OBJ loading, including a multi-material model drawn as one group per material |
| `lights.ae` | Material presets cycling with the light type, bloom, transparency |
| `blender_pipeline.ae` | A model authored and keyed in Blender, exported, loaded and played |
| `backend_switch.ae` | The same scene through either renderer, `./build/backend_switch vulkan` |
| `spinning_cube.ae` | The smallest complete program: window, light, one model |

Beside them, `tools/zombie_street.ae` is the figure's measuring rig: one block
and one zombie, where the critique, the measurement and the frame budget hold
the figure and the street to their standards on every build. It is not a demo
and it is what the demo is built from.

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
