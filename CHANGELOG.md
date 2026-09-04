# Changelog

## [current]

First working engine.

### Rendering

- `Backend`, a vtable both renderers fill in, so a program picks its renderer
  with a constructor argument and nothing else changes.
- OpenGL 4.1 core backend: PBR materials, directional and point lights,
  instanced rendering, frustum culling, a separate transparent pass, MSAA, FXAA
  and bloom. Uniform locations resolve once per program into named slots.
- Vulkan backend, windowed and offscreen, on a loader opened at runtime with
  native surface creation on macOS, Windows and X11. Nothing links against
  Vulkan, so a program built with it still starts where no driver exists.
- The Vulkan backend runs the same feature set as OpenGL: PBR materials,
  mipmapped textures, per-instance rendering, a transparent pass, MSAA, the
  skybox, and the FXAA and bloom composites. Its shaders are generated from the
  OpenGL sources, so there is one shader source rather than two that drift, and
  the std140 offsets the renderer writes at are checked against glslang on every
  build.
- Skybox and post-processing are backend capabilities rather than
  renderer-specific calls, so switching backends does not change what a scene
  looks like.
- `tests/test_backend_parity` renders the same scene through both backends
  offscreen and compares every channel across materials and textures, instancing
  and transparency, skybox, FXAA and bloom. The two agree to within 1.4% of
  channels.
- All thirteen GLSL programs ported, including the PBR and Gerstner-wave
  fragment shaders.

### Content

- OBJ and MTL loading with vertex deduplication and per-material index ranges.
- Procedural cube, sphere, plane, quad and water-grid primitives.
- Gerstner-wave ocean with the wave table in the model's uniform arrays.
- Voxel worlds drawn as one instanced call, with exposed-face culling.
- Surface nets over a signed distance field: one vertex per cell that straddles
  the surface, placed at the average of its edge crossings, with quads around
  every sign-changing grid edge and normals from the field gradient.
- Improved Perlin noise with a platform-independent seeded shuffle.
- Game objects and components, ray casting, and a fly camera with frustum
  extraction.

### Scenes

- Scene save and load. A model from a file records its path and reloads through
  the OBJ loader; a procedural one writes its geometry to a gzip-compressed
  binary mesh, float32 stored as its IEEE-754 bit pattern in little-endian order
  so a file written on one host reads back identically on another.

### Shading presets

- Default, high quality, performance and voxel configurations over the advanced
  features the fragment shader exposes: clearcoat, sheen, transmission, image
  based lighting, procedural noise, soft shadows, volumetric lighting, screen
  space occlusion, global illumination, bloom and filtering quality. Applying a
  config writes a model's own uniforms, so two models in one scene can run
  different settings through the same program.

### Engine

- Window, input and timing over GLFW.
- Main loop with a fixed-step accumulator, frame pacing and `AE3D_FRAMES`,
  which caps any program at a frame count so every example is also a smoke test.

### Editor

- A scene editor on [aether-ui](https://github.com/aether-lang-dev/aether-ui):
  hierarchy, asset browser, console, and an inspector that changes with what is
  selected. See [docs/editor.md](docs/editor.md).
- A transform gizmo that moves, rotates and scales the selection along an axis,
  dragging along the projected axis so it stays correct at any camera angle.
- Undo and redo in `ae3d.history`, a module rather than editor code, so it is
  tested without a window. An adjustment is one step rather than one per event.
- Objects can be meshes, water, voxel worlds or lights, each carrying a
  component that says what it is; the inspector shows the section that belongs
  to it. Lights are scene objects with a marker that can be picked and dragged.
- Spin, bob and orbit behaviours attach to any object and run in the frame loop.
- The engine's rendering presets, scene save and load, and a camera inspector.
- The viewport is a framebuffer object read back and blitted into a canvas,
  which is what lets a toolkit with no GPU surface host a 3D view.

### Measured

Per-frame costs, measured headless so they are the engine's own rather than the
GL implementation's, over two thousand iterations with zero leaked bytes and
26MB peak:

- 578us to upload 200000 instance matrices, 12.8MB a frame.
- Under a microsecond each for a model transform, a camera and frustum rebuild,
  and a water uniform update.
- 19ns per Perlin sample; a 131072-cell exposed-face scan under a millisecond.
- 400 separate models: 806us a frame, from 1200us before the frame's uniforms
  were hoisted out of the per-model loop.
- Reading a 1280x720 frame back: 307us pipelined against 1625us waiting.
- The editor idle: one sample in `draw_frame` over six seconds, from 367 before
  it stopped redrawing an unchanged viewport.


- 256x256 ocean: 65536 vertices and 390150 indices built in 2ms.
- 192x192x48 voxel terrain: 960464 solid voxels reduced to 93030 visible,
  generated in 9ms, instanced in 7ms, one draw call.
- 200000 particles under Verlet integration in two instanced draws at 126fps.
- 250000 sand grains falling, colliding and settling in one instanced draw at
  70fps.
- 160x160x64 field meshed by surface nets into 34241 vertices and 67590
  triangles in 33ms, one draw call at 106fps.
