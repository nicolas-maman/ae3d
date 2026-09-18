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
- **Weather.** `ae3d.weather` puts rain, snow, dust or a storm over any
  scene with one call: `weather_set(w, STORM, 0.8)`. The particles are point
  instances -- a position, a scale, a colour and a phase each, the stream
  the sand's grains use -- stepped in C in a box that rides ahead of the
  camera, so a hundred thousand drops cost a fraction of a millisecond
  wherever the eye goes; rain is a thin streak falling fast, snow a flake
  swaying down, dust a mote carried by the wind. Each kind sets the fog it
  brings, the cloud cover, the sky's overcast (`engine_set_sky_overcast`:
  the sky pulled toward a flat grey or ochre at its own brightness, which
  the clouds' ambient follows) and dims the sun; a storm adds lightning, the
  key light thrown up for three frames every few seconds at the storm's own
  beat. What the weather takes it gives back when set clear. The weather is
  a behaviour the engine runs; `tests/test_weather` holds the counts, the
  box, the sun, the fog and the lightning to their numbers.

  ![Rain, storm, dust and snow over the island](weather.png)

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
full mesh, the far tier the build's own 168-triangle stand-in, and past
eighty metres each zombie is a picture; the draw count does not change with
the crowd, and the simulation runs over the engine's job pool on every
core. Twenty thousand hold ~120 fps on an RTX 4070 Ti at 1280x720 with
the GPU shared, half a million 79.*

### Impostors

The third tier of a crowd is a picture. `tools/bake_impostor` bakes a
figure into an atlas: the figure seen from eight angles around it by eight
frames of its walk, posed exactly as the crowd poses it (its gait baked in
place into a pose bank, one instance at the origin), through a lens narrow
enough that the picture is near orthographic. Two atlases, read back
through the engine's capture channel (`engine_set_capture_channel`): the
figure's **albedo**, before any light, and its **normals** in its own
frame. A crowd model whose mesh is one upright quad, given the atlases and
`model_set_impostor(m, cols, rows, width, height)`, draws each instance as
that quad turned to the camera, showing the cell for the angle the camera
sees the instance from (measured around its facing, the instance's own +X,
which is the crowd's yaw zero) and the frame its phase is at; the fragment
cuts it out by the atlas's alpha, turns the baked normal into the world by
the facing, and lights it with the scene's lights, shadow, occlusion and
fog like any surface. A lamp that warms the mesh beside it warms the
picture the same. The transparent pixels of an atlas carry the colour of
the nearest opaque ones (bled outward at the bake), so the texture's filter
and mip levels never blend a key colour into an edge.

### The scene's depth

The occlusion, the reflection and the water read the scene's depth. On
OpenGL it is blitted out of the frame after the opaque draws; on Vulkan the
frame's multisampled depth is resolved into a one-sample target between
the two halves the scene pass is drawn in, the nearest of the samples a
pixel. Both are the depth of what was drawn -- the near figure's real
silhouette, the picture of a far one -- and neither draws anything twice.

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

