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

- Loading a scene that holds two models of the same shape crashed the editor.
  Merged draws are pooled and matched to a model by vertex array id, the pool
  outlives the models it was built from, and OpenGL hands the same id out again
  for the next array it creates. So a batch built for geometry that had since
  been released matched its replacement by number, decided it was unchanged,
  skipped its upload and drew from the buffer of the array that was gone:
  `SIGSEGV` inside `glDrawElementsInstanced`, six times out of six. Releasing
  geometry now tells the pool to forget it.

- An instanced model had its scale and rotation applied twice. Each instance's
  matrix is built from the model's own scale and rotation, and the shader
  multiplies the model matrix by the instance's, so a model scaled 2.4 drew its
  instances at 5.76. One instance of a sphere scaled 30 covered the whole frame
  where the model alone covered 1062 pixels, and the magnified sphere showed
  almost no shading across the part still on screen, so `examples/sand.ae` drew
  a handful of flat blobs instead of grains. The merged-batch path had always
  sent identity for this reason; the per-model path now does too, in both
  renderers.

- Every surface built from a signed distance field faced into the solid. The
  builder negated the field's gradient, which already points out of it, so a
  terrain lit by a sun overhead gathered nothing from it and was drawn at
  ambient only. `examples/smooth_terrain.ae` came out a flat olive with no
  relief at all: mean luma 95 and a standard deviation of 3 across the
  terrain, against 145 and 6 once the normals face out. Nothing caught it
  because a normal that is exactly backwards still produces geometry, still
  triangulates and still renders. It renders dark.

- `core.mesh_normal` reads back what a mesh was given, which `mesh_set_normal`
  could write and nothing could check.

- A colour set on a model loaded from a file was silently dropped. A loaded
  model is drawn group by group, and a group carries a material only when the
  file gave it one: an OBJ whose `mtllib` line is commented out, or which names
  a material no `.mtl` defines, produces groups with none. Those were drawn
  white and the model's own material, the only one a caller can set from
  outside, was never consulted, so `model_set_diffuse`, `model_polished_metal`
  and the rest did nothing on anything loaded from disk. A group without a
  material of its own now uses the model's. `examples/models.ae` set a gold
  metal and a blue and rendered both in the same cream.

- The water's `transparency` was its opacity: the number went straight into the
  surface's alpha, where 1.0 hides what is behind it. Every caller that set it
  high to see through the water was making the water opaque, and the caustics
  example was one of them, so the example about what a surface throws onto a
  seabed did not show its seabed. The knob is `opacity` now, from
  `simulation_set_opacity` through the scene field and the editor row down to
  the `waterOpacity` uniform in both backends' shaders, and the example asks
  for clear water.

- A second camera was culled against the first camera's frustum. The dirty flag
  guarding the cached frustum was one boolean for the whole program, while the
  frustum it guarded belongs to a renderer, so the first render of a frame
  filled its frustum and cleared the flag and every later render reused it.
  Anything that draws the same scene from two cameras, a minimap, a reflection,
  a second viewport, culled the second view against the first. The flag is gone
  and the frustum is filled from the camera being drawn: six planes out of one
  matrix multiply measures below the benchmark's resolution, 0 ms over 400
  frames, which is not worth a cache that can be wrong.

- The leak check skipped every suite that uses `ae3d.engine`, and both of them
  were losing the model they built in `on_start`. The exclusion existed because
  a program that opens a window produces leaks it cannot do anything about, but
  the check already told a system retain cycle apart from a lost allocation, so
  it was broader than its reason. It now judges a leak by how close the frame in
  this binary is to the allocation: something this code lost was allocated a
  frame or two below its own call, while AppKit's stray array from window
  teardown has ten Apple frames in between and only reaches `main` because
  `main` called `glfwDestroyWindow`. Three of `test_render`'s exits were also
  losing an engine and a light.

- Removing an instance left every instance after it wearing the colour of the
  one before. Removal shifts the colours down one exactly as it shifts the
  matrices, but only the matrices were flagged for re-upload, and OpenGL sends
  the two buffers separately. Vulkan re-sends both whenever either changed, so
  it was right by accident and the difference hid the bug. Resizing an
  instanced model had the same hole, in the other direction: new instances
  came up wearing whatever the colour buffer last held.

- A camera would accept a field of view of zero or half a turn, a near plane at
  or behind the eye, or a near plane equal to the far. All three divide inside
  `mat4_perspective`, and the result is a projection full of infinities: every
  frame after it is empty, with nothing on screen to say why. The three setters
  refuse those values, the way `camera_set_aspect` already refused a zero
  aspect. A slider could never reach them; a number field can.

- Adding a component to a game object switched it back on. A component
  deliberately disabled before it was added ran anyway on the next update,
  and there was no way to add one in a disabled state. Adding a component is
  no longer a decision about whether it runs.

- Five shader accessors returned full-screen post passes and a skybox as if
  they were model shaders. Nothing called them, and anything that had would
  have compiled a screen-space program into a model's slot, where the
  uniforms it samples are never bound. They are gone.

- Handing the frame to the editor's canvas copied the whole viewport every
  frame. A profile put 61% of the frame in that `malloc` and `memcpy`, more
  than reading the frame back off the GPU and about six times the cost of
  rendering it. The canvas borrows the renderer's buffer now instead of
  copying it, and the readback alternates between two buffers so the frame
  already handed over is never the one being written. Measured over 900
  frames: 3.74s of CPU to 3.26s, with system time down a quarter.

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

- Undo steps back one adjustment, not every adjustment a slider has ever made.
  Edits to one property coalesce so that a drag, which delivers a callback per
  pixel, is a single step, and nothing ever closed that step: letting go and
  taking hold of the same slider again extended the first one, however long the
  pause, so undo jumped back to wherever the property stood before the first
  drag. An edit that has finished now closes its step, and the editor says so
  when the edits stop arriving.

- Frustum culling is checked against what it is for: the frame has to come out
  byte for byte the same whether culling is on or off, while the count of draws
  falls. Nothing tested either half, so a culler that dropped something visible
  and a culler that dropped nothing would both have passed. The suite includes a
  model whose middle lies outside the view and whose near side reaches into it,
  and a floor built from a corner and turned about, which are the two cases a
  culler working from bounds gets wrong.

- `core.set_draw_merging` turns automatic merging off, which is how a program
  sees a scene as its draws really are, and how the scene benchmark measures
  what the merging is worth rather than quoting a number from memory.

- The post-processing checks ask whether the effect did anything. Comparing the
  two backends says they agree, which a pass that quietly copied its input
  through would satisfy on both sides at once, and that is exactly how the
  multisample resolve failure hid. Turning FXAA or bloom on now has to change
  what the frame adds up to, in both renderers. The skybox is asked the same, against a
  frame rendered for the purpose rather than whatever the last comparison left
  in the buffer.

- A copy of a model keeps the shader it was told to draw with. `model_clone`
  carried the mesh, the material, the transform and, since the settings fix, the
  uniforms, but not the model's own shader, so a copy quietly fell back to the
  default program.

- An instance that moves or changes colour reaches the GPU. The buffer holding
  every instance's transform and colour was uploaded when the model was
  registered with a renderer and never again: on OpenGL the per-frame path
  re-sent the matrices alone, and neither colour setter asked for even that; on
  Vulkan nothing re-sent anything, so an instanced model there drew its first
  frame forever. A particle system that moves its particles and a voxel world
  that recolours a block both asked for something the engine accepted and never
  did, on both backends, and the suites drew instanced geometry without ever
  moving it.

- A mesh reshaped after its model was drawn reaches the GPU. The setters that
  move a vertex marked the mesh dirty and nothing read the flag, on either
  backend, so anything deforming geometry on the CPU asked for something the
  engine accepted and never did. Identical geometry is uploaded once and shared,
  so a model whose vertices have moved stops sharing before its new shape goes
  up, rather than reshaping everything else drawing what it used to be.

- A texture asked for after the model was registered reaches the frame. Both
  renderers resolved a path to a texture once and skipped any material that
  already had one, so setting a new path wrote it into the material and changed
  nothing on screen. The material says its texture is stale, and the next draw
  lets go of the one it was holding and takes the one it now asks for.

- The workflow runs when someone starts it, rather than on every push and every
  pull request. It was two runners a change for work `./ci.sh` does on the
  machine the change was written on.

- A water setting takes effect when it is set. Every one of them wrote a field
  that only `simulation_apply` copied into the uniforms the shader reads, and
  that ran once, from `simulation_build`: colour, transparency, wave height,
  wave speed, randomness, caustics, reflection, shadow, distortion and normal
  strength all did nothing once the surface existed, which is why the editor's
  water sliders moved nothing at all.

- `skybox_set_color` is gone. A skybox with no texture is drawn as the clear
  colour, which is copied when the skybox is handed to a renderer, so setting
  its colour afterwards changed a field nothing read again. Nothing called it,
  and `skybox_solid` takes the colour it is built with.

- A scene file that lists models and produces none is refused rather than
  handed back empty. An empty list is what a scene saved with nothing in it
  looks like, so a file whose meshes had all gone missing loaded as a success
  with nothing in it and the reason went unread. What it can build it still
  keeps: one model missing its mesh does not lose the rest.

- The editor's wave height slider sets the wave height. It wrote the multiplier
  the shader scales the wave table by and read back the height the table is
  built from, two different quantities, so the readout never followed the
  handle and an undo wrote a height into a multiplier. A simulation's wave
  height can now be set after it is built, which rebuilds the table it is
  derived from.

- Nine inspector rows do something. Every row from nine up was caught by the
  light branch, which wrote only intensity and ambient, so wave height, wave
  speed, transparency, the light's three colour channels and the camera's field
  of view, near and far plane all recorded an undo step, moved their own
  readout, and changed nothing. They are dispatched by what they control, the
  camera rows are implemented, and the editor's report says how many rows
  cannot move their target so the run fails when one of them cannot.

- Undo puts back what the gizmo did. Dragging an axis, a ring or a handle
  recorded nothing at all, so the next undo stepped back through whatever came
  before the drag and put that back instead, leaving the object where the drag
  had left it. A move and a scale are recorded in the same slots their inspector
  rows use; a turn is recorded as the angle the drag has turned through and
  undone by turning back through it. Replaying a water row also names the water
  it was recorded against, rather than whichever object happens to be selected
  when the undo arrives.

- The shadow suite runs on both renderers. It asked its questions of OpenGL
  alone, and Vulkan's shadows were covered only by a comparison between the two,
  which passes just as well when both are wrong in the same way. A backend also
  says whether it has a shadow map, so asking for shadows can be told apart from
  having them.

- The editor's actions are asked for their effect, the way its rows now are.
  Duplicate has to add a model, Delete has to take one away, Frame selection has
  to move the camera, and each of the three scripts has to leave what it is
  attached to somewhere else after a tenth of a second. A button that dispatches
  to nothing looks exactly like one that works when the only witness is a person
  watching the viewport, which is how nine rows came to do nothing at all.

- A camera moves at its speed whichever way it points. Each held key was scaled
  separately and added, so a camera holding two of them travelled at the speed
  of both: forty one percent faster on the diagonal than along an axis, which is
  not what a speed means. The keys are added up first and scaled once, and
  `camera_move` is that step by itself, so a program driving its own input gets
  the same behaviour.

- The engine calls the fixed step it was counting. The accumulator was filled,
  drained a step at a time, and nothing called in between, so `engine_set_fixed_step`
  set a number the engine subtracted from itself: a program that wanted a fixed
  update waited forever, and a behaviour scene's `fixed_update` had to be driven
  by hand. `engine_on_fixed_update` is called once per step that fits in the
  frame, with the step rather than the frame's own delta.

- The OpenGL post-processing buffers follow the viewport. They are resized by
  comparing the viewport against the size recorded on the renderer, and updating
  the viewport recorded the new size without resizing them, so that comparison
  was always equal and they stayed as they were. With FXAA or bloom on, a
  viewport made smaller drew the frame at the old size and stretched it over the
  new one: a third of the pixels Vulkan lit. Every resize in the parity suite had
  the composites switched off, which is why nothing saw it.

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

### Tooling

- The editor driver waits for what it is about to assert rather than sleeping
  first. Every count it checks follows an action the editor performs in its own
  time, and a sleep long enough on an idle machine fails inside a full run,
  naming the check rather than the timing assumption behind it.

- The editor driver runs on both backends. The report checks have always run on
  each, but nothing had ever pressed a widget on the Vulkan one, and the
  editor's controls reach the renderer through a vtable that only a real click
  exercises.

- The editor driver checks that a setting survives the scene file, not just
  that the row count does. The value is changed after saving on purpose:
  left alone it would come back whatever loading did, and the check would be
  proving that memory keeps its contents.

- `tools/check_ui_name_collisions.py` refuses a name the editor shares with
  something `ui` exports. A bare call to such a name binds the toolkit's
  function inside the `ui.window` block and the editor's outside it, silently
  and in both directions, which is how the Undo button came to step an empty
  stack belonging to the toolkit. It comes back whenever either side gains a
  name, so it is checked rather than remembered.

- The editor driver presses Save, Load and Delete as well. A scene that never
  reaches disk and a Load that brings back nothing both look like a working
  editor from the inside: the buttons return and the report is unchanged.

- `tools/drive_editor.py` presses the editor's real widgets. Every other check
  on the editor reads the report it writes about itself, and that report comes
  from calling the handlers directly, so a button that cannot be hit, a field
  whose callback is not wired, or a row that does not answer a click all pass.
  The driver clicks Cube and counts the scene list, types into a position field
  and selects away and back: what comes back is the model's own formatting,
  which is the only thing that proves the typed value got there. Gated in
  `ci.sh`, skipped where there is no python3 or no display.

- `AE3D_SNAPSHOT=<path>` writes the last frame of a bounded run to a PNG, so
  any program built on the engine can be looked at rather than only run.
  Running an example proves it does not crash and counting its draws proves it
  asked for something; neither says what came out, and an example rendering a
  flat wash of one colour exits zero with the same draw count as one rendering
  the scene it is named after. zlib does the compression, which the build
  already links, so what was added is the chunk framing PNG puts around it.

### Examples

- `spinning_cube`, `models` and `lights` aim their cameras. All three put the
  camera above their subject and never called `camera_look_at`, and a new
  camera looks along -z rather than at anything in particular, so all three
  framed their subject in the bottom third. `water` is left alone: its camera
  looks at a horizon on purpose.

### Editor

- The console's caption lines up with its lines. A section bar and a caption
  that shares a row with a button are different things, and using the bar for
  both gave the caption the bar's padding on top of the panel's own, sitting
  it fourteen pixels right of everything beneath it.

- The viewport's ground is a dark blue grey rather than near black. A scene on
  black looks like it is floating in a void rather than standing in a room, and
  an empty one is the first thing the editor shows.

- The grid has its axes. A grid of identical lines says how big things are and
  nothing about where they are, so a scene with nothing selected gave no way to
  tell which way round it was. Red along x and blue along z, the colours the
  gizmo already uses, muted so they do not compete with it.

- A section header is a bar across the panel rather than a word floating over
  the rows. The headings were the same weight as the labels beneath them and
  carried no rule, so scene, add, terrain, assets and edit read as one
  undifferentiated column.

- The tool buttons are the size of tools. At their old height the add and
  terrain grids took more of the left column than the outliner did, which is
  the wrong way round for ten actions that are pressed once each.

- The hierarchy says what each object is and which one is selected. Every row
  was the same grey word, so a water surface and a cube looked alike and the
  inspector was the only thing that said what was being edited. Each row now
  carries a one-character marker for its kind and the selected one is lit.

- A bounded setting is one line: label, slider and value across a row, the way
  all three of the editors this borrows from draw one. The slider used to sit
  under its own caption, which cost two lines a setting and read like a page of
  preferences rather than an inspector; eleven of them filled the panel twice
  over. Transform, material, light, camera and behaviour now all fit at once
  where material alone used to reach the bottom.

- Captions share a column, so every control starts at the same place. Ragged
  control edges are most of what makes a panel look unfinished.

- The transform modes are on the viewport. Move, rotate and scale were reachable
  only by pressing W, E or R, which is the convention but not something a panel
  can show you: nothing on screen said which was live. The live one wears the
  accent, and the keys still work.

- The colour's hex was cut off. The value column is sized for a number and a
  hex colour is seven characters.

- Deleting an object made the editor stop believing what the others were. A
  model, its component and its script live at the same index in three lists,
  and only the add path moved all three: delete, undo of an add and redo of one
  each moved the model alone, so every component after that point answered for
  the wrong object. Adding water and then deleting an unrelated cube left the
  inspector refusing to show the water section for the water, and
  `update_water` driving whichever object had inherited the component. Every
  path moves all three now, and a detached model keeps its component and its
  script so an undone delete brings back the simulation rather than a bare
  mesh.

- A section with nothing to edit hides whole. Hiding the water settings hid
  the heading, each slider and each readout, but not the rows holding their
  captions, so a scene with no water in it showed `wave height`, `wave speed`,
  `opacity` and `foam` as four stranded words with no heading above them and
  no controls under them.

- The Undo button did nothing. aether-ui exports names of its own for stepping
  the toolkit's command stack, and inside the `ui.window` block a bare call
  bound to those rather than to the editor's own, so the button stepped an
  empty stack belonging to the toolkit. The editor's self-check calls the same
  function from the top level, where its own definition wins, so it reported a
  working undo the whole time. The editor's history stepping is called
  `undo_step` and `redo_step` now, and the driver presses the button and checks
  the object comes back off. Filed upstream as aether-lang-dev/aether-ui#112.

- The odd button out of a pair spans its row. `Light` and `Caves` each sat at
  their own width beside a spacer, a third the size of the buttons above them,
  which made the add grid look unfinished. Each section is now two pairs and a
  full-width row.

- The frame rate read 0 for the life of the program, and the frame delta never
  left its fallback. The editor measured time by arithmetic on `clock_ns()`,
  and in place that clock only ever changed in whole seconds: the interval
  between two frames measured 0 for a run of ticks and then 1. It reads
  `platform.time()` now, the same double off the monotonic clock the engine's
  own loop uses, and the interval reads 0.033, 0.021, 0.014 with the bar at 62
  fps. Water and behaviour scripts were advancing at a fixed step whatever the
  machine was doing.

- The status bar refreshed on a count of frames, and the viewport is not
  redrawn while nothing changes, so the bar froze on whatever it last managed
  to write. It is throttled by time now.

- The left panel ran out of colour. Its background is drawn by the stack inside
  the scroll view, and that stack is only as tall as what it holds, so
  everything below the last button was the toolkit's own white. The panel
  colour is on the scroll views now as well.

- The material colour chip was an empty outline. The style sheet is applied to
  the tree after it is built, so the colour set during the first selection was
  wiped by it, and the readback the check trusted reported the colour anyway.
  It is painted on the first frame now, beside the split positions that are set
  there for the same reason.

- The editor's report counts `mis_styled`: a widget whose class the sheet never
  defines styles nothing and renders in the toolkit's default, and nothing in
  the widget tree says otherwise.

- The material colour is a painted chip beside the hex, not hex alone. The
  three channel sliders never show the colour they add up to, and a chip is
  how all three of the editors this borrows from draw a colour property. It
  is a fixed-size label with a background colour rather than a canvas, since
  a second canvas in a panel collapses it.

- The editor's report counts `chip_wrong`: the colour actually painted on the
  chip, read back off the widget, against the colour the material holds. A
  chip that is never painted looks exactly like a chip showing a very dark
  material, and nothing else in the tree can tell them apart.

- The inspector uses a number field where a slider made no sense. Position was
  a slider clamped to plus or minus four hundred, so an object further out than
  that could not be typed and the control pinned; the far plane was a slider
  from two hundred to eight thousand, where a pixel is thirty units. Position,
  scale and the two clip planes are fields now, and the sliders that remain are
  the ones whose range is the whole of the value: colour channels, metallic,
  roughness, transparency, field of view.

- Position is one row of three fields with coloured X, Y and Z letters instead
  of three labelled rows, which is how the same property is drawn in Unreal,
  Unity and Godot. It reads as one thing and costs a third of the height.

- The add buttons are a grid grouped by what they make, with the five biomes
  under their own heading, instead of ten identical full-width bars in a
  column. Delete has its own colour, being the one button in the panel that
  pressing again does not undo.

- The editor's report counts `blind_fields`: a row that applies its value but
  never shows it. `stuck_rows` cannot see that, because it drives the property
  directly and never looks at the control.

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
- 400 separate models: 87us a frame and one draw call, against 1363us and four
  hundred with merging switched off. The renderer stopped re-sending state a
  draw already had (812us), then stopped issuing a draw per model at all. The
  scene benchmark measures the same scene both ways rather than quoting one
  number without the other.
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
