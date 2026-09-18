# What the engine draws, and how

The feature list in full, with the reasoning behind each. The [README](../README.md) is the short form.

## Features

- **Two renderers behind one interface.** `Backend` is a vtable both the OpenGL
  and Vulkan renderers fill in; a program picks its renderer with a constructor
  argument and nothing else changes. `tests/test_backend_parity` draws the same
  scene through both and compares them channel by channel across materials,
  textures, instancing, transparency, skybox, FXAA, bloom, shadows, multiple
  lights, a Gerstner ocean, an instanced voxel chunk, the clouds, the
  occlusion and instances placed as points. They agree to within 0.7% of
  channels, and CI fails if the generated Vulkan shaders fall behind the GLSL
  they are made from.
- **The sun by the hour.** `engine_set_time_of_day(hours)` puts the sun where
  the hour does and sets the key light, the fog and a sky drawn from the same
  sun -- blue at noon, gold and red at dusk, moonlit at night -- so the sky,
  the clouds and the light agree; the editor has it as a switch and a slider,
  and the scene file carries the hour.
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
  backends), volumetric clouds and their shadows, a sky drawn from the sun by
  the hour or a painted one, fog applied after tone mapping, MSAA, FXAA and
  bloom. Screen-space reflections on wet surfaces on
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
- **Volumetric clouds.** A layer of cloud marched in the sky shader over
  whatever sky is set, built the way a production sky builds it: a weather
  map says where cloud is and what kind, from a low stratus to a tall
  cumulus; a tileable Perlin-Worley cube gives the body and a Worley
  fractal erodes its edges, wisps at the base and billows above; each
  sample is lit by the sun through the cloud over it, in three octaves of
  Beer's law with the powder darkening and a two-lobe phase, and by the
  sky. The noise is baked once at start into a 2D and a 3D texture
  (`native/ae3d_cloudnoise.c`), so the march is a fetch a sample and the
  clouds are a millisecond and a half of the frame. The ground computes
  the same weather field where the sun's ray meets the layer, so their
  shadows cross the terrain as they drift. One call,
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

![Twenty thousand zombies filling the street from end to end, seen from above the pavement](zombie-horde.png)

*`AE3D_CROWD=20000 AE3D_NEAR=28 ./build/zombie_city`: the near tier draws the
full mesh, the far tier the build's own 168-triangle stand-in, and the draw
count does not change with the crowd. Twenty thousand hold ~38 fps on an
RTX 4070 Ti at 1280x720 with the GPU shared.*

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

