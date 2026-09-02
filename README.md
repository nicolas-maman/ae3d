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
- **Vulkan**, windowed, on a loader opened at runtime. Nothing links against
  Vulkan, so a program built with this backend still starts where no driver
  exists and says so.
- **Gerstner-wave ocean**, **Perlin terrain**, **voxel worlds** drawn as a single
  instanced call, **surface nets** over a signed distance field, an OBJ/MTL
  loader, ray casting, and a component system.
- **Scenes save and load.** Transforms and materials as JSON; geometry that came
  from a file records its path, and geometry that did not is written to a
  compressed binary mesh beside the scene.

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
module, and runs every test suite and every example.

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
  behaviour   game objects and components
  raycast     ray tests against spheres, triangles and meshes
tests/      test suites, each a program that prints its own verdict
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
