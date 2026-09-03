# aether3d

A 3D rendering engine in [Aether](https://github.com/aether-lang-dev/aether), with a
switchable OpenGL 4.1 / Vulkan backend.

This is a port of [Gopher3D](https://github.com/nicolas-maman/gopher3D), a Go
engine, to Aether and C. See [Credits](#credits).

## What it does

- **Two renderers behind one interface.** `Backend` is a vtable of function
  pointers that both the OpenGL and Vulkan renderers fill in, so a program picks
  its renderer with a constructor argument and nothing else changes.
- **OpenGL 4.1 core**, the highest version macOS offers and enough everywhere
  else: PBR materials, directional and point lights, instanced rendering,
  frustum culling, a separate transparent pass, MSAA, FXAA and bloom.
- **Vulkan**, windowed and offscreen, on a loader opened at runtime. Nothing
  links against Vulkan, so a program built with this backend still starts where
  no driver exists and says so. It runs the same feature set as OpenGL, and
  `tests/test_backend_parity` proves it: the same scene through both renderers,
  compared channel by channel across materials and textures, instancing and
  transparency, the skybox, FXAA and bloom. The two agree to within 1.4% of
  channels.
- **Gerstner-wave ocean**, **Perlin terrain**, **voxel worlds** drawn as a single
  instanced call, **surface nets** over a signed distance field, an OBJ/MTL
  loader, ray casting, and a component system.
- **Scenes save and load.** Transforms and materials as JSON; geometry that came
  from a file records its path, and geometry that did not is written to a
  compressed binary mesh beside the scene.

## Editor

![the editor viewport](docs/editor-viewport.png)

`editor/` is a scene editor whose chrome is [aether-ui](https://github.com/aether-lang-dev/aether-ui).
It has a scene hierarchy, an asset browser over the meshes in `resources/`, a
console, an inspector that changes with what is selected, and a viewport you
orbit with the mouse and click to select objects in. A transform gizmo moves,
rotates and scales the selection along an axis; edits are undoable; and a
property change repaints the viewport as you drag rather than when you let go.

Objects can be meshes, water, voxel worlds or lights. Each carries a component
recording what it is, and the inspector shows the section that belongs to it:
wave height and speed for water, colour and intensity for a light, field of view
and clip planes for the camera. A behaviour can be attached to any object and
runs in the frame loop.

See [docs/editor.md](docs/editor.md) for the controls.

aether-ui owns the real window and every widget. The viewport is a GPU render:
the scene is drawn into a framebuffer object that has no window of its own, read
back, and blitted into an aether-ui canvas each frame. That is what lets a native
toolkit with no GPU surface host a 3D view
([aether-ui#92](https://github.com/aether-lang-dev/aether-ui/issues/92)); when a
GPU surface exists, the readback is the only part that goes away.

```bash
git clone https://github.com/aether-lang-dev/aether-ui.git ../aether-ui
./editor/build_editor.sh
./build/aether3d_editor
```

Picking casts the cursor ray against every model's exact triangles, rejecting
each against its bounding sphere first, so selection stays cheap with a full
scene.

## Requirements

- The [Aether toolchain](https://github.com/aether-lang-dev/aether) on `PATH`
  (`ae` and `aetherc`).
- GLFW 3.
- A C compiler.
- For the Vulkan backend: a Vulkan loader and driver. On macOS that is MoltenVK.
  Neither is needed to build.

```bash
brew install glfw                      # macOS
brew install molten-vk vulkan-loader   # macOS, optional, for the Vulkan backend

sudo apt install libglfw3-dev          # Debian and Ubuntu
sudo apt install libvulkan-dev mesa-vulkan-drivers   # optional
```

## Build and run

```bash
./build.sh examples/spinning_cube.ae
./build/spinning_cube
```

`build.sh` compiles the native layer once, runs `aetherc` over the Aether
sources, and links. `AETHER3D_FRAMES=<n>` caps any program at `n` frames, so
every example doubles as a smoke test that terminates on its own.

`./ci.sh` builds the native layer with warnings as errors, type-checks every
module, runs every test suite, benchmark and example, and checks that every
headless one reports zero leaks.

## Examples

| Example | What it shows |
|---|---|
| `spinning_cube.ae` | The smallest complete program: window, light, one model |
| `backend_switch.ae` | The same scene through either renderer, `./build/backend_switch vulkan` |
| `models.ae` | OBJ loading, including a multi-material model drawn as one group per material |
| `lights.ae` | PBR material presets cycling with the light type, bloom, transparency |
| `water.ae` | A 256x256 Gerstner-wave ocean, 65536 vertices |
| `voxel_world.ae` | 960464 voxels of Perlin terrain, 93030 visible, one draw call |
| `black_hole.ae` | 200000 particles under Verlet integration, two instanced draws |
| `sand.ae` | 250000 grains falling and settling, click to scatter them |
| `smooth_terrain.ae` | The same terrain meshed with surface nets, 67590 triangles |

## Layout

```
native/     C: GLFW window and input, OpenGL entry points, Vulkan backend,
            float32 mesh and instance buffers, image decoding
src/a3d/    Aether modules
  core        vectors, quaternions, matrices, scene types, camera, frustum
  platform    window, input, timing
  shaders     the GLSL programs
  gl          the OpenGL renderer
  vk          the Vulkan renderer
  engine      window, main loop, backend selection
  loader      OBJ/MTL and procedural primitives
  noise       Perlin noise
  voxel       voxel worlds
  water       the ocean surface
  scene       saving and loading scenes
  rendering   presets for the shader's advanced features
  behaviour   game objects and components
  raycast     ray tests against spheres, triangles and meshes
tests/      test suites, each a program that prints its own verdict
benchmarks/ per-frame cost measured without a window
examples/   runnable scenes
```

## How it is put together

**Aether never handles float32.** A model owns a native mesh handle holding
interleaved position, uv and normal data, and, when instanced, a native buffer of
per-instance matrices. Aether drives them through `a3d.core`; the GPU reads them
with no conversion pass in between.

**Uniform locations are resolved once per program** into named slots rather than
looked up or hashed per draw. Per-model custom uniforms cache their own location
on the uniform that owns them.

**Only exposed voxels become instances.** A solid world never pays for its own
interior: 960464 solid voxels reduce to 93030 visible ones.

**Bulk paths exist where they matter.** A particle system moving every instance
each frame uploads all positions in one call, not one call per instance.

**The frame allocates nothing.** `benchmarks/bench_frame.ae` runs the heaviest
per-frame work two thousand times with no window, so what it measures is the
engine rather than the GL implementation: 578us to upload two hundred thousand
instance matrices, under a microsecond each for transforms, camera, frustum and
water, and zero leaked bytes at 26MB peak.

**The frame's uniforms go up once, not once per model.** Of the seventeen
uniforms a draw needs, sixteen are the same for every model in the frame. They
are uploaded once per program per frame, which took a four-hundred-model scene
from 1200us to 806us. `benchmarks/bench_scene.ae` keeps that honest.

**The viewport readback is pipelined.** Reading a frame into client memory stalls
until the GPU has finished it; two pixel buffers mean the read is issued into one
while the one filled last frame is mapped, so the CPU never waits. At 1280x720
that is 1625us a frame against 307us. `benchmarks/bench_readback.ae` measures
both paths in one process. The editor takes the pipelined read and is a frame
behind; anything comparing what it just drew takes the waiting one.

**The editor only redraws when something changed.** A camera move, an edit, a
selection, or a scene holding water or a behaviour. Otherwise the frame already
on screen is the right one, and rendering it again is the largest idle cost an
editor has.

## Differences from Gopher3D

- **One transform, not two.** Gopher3D's `GameObject` carries its own transform
  and the component manager copies it to and from the model's every frame in both
  directions. Here a game object points at the model that already owns one.
- **No voxel chunks.** The instanced path builds a single model for the whole
  world, so chunking bought nothing and cost a pointer chase per voxel. Storage
  is one flat grid.
- **Ray tests return a distance**, negative for a miss, instead of a tuple, so a
  query allocates nothing and the caller derives a hit point only when it wants
  one.
- **Surface nets actually runs.** Gopher3D carries the pieces of a surface
  mesher, corner sampling, edge interpolation and a gradient normal, but its
  surface path only ever emits a height field and the pieces are never reached.
  Here it is the algorithm those pieces describe.
- **Vulkan actually renders.** Gopher3D lists its Vulkan renderer as incomplete.
  Here it is at parity with OpenGL and a test proves it pixel by pixel.
- **No game export.** Gopher3D's editor builds a standalone Go binary. Saving and
  loading a scene covers getting work out of the editor; generating a program is
  a different job from editing one.

## Credits

A port of [Gopher3D](https://github.com/nicolas-maman/gopher3D) by the Gopher3D
contributors, MIT. The architecture, the GLSL programs, the material and
lighting model, the OBJ loader's behaviour and the demo scenes all come from that
project. The Go was used as the reference; none of it was copied, and the Aether
and C here are independent implementations.

Built with [GLFW](https://www.glfw.org/), [Vulkan](https://www.vulkan.org/) via
MoltenVK on macOS, and [stb_image](https://github.com/nothings/stb).

See [NOTICE](NOTICE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## License

MIT, see [LICENSE](LICENSE).
