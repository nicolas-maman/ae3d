# Changelog

## [current]

First working engine.

### Rendering

- Both backends merge draws. Models that share geometry and a material are drawn together. Identical
  meshes are uploaded once and the renderer merges the models that use them
  into a single instanced draw, rebuilding the instance buffer only when the
  group changes, so a scene that has not moved re-sends nothing. The depth pass
  merges on geometry alone, since a shadow does not care about materials.

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
  and transparency, skybox, FXAA, bloom, shadows, two lights, a shading preset
  a Gerstner ocean driven through twelve simulation steps, back-face culling,
  the ocean through FXAA, a resize with a composite running, and an instanced
  voxel chunk. The two agree to within 0.7% of channels.
- The offscreen path is multisampled, so a scene drawn into a framebuffer with
  no window of its own is antialiased like every other surface. The viewport the
  editor composites and the frames the parity suite compares were the only ones
  in the engine drawn without it, which is most of what the two backends
  disagreed about.
- Shadow mapping in both backends. The light renders the scene into a depth map
  of its own and the lit pass compares against it, sampled over a 3x3
  neighbourhood with a slope-scaled bias. The light's box is centred on the
  scene and sized to it, so the map covers what the camera can see. Depth is
  written to a colour target because this driver returns opaque white from
  `texture()` on a depth attachment. Vulkan records its shadow pass into the
  frame's own command buffer ahead of the scene pass, which is why the scene
  pass now opens at the first draw rather than at the start of the frame.
- Up to four lights per scene in both backends, directional or point, uploaded
  into a std140 array whose stride the generator derives from the shader.
- The Vulkan renderer writes what a whole frame shares, the view position, the
  lights and the light-space matrix, once per frame rather than once per model.
- `Backend` covers the clear colour, shadows, lights, the draw count and the
  frame's pixels, so a program that switches renderers no longer reaches past
  the vtable for any of them.
- Per-model uniforms reach the Vulkan renderer. The generator emits a
  name-to-offset table for the block it lays out, and each uniform resolves its
  offset once and keeps it, so shading presets and anything else set by name
  behaves the same on both backends.
- Lights can be added to and removed from either renderer. The Vulkan one had a
  list nothing could fill, so only the key light ever reached it.
- A Vulkan renderer can be resized while a composite is running. Descriptor
  sets are cached per texture and the rebuild destroys the post-processing
  texture, so every set handed out before the resize named an image that no
  longer existed and the frame came back as noise. The editor hit it on its
  first frame.
- The Vulkan renderer culls back faces when the scene asks for it. Its
  pipelines were built with culling off whatever `core.face_culling()` said,
  because cull mode is fixed at pipeline creation before Vulkan 1.3, so the
  opaque pipelines now come in two variants. Transparent surfaces are never
  culled on either backend: you see through one to its own far side.
- The Gerstner ocean runs on Vulkan. Its program is generated from the same
  source as the OpenGL one, and the wave tables reach it as std140 arrays,
  whose element stride is 16 bytes whatever they hold.
- Each program keeps its own uniform block on the Vulkan side, the way OpenGL
  gives every program its own uniform state. One shared block let a model's
  uniforms leak into the next draw that used a different program, because the
  two programs share member names.
- A shader carries a name both backends understand, so a backend that cannot
  compile GLSL at run time picks its pipeline by that rather than by comparing
  source text.
- An offscreen Vulkan renderer can be resized. It took the surface path and
  dereferenced a null surface, which is why the editor could not use it.
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
- Improved Perlin noise with a seeded shuffle that is the same everywhere. It
  drives its generator with unsigned arithmetic, because the signed overflow
  it used to rely on is undefined in C and one compiler optimised the seed
  away entirely, giving every seed the same field.
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
  space occlusion, global illumination, caustics and bloom. Applying a config
  writes a model's own uniforms, so two models in one scene can run different
  settings through the same program. Every one of them changes the picture, and
  a test renders the scene twice per setting to say so.

- Caustics on submerged surfaces: the moving web of light a water surface throws
  onto whatever lies under it, on both backends. A config carries the water
  line, the depth over which the light fades out, and the scale, speed and
  strength of the pattern, so a scene measured in metres and one measured in
  centimetres both look right. Off by default and free when off. The renderer
  keeps the clock that moves the pattern, so it animates without the program
  driving it, and `examples/caustics.ae` shows a seabed lit through an ocean.

### Fixes

- The shader generator checked that a uniform declared in two programs means the
  same thing in both, and the check compared each name against itself, so it
  could never fire. Two programs disagreeing about a type would have laid the
  block out for one of them and corrupted what the other read. It compares types
  now, and the one name that did disagree is gone.

- The engine clock read zero until a window existed. Anything that rendered
  without opening one, an offscreen context or a test, ran on a clock that never
  moved. It reads a monotonic clock directly and no longer depends on the window
  system.

- A directional light's direction now means one thing everywhere. Shading and
  the water surface read it as pointing at the light, and every scene in the
  tree passes it that way, but the shadow camera and the shadow bias read it as
  the way the light travels. The shadow map was rendered from the side opposite
  the light, so a lit scene put its objects in their own shadow and cast nothing
  on the ground: with the sun to the right, the ground darkened by zero either
  side of the blocker. The direction points at the light, the shadow falls away
  from it, and a test measures which side it lands on, which nothing did before. The water surface's god rays pointed at the world origin rather than at
  the sun, for the same reason, and now follow the light like everything else. The ocean example asked for a sun four and a half times as bright as
  the sun, which is what it took to see anything when none of that light was
  reaching the water; it asks for one sun now.

- A model is shaded by what it asked for and nothing else. Settings are uploaded
  per model and the program keeps whatever the last draw wrote, so a model that
  asked for nothing picked up its neighbour's occlusion, noise and the rest:
  configuring one sphere took more than half the light off the untouched sphere
  beside it. Before a draw whose set of settings differs from what is loaded, the
  unset values go in first and wipe the difference. Models configured alike agree
  on the set, which is the common case and costs nothing: the scene and shadow
  benchmarks are unchanged.

- A copy of a model shades like the model it was copied from. `model_clone`
  copied the mesh, the material and the transform but left the settings behind,
  so a copy came out with everything off. It also abandoned the empty mesh the
  new model was born with and never handed its cloned material to anyone, so
  both leaked; a copy now frees everything it holds.

- Resizing no longer leaks the Vulkan shadow target. A rebuild made a fresh
  shadow image, its memory, two views, a depth image with its own memory, a
  sampler and a framebuffer, and destroyed none of the previous set, because the
  swapchain teardown never touched it. No frame ever looked different for it, and
  the runners have no Vulkan driver on macOS, so nothing could have noticed: the
  backend now reports how many shadow targets it holds and the parity suite
  checks that a resize replaces one rather than adding one.

- Eight settings that did nothing are now real or gone. `specularColor` tints
  what a dielectric reflects, `enableShadows` decides whether a model receives
  them, and `enableImageBasedLighting` with `iblIntensity` turn the environment
  reflection up and down, all of which the shader declared and never read.
  `shininess` is a specular exponent this shader has no use for, and a model
  differing only in it was refused a merged draw; a `.mtl` file's `Ns` now
  becomes the roughness it corresponds to, where before it was parsed and thrown
  away. Bloom radius belongs to the post pass that has one, and filtering
  quality is sampler state rather than shading, so both are gone from the config,
  along with `advanced_lighting`, which nothing ever applied.

- The water surface's settings work too. Reflection strength, distortion and
  normal strength were each wired to a public setter and read by nothing;
  `simulation_set_specular` is now `simulation_set_reflection`, which is what it
  scales, and normal strength decides how far the waves tilt the surface away
  from flat. Refraction needs a picture of what is behind the water, which this
  shader never had, so its two uniforms are gone, as are two more that repeated
  what transparency already said. The surface also averaged four normals offset
  symmetrically around its own on every fragment, which cost five normalizes and
  returned the normal it was given, because the offsets cancel.

- The fog colour the water is given is the fog colour it uses. A local of the
  same name shadowed the uniform inside the fog block, so the surface faded into
  a shade nobody chose. The time of day still decides the shade and the colour
  now tints it.

- FXAA reads five pixels rather than nine. Four corner samples were fetched,
  turned into luma, added into two sums that were the same sum, and never looked
  at again, so four of every nine reads a full-screen pass made were for
  nothing. The frames are identical, and backend parity still holds.

- A model's bounds follow it wherever it goes, and cost nothing to keep. They
  were refreshed only when frustum culling was on, which it is not by default,
  so a rotated model kept the bounds it had before: everything that reads them
  read the wrong thing, and picking a floor that had been turned silently
  missed, since the mesh test gives up early on the bounding sphere. Each
  refresh also scaled and rotated every vertex twice, which is what a move used
  to cost. The mesh's own centre and radius are worked out once and put through
  the model matrix, so the bounds are exact for a rotation, never too small for
  an uneven scale, and two thousand moves went from six milliseconds of work to
  too little to measure.

- A saved scene keeps its shading. Every setting lives in a model's own
  uniforms, and the file recorded the transform, the material and the geometry
  but none of them, so a configured scene loaded back with everything off, and
  a model told not to cast a shadow came back casting one. The editor saves and
  loads scenes, so a session's work did not survive the round trip. Files
  written before this load exactly as they did.

- Features that fade out with distance measure against how far the camera can
  see, not against a count of world units. Volumetric lighting returned nothing
  for anything closer than a thousand units, so in a scene a few hundred units
  across it was switched on, cost nothing and showed nothing; occlusion,
  global illumination and the water's reflection detail stepped at five, ten,
  twenty, thirty, fifty and a hundred thousand. A scene measured in metres and
  one measured in centimetres now behave the same, and the suite that renders
  every setting twice makes its scene an ordinary few hundred units across to
  say so. Foam on the wave crests went the same way: it needed a crest four
  hundred and fifty units above the world's zero, which no ocean this engine has
  drawn ever reached, so it appeared on none of them. A crest is now measured
  from the water's own level against how high its waves go.

- A saved scene keeps its lights. The file recorded models and nothing else, so
  the editor's sun, whose intensity, ambient strength and colour it lets you
  set, came back at its defaults after a save and a load, with nothing said
  about it. `scene_save` takes the lights alongside the models and
  `scene_load_lights` hands them back; a file written before this has none
  recorded, and the scene keeps whatever it was already lighting with.

### Portability

- The renderer asks the driver whether it really performs a multisample resolve
  before relying on one. A driver that accepts the call, reports no error and
  copies nothing used to leave every frame with an effect on it black; it now
  gets the effect chain without multisampling, and says so once.
- A model no longer loses its texture to the one drawn before it. Binding the
  shadow map leaves the default texture bound, which the renderer's record of
  what is bound was not told, so a model whose texture matched its predecessor
  had its bind skipped.
- Both backends free what they allocate: the geometry a renderer shares, the
  batches it builds, and the copies it keeps to recognise geometry it has seen.

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
- 400 separate models: 65us a frame and one draw call, from 1005us and four
  hundred. The renderer stopped re-sending state a draw already had (812us),
  then stopped issuing a draw per model at all.
- 200 shadow casters: 52us a frame unshadowed and 362us with shadows, from
  551us and 839us. What is left of the shadow pass is fill: a 2048x2048 depth
  map, not the draws that fill it.
- Multisampling the offscreen target costs nothing measurable on a scene of 400
  separate models: 1005us a frame against 1010us with it off, because that scene
  is bound by its draw calls rather than by fill.
- The Vulkan offscreen path no longer waits for its readback. Each frame in
  flight copies into a staging buffer of its own and whoever asks for the
  pixels waits then, so a program that renders without reading never stalls:
  200 casters go from 695us a frame to 264us, and with shadows from 842us to
  360us against OpenGL's 359us.
- Reading a 1280x720 frame back: 307us pipelined against 1625us waiting.
- The shadow pass over 200 casters at 1280x720: 248us in OpenGL and 103us in
  Vulkan, which records it into the frame's own command buffer. Measured by
  alternating the two states block by block and keeping each one's fastest,
  because the difference is smaller than the spread between whole runs.
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
