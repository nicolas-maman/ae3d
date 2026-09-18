# Changelog

## [current]

### Game objects and scripts, the Unity shape

- A program is game objects in the engine's scene with scripts on them,
  the way Unity and gopher3D had it: `engine.object(e, name, model)` puts a
  game object in the scene and its model in the renderer (on the first
  frame, when the window is not up yet; the model is the object's from
  then on), `engine.script(o, name, state, start, update, fixed_update,
  late_update)` is AddComponent with the phases by Unity's names, and
  `engine.destroy(e, o)` takes an object out at the end of the frame. Every
  phase gets the script's state and the object it is on -- `start(state,
  go)`, `update(state, go, delta)` -- the way a MonoBehaviour has `this` and
  `gameObject`; `engine.of(go)` is the object's engine and
  `engine.running()` the one whose loop is running. The engine owns its
  scene (`engine_scene`) and frees it, its objects and their models with
  itself. `behaviour.object_add_script` is the same on a bare scene;
  `scene_late_update` runs the late phase, which the engine's scene now
  does after every update; components' `awake`, `start` and `destroy`
  are handed the object too. Every example is written this way, with a
  struct of its own state where it has any, and `tests/test_gameobjects`
  runs one through the engine on both backends. The old shape -- a
  `Behaviour` with `on_start(state, e)` and `on_update(state, e, delta)`
  registered on the engine -- is what the engine's own systems are, and
  stays for them.
- The editor's scripts export `start` and `update`, not `script_start` and
  `script_update`: the same names as everywhere else. The loader looks for
  those; the template the editor writes and the four shipped scripts have
  them.

### The loop owns the lifecycle

- A program is a **behaviour** the engine runs: `engine.behaviour_new(name,
  state)`, its `start`, `fixed_update`, `update`, `pose` and `late_update`
  set to the program's functions, `engine_add_behaviour`, `engine_run`. The
  engine's loop calls the phases, in that order each frame, and the program
  never calls them itself -- the way a MonoBehaviour joins a Unity scene or
  a gopher3D behaviour registers with its manager. Any number of behaviours;
  one added while the loop runs starts on the next frame; a scene of game
  objects and components joins the loop through `engine_add_scene`. The
  single hooks `engine_on_start/on_update/on_fixed_update/on_render/on_pose`
  and `engine_set_user_data` are gone with the `data as *Engine` casts they
  needed: every phase is handed the behaviour's state and the engine. Every
  example, tool and test is a behaviour now.
- **Vulkan by default.** `engine_new()` draws through Vulkan, and through
  OpenGL where there is no driver, said so; `engine_new_with(api)` is the
  explicit choice and `AE3D_API=opengl` the override. The editor the same.
  `engine_run(e)` takes no window position; `engine_run_at(e, x, y)` does.
- The agent's `snapshot` works on Vulkan: the capture is armed for a pending
  snapshot before the frame is drawn, and the answer arrives with the file.
- `engine_fps` reports the run's rate before the half-second window has
  filled, so a short bounded run says its rate and not zero.

### Clouds as textures

- The clouds are drawn from baked noise instead of noise summed in the
  shader: `native/ae3d_cloudnoise.c` bakes a 256x256 weather map (where
  cloud is, its kind, its banks -- the same tileable field the ground shader
  computes for the cloud shadow, so the shadow under a cloud is the cloud)
  and a 64x64x64 Perlin-Worley shape cube with a Worley fractal in its other
  channels, once at start, uploaded on both backends (`sampler3D` on
  OpenGL; bindings 3 and 4 of the Vulkan sky draw). The sky march is a fetch
  a sample where it summed forty hashes: the sky stage of a scene under
  clouds drops from about six and a half milliseconds to about one and a
  half, and the frame rate of `smooth_terrain` goes from 86 to over 130.
- The cloud itself is built the way a production sky builds it: a height
  profile by kind, a stratus low and flat and a cumulus tall; the shape
  remapped by the Worley fractal and carved by the cover; edges eroded by
  the fractal at six times the scale, inverted at the base for wisps; three
  octaves of Beer's law through a six-sample march toward the sun; the
  powder darkening; a two-lobe Henyey-Greenstein phase; ambient from the
  sky that darkens down the layer. The march strides through clear air and
  steps finely in cloud, ends twelve kilometres into the layer, and the
  clouds sit in the haze by their distance. The layer is 1400 to 2600 m.
- `native/shaders/generate.py` takes a sampler as `name:3D` for a
  three-dimensional binding.
- The clouds are lit by the key light's colour times its intensity, and
  their sky light is lifted toward white only by day: a sun the sky module
  had set below the horizon lit them as at noon all night, and a cloud lit
  by a dark sky is a darker patch of it.
- A batched model that moves no longer stalls the frame on Vulkan: the
  batch's instance stream is written into its ring, a slot a frame in
  flight, instead of freed (a wait for the whole device) and uploaded
  afresh. `lights`, five spinning spheres, went from 6.9 ms of CPU a frame
  to 0.1. `scripts/perf.sh` reads a stage time printed in exponent form.
- The scene's split by stage is on the editor's stats bar (depth, sky,
  opaque, ao, clear) and in the agent's `frame.stats` (`stages_ms`), on
  Vulkan, the same figures `AE3D_PERF` prints. A frame with no sky to draw
  stamps the sky's end all the same, so its opaque draws are no longer
  charged to the sky.

### Weather

- `ae3d.weather`: rain, snow, dust and storm over any scene, as point
  instances stepped in C (`native/ae3d_weather.c`) in a box that rides
  ahead of the camera; each kind brings its fog, cloud cover, sky overcast
  and a dimmed sun, and a storm its lightning; `weather_set(w, CLEAR, 0)`
  gives everything back. `AE3D_WEATHER=rain|snow|dust|storm` and
  `AE3D_WEATHER_LEVEL` on `smooth_terrain`; `tests/test_weather`.
- `engine_set_sky_overcast(amount, colour)`: the sky, painted or by the
  hour, pulled toward a flat cast at its own brightness, on both backends.
- The sea takes the scene's fog. The water surface carried a haze of its
  own (a fraction of the ocean's size, sky blue, a third strong) that
  overrode whatever the scene set, so a fog that buried the island left
  the sea beside it blue.
- A Vulkan instance stream that was empty when its model was added gets
  its buffer the first frame it has instances.
- The white band along the horizon under any cloud cover is gone: the
  cloud march faded its alpha toward the horizon but not its premultiplied
  colour, which was then added over a sky the alpha had left standing
  (rows at the horizon read 249..255 under a storm; the fogged sea below
  them 132). Both fade together now and the layer meets the fog.
- The sea reflects the sky there is: the water shader takes the sky's
  overcast (`skyOvercast`, `skyOvercastColor`, through the water
  program's own uniforms on OpenGL and the shared block on Vulkan) and
  pulls its reflected sky and sky light toward the overcast's grey, so
  the sea under a storm is grey-blue and not the painted afternoon's
  blue. `tests/test_backend_parity` holds the overcast sea on both
  backends and checks it goes greyer.
- The editor's weather section (#328): the kind as five buttons, the
  strength, the wind's heading and speed as rows, undone and saved with
  the scene (`weather` in the file, the kind by name), and an overcast
  row beside the cloud cover. The weather module runs over the viewport
  through `engine.engine_over(renderer, api, camera, light)`, an Engine
  around a renderer somebody else draws with, stepped by `engine_update`.
  `tools/drive_editor.py` presses the buttons, reads the file and loads
  it back.
- The weather gives the scene's own sky back: `weather_set(w, CLEAR, 0)`
  restores the clouds and the overcast as they stood when the weather
  came, not none (the engine records what `engine_set_clouds` and
  `engine_set_sky_overcast` last set: `engine_clouds`, `engine_cloud_wind`,
  `engine_sky_overcast`, `engine_sky_overcast_color`), and
  `weather_relight` re-takes the key light when the scene sets it under
  the weather.
- `render.set` in the agent channel takes `clouds` and `cloud_wind`,
  `overcast` and `overcast_color`: the sky a weather brings, by hand.
- The editor's view rows (cloud cover, occlusion, overcast, weather) read
  their own state in the row check, so they no longer report themselves
  stuck with a model selected.

### The channel's client, in Aether

- `tools/ae3d_agent.ae` replaces `tools/ae3d_agent.py`: the same command
  line (`--port`, `--raw`, `--script`, `op key=value ...`, `help` rendered
  readable, non-zero exit on a refusal) written against `ae3d.probe`, so
  the client speaks the engine's protocol in the engine's language and
  changes with it in the same build. `--docs` writes `docs/agent.md` from
  the schema on stdin around the text in `tools/agent_doc_text.md`;
  `scripts/gen_agent_docs.sh` builds and runs it and needs no Python.
- `tools/measure_scene.ae` replaces `scripts/measure_scene.py`, the same
  measurements (every model on screen and its images loaded, the traces,
  each surface's colour alone against the sky, the depth test's stability
  under half a millimetre, the animation reaching the picture) against a
  scene `ci.sh` starts, on OpenGL and on Vulkan; the numbers it reports
  are the Python's to the last digit.

### glTF

- `ae3d.gltf` loads glTF 2.0 -- `.gltf` with its `.bin`, or `.glb` -- into
  the engine's models, skeletons and clips: the node tree with TRS or
  decomposed matrices, mesh primitives with position, normal, uv, joints and
  weights, materials with base colour, metallic, roughness and their
  textures (embedded images decoded and registered under
  `<file>#image<n>`), skins with their inverse bind matrices, and every
  animation as one clip per node, STEP, LINEAR and CUBICSPLINE. A skinned
  primitive is placed by its joints alone, as the format says. What is not
  read (sparse accessors, `data:` URIs, morph targets) is named in the
  scene's warnings. `examples/gltf_viewer` shows any file;
  `tests/test_gltf` checks a generated two-bone arm against the arithmetic
  and loads the Khronos Fox. The skin palette holds 96 bones (was 48), a
  Mixamo rig with its fingers.
- Every lamp throws its own shadow by ray (Vulkan, rays on): a point
  light has no shadow map, and the key light's shadow stood in for every
  lamp's, so a night street's figures shadowed nothing. One ray per lamp
  that reaches the pixel, to a spot on the lamp's face
  (`engine_set_lamp_size`), stopped short of the fitting; the rays'
  spirals (penumbra, occlusion, lamps) turn by the golden angle every
  frame (`rayFrame`) so the temporal pass folds them -- the projection's
  jitter, used before, is a fraction of a pixel and turned nothing.
  `tests/test_ray_shadows` checks a lamp's shadow of the ball.
- Sixteen lights a frame (was four), the nearest to the camera picked each
  frame out of every light the scene registers (`core.nearest_lights`) on
  both backends, and a point light past its fall-off skipped before it is
  shaded. `tests/test_lights` registers twenty-one and checks the near one
  still lights the scene.
- `engine_set_ssr(e, on, road_height, strength)`: the wet ground's
  reflection through the engine, kept until the backend is up like the
  fog and the occlusion (set on the renderer before `engine_run` it was
  wiped by the backend's start).
- `examples/zombie_city`: a light under every lamp head its tiles place
  (twenty-one; three at the centre tile lit three lamps' worth of a
  six-hundred-metre street, and the camera stood in the dark end); the
  rays on where the device traces, with the sun's half degree, the lamps'
  shadows and the occlusion by ray; the wet road through `engine_set_ssr`
  at the road's own height (a plane set between the road and the
  pavement mirrored the pavement too); the temporal pass on. 400 figures
  139 fps, 20,000 at a 28 m near band 54 (113 with the map alone).
- The black hole draws again by default: it makes its engine on OpenGL,
  since its picture is one GLSL fragment shader and the Vulkan backend
  compiles no GLSL at run time -- with Vulkan the default it had been a
  black frame at 150 fps that said nothing (#352). The Vulkan backend now
  says, once per model, when a custom GLSL shader lands on it and is drawn
  with the scene shader instead.
- CI builds and drives the editor on Linux: the workflow fetches aether-ui
  at a pinned commit (`AETHER_UI_REF`) with GTK4 and hands `ci.sh` the
  checkout, so the bounded runs on both backends, the roundtrip scene,
  the name check and the widget driver run on every dispatch. The editor
  had gone unrun in CI for want of the checkout.
- The device sort draws each part of a tier's figure whole: an indirect
  command per part (`AE3D_VK_CROWD_PARTS`, sixteen a tier), with its own
  index count, the sort's count copied into every one. It held one count
  per tier -- the last part bound -- so the zombie's near body drew with
  its clothes' count, a seventh of its mesh, and a fifteen-part glTF
  figure came apart. The half-million figures were measured on that:
  the device row is 78 fps, not 90 (docs/performance.md). Freeing a
  device crowd after its engine is a no-op rather than a wait on a null
  device.
- `tools/bake_impostor --gltf figure.glb [Walk] [out.png] [facing]`: a
  glTF figure's impostor atlas from its file alone, the figure turned to
  face +X first; and the bake discards its first frame, which drew a
  file's embedded texture before it had reached the device (a black
  first cell).
- `examples/gltf_crowd.ae` has the zombie's three tiers: the file's
  meshes, the same decimated (`mesh_decimate`), and the picture beside
  the file when the bake wrote one; the sort on the device where it
  sorts there, on the CPU elsewhere. A hundred thousand of a Quaternius
  figure at 130 fps hidden, with fifteen parts a figure.
- `gltf.bake_bank(scene, animation, frames)`: any animation of a glTF
  file baked into a pose bank in place over its first skin
  (`gltf.bake_root` picks the joint that carries the travel;
  `gltf.animation_duration` the cycle), so a figure from a public pack is
  a crowd without Blender. `examples/gltf_crowd.ae` walks hundreds of any
  figure over a field, every skinned primitive instanced over the bank;
  `tests/test_gltf_crowd` bakes the Khronos Fox's walk and draws thirty.
  Three hundred of a Quaternius (CC0) survivor at 150 fps hidden, on both
  backends alike.
- `skin.skeleton_set_inverse_bind` and `skeleton_settle` take a file's
  inverse binds where `skeleton_bind` would derive them from the pose.
- `native/ae3d_blob.c`: a file as bytes and the little-endian numbers in it.

### Ray-traced shadows

- On Vulkan where the device has `VK_KHR_ray_query`: `engine_set_ray_shadows(e, on)`
  / `AE3D_RAYS=1`. A bottom-level acceleration structure per static mesh
  at upload; each frame the traced models' matrices into a top-level
  structure built before the passes; the scene fragment's ray-query
  variant traces one ray toward the sun per lit pixel and combines it
  with the shadow map, which keeps only what the structure does not hold
  (the skinned, the crowd, points) while the rays are on. The instance
  is Vulkan 1.2 where the loader has it, the device enables the
  acceleration-structure, ray-query, deferred-host-operations and
  buffer-device-address extensions where present, and the descriptor
  layout carries the structure at binding 5 there; nowhere else does
  anything change (`AE3D_NO_RAYS=1` keeps it all off).
  `engine_ray_query(e)` says whether the device traces.
- `tests/test_ray_shadows`: the rays' shadow against the map's on a ball
  over a plane (same depth, same edge, fully lit past a ray's edge),
  off again; skips where the device does not trace.
- Measured: the city's shadow pass at 400 zombies goes from 419 to 246
  draws and the scene from 2.96 to 1.57 ms (one ray in place of nine
  map taps); at half a million, where the crowd stays in the map, the
  rays cost two or three per cent.
- Occlusion by ray: `engine_set_ray_occlusion(e, on)` / `AE3D_RAY_AO=1`,
  with the rays and the occlusion on. Four cosine-weighted rays into the
  hemisphere over every lit pixel, to the occlusion's reach, in the
  screen-space pass's place, folded by the temporal pass;
  `vk.renderer_set_ray_occlusion`. `tests/test_ray_occlusion`: a wall's
  foot darkens by ray, the open ground and the wall's top do not. In the
  city at 720p the opaque pass goes from 1.23 to 1.81 ms and the
  screen-space pass is skipped.
- The sun has a size for the rays: `engine_set_sun_size(e, degrees)` /
  `AE3D_SUN_SIZE=n` (tenths of a degree). Four rays a pixel into the
  cone the sun's disc subtends, on a spiral turned by a per-pixel noise
  and the frame's jitter, so the temporal pass folds them into a smooth
  penumbra: sharp at the caster, soft far from it. `tests/test_ray_shadows`
  holds a wide sun to a half-lit edge and a dark middle, and
  `AE3D_RAY_DUMP=<dir>` writes its frames. About a millisecond of scene
  time in the city at 720p.
- The crowd in the rays: `crowd.device_crowd_rays(dc, far, bank)` builds
  a bottom-level structure per frame of the pose bank from the far
  tier's mesh (`ae3d_vk_pose_blas_create`), and the device sort writes a
  ray instance per kept mesh figure within the shadow map's distance --
  position, heading, the frame's structure -- after the static scene's
  (`crowd_sort_vk.comp` bindings 5 to 7, push constants `crowdRay2`,
  `rayLimit`, `rayFrames`). `ae3d_vk_ray_reserve(count, statics)` sizes
  the room from the sort's written-back count with a margin and zeroes
  it, so unfilled slots are inactive instances; a crowd in the rays
  leaves the shadow map. `examples/zombie_city` puts its horde in;
  `tests/test_ray_shadows` checks a device-sorted figure's ray shadow
  against the map's. Measured: 400 figures unchanged, 20,000 unchanged
  (140 fps), 500,000 from 75 to 60 fps with the whole visible crowd
  traced.

### DLSS

- NVIDIA DLSS through Streamline, on Vulkan: `engine_set_dlss(e, mode)`
  or `AE3D_DLSS=n` (1 performance, 2 balanced, 3 quality, 4 ultra
  performance, 6 DLAA). The runtime is loaded before Vulkan starts and
  its interposer is the loader; the scene is drawn at the mode's render
  size with the textures' mips biased by `log2(scale)` and no
  multisampling; the camera, the jitter and the motion-vector scale go
  in each frame, the colour, depth, vectors and output are tagged, and
  the evaluation sits where the temporal pass was. `native/ae3d_dlss.cpp`
  is built only with `AE3D_STREAMLINE_ROOT` (the SDK); the stub
  otherwise says so. The runtime is not shipped (`AE3D_STREAMLINE` or
  beside the program). Without it the program says why, draws as before
  and the temporal pass stands in. `tests/test_dlss` (skips where it
  cannot run). `AE3D_MSAA=n` sets the multisampling on any scene, and
  Vulkan honours `engine_set_msaa` now (it always took 4).
- Found on the way: an upscaler fed textures sampled at the render size's
  mip could not reconstruct what was never sampled -- the negative LOD
  bias is what took DLSS quality from a soft upscale to the native
  frame's sharpness.

### Render scale on Vulkan

- `engine_set_render_scale(e, scale)` draws the scene smaller than the
  window on Vulkan as it has on OpenGL (it answered 1.0 before): the
  scene's targets -- its colour, depth and motion vectors, the camera
  depth, the reflection and the temporal textures -- at the scaled size,
  the scene pass and the passes over it at that size, the composite at
  the window's, so the frame's cost is the scene's pixels. Post is on
  while scaled, since the composite is the upscale. `AE3D_RENDER_SCALE=50`
  on any scene; `tests/test_render_scale` checks Vulkan beside OpenGL.
  Step 3 of #324: what an upscaler needs to sit on.

### Motion vectors

- Every scene draw writes its pixel's motion vector beside its colour: a
  second colour attachment of the scene pass on Vulkan (R16G16,
  multisampled and resolved with the colour, `ae3d_vk_velocity_texture`),
  a second draw buffer of the post framebuffers on OpenGL. The vertex
  stage carries its clip position this frame and last -- a model from its
  own matrix of last frame (`model_frame_done`, taken at the frame's end),
  the last frame's unjittered view-projection, a crowd figure from its
  walk (`model_set_crowd_walk(m, seconds)`: its pose a frame earlier, its
  position a frame's travel back along its facing, from the pose bank's
  own travel), the sky from the camera's turn -- and the fragment writes
  the difference in texture space with the frame's jitter taken out. A
  merged batch and an instance stream carry the camera's motion alone; a
  skinned palette's previous pose is not carried.
- The temporal pass reprojects by the vector instead of by depth alone, so
  a model that moves, a figure that walks and a camera that turns all find
  their history where it is. Step 2 of #324; DLSS is handed this target.
- Capture channel 3 (`engine_set_capture_channel(e, 3)`, `AE3D_CAPTURE=3`
  on any scene) draws the vector in pixels, 128 for still. While a capture
  channel is read no effect runs over it, on either backend: the post
  chain, the reflections and the occlusion stand down, so the channel's
  numbers are the surfaces' own.
- `tests/test_velocity.ae`: a still cube reads still, a cube moved by half
  a metre reads the move the camera's projection says it made, to the
  pixel, a camera moved reads the slide, and under the temporal pass the
  place a cube left is not its ghost. Both backends.
- Fixed: the Vulkan sky drew its matrices into whichever program's block
  was current; it now draws from the scene's.

### The crowd sorted on the device

- On Vulkan the horde's per-frame sort is a compute pass. The simulation's
  columns -- each figure's position, yaw, phase and tint, eight doubles --
  go to the device as eight floats a figure (`ae3d_vk_crowd_fill`, over the
  job pool, into a buffer in the device's own memory where the host can
  see it), and `crowd_sort_vk.comp` picks every figure's tier by its
  distance to the camera, drops the ones past the cull, builds the instance
  matrix from the yaw and the tier's scale, packs it with the colour and
  the phase into the tier's stream and counts it into the tier's draw
  command -- a workgroup at a time, one atomic per tier per workgroup. The
  tiers draw by those counts, indirectly (`vkCmdDrawIndexedIndirect`), the
  shadow pass too over the same streams with the depth proxy's index count;
  nothing about the sort comes back to the CPU. `crowd.device_crowd_new
  (capacity)` makes one (null without Vulkan up, and the caller keeps
  `crowd_tiers`), `device_crowd_bind(dc, model, tier)` names the models
  that draw its tiers, `device_crowd_update(...)` says each frame which
  figures and where the camera is, `device_crowd_count(dc, tier)` reads
  back what the last frame sorted. `zombie_city` uses it on Vulkan
  (`AE3D_GPU_CROWD=0` keeps the CPU sort); the OpenGL path is unchanged.
  Half a million: the CPU's update 8.3 to 3.1 ms, its frame 3.3 to 2.4,
  the matrices, the sort and the upload gone from it; 75 to 90 fps.
- The horde's teleport audit -- every figure's step against its last
  position, every frame -- is native and over the job pool
  (`crowd.crowd_audit`), 1.3 ms to 0.4 at half a million. Its timing had
  also been counted into the sort's: the "sort" column of the job pool
  table was the sort plus the audit.
- `tests/test_device_crowd.ae`: the device's counts against the CPU
  sort's over the same figures, band by band and with a cull; a figure
  drawn where it stands, turned by its yaw, tinted by its colour, by its
  tier's model, and gone when culled; and no device crowd without Vulkan.

### Billboards: the weather as sprites

- `model_set_billboard(m, mode)`: a point-instanced model's mesh turned to
  the eye at every point -- 1 upright, spun about the world's up alone, so
  a streak of rain stays a streak; 2 full, tipped to face the eye as well,
  for a flake. The model's scale stays, its rotation gives way to the turn.
  Both backends, the shadow pass included.
- The weather draws a quad with a soft disc for its texture (alpha falling
  off to nothing at the edge, made from the engine's own numbers and
  registered by name), a drop an upright billboard and a flake and a mote
  full ones: a drop is a line with soft ends, a flake a dot, where they
  were cubes that read as squares up close (#328).

### The critique, in Aether

- `tools/critique_scene.ae` replaces `scripts/critique_scene.py`, standard
  for standard with the same numbers: texel density, relief, the figure's
  geometry and proportion, the frame's budget, the surfaces' normal maps
  and occlusion reaching the picture, a held frame holding still, the
  night's contrast and warmth, the pools of light, the wet road's analytic
  and screen-space reflections, the figure's reflection, bloom and shadows
  proven by turning them off, the fast pose reading true, spins, the
  head's carriage, the walk's planted feet, the weight shift, the strike,
  the silhouette and its islands, the leg count. `ci.sh` launches the
  scene and attaches it, as it does `measure_scene`. One standard, the
  head's follow-through, was found never to have measured anything (both
  bones' sway is zero and the Python ranked noise) and is a note until the
  clip has one (#333). What is left of #272 in Python is the editor driver
  and the Blender scripts.
- `tests/test_ssr`: the reflection mirrors a glowing block onto the wet
  plane under it, on Vulkan; the first check of the reflection outside the
  critique, and of the resolved depth it reads.
- `AE3D_VK_SHOW_DEPTH=1` composites the resolved scene depth in place of
  the frame on Vulkan, for looking at what the occlusion, the reflection
  and the water read.

### The frame as characters, in Aether

- `tools/ae3d_view.ae` replaces `tools/ae3d_view.py`: `frame.grid` as a page
  of characters, densest where the frame is brightest, stretched over the
  range the frame uses (`--absolute` for 0 to 1), `--coverage` for how much
  of each cell is not the background, `--isolate <model>` for one model's
  silhouette, `--region x,y,w,h` for a window at a cell a pixel, `--time`
  to seek the clips first. The kept connection it was built on is
  `ae3d.probe`; what is left of #272 in Python is the critique and the
  editor driver.

### Temporal anti-aliasing

- `engine_set_taa(e, on)` (`AE3D_TAA=1` in any scene), on both backends:
  the projection is nudged a fraction of a pixel each frame through an
  eight-frame Halton sequence, and a temporal pass folds every frame into a
  history found by reprojection -- each pixel's world position from the
  scene's resolved depth, projected with the last frame's view-projection
  -- and held to the range of the pixel's neighbourhood this frame, so a
  walking figure trails no ghost and a sky pixel holds its place as the
  camera turns. The result is what the post chain composites and the next
  frame reads back. `tests/test_taa`: a tilted bar's edge is graded finer
  (496 to 605 in-between pixels at 320x240) with the same light overall, holds still once the history has filled, and
  leaves no trail under a panning camera, on OpenGL and Vulkan alike.
  What DLSS (#324) still needs on top is
  per-object motion vectors; the jitter, the history and the depth
  reprojection are in place.

### Wet surfaces

- `engine_set_wetness(e, amount)`: rain on the scene. Every surface that
  faces up goes darker, smoother and a mirror at a grazing angle -- the
  lamps smear down a wet road the way they never do a dry one -- and walls
  hardly change, water runs off them. The weather sets it with the rain
  (half to full with the intensity), full in a storm, dry under snow and
  dust, and takes it back with the clear. `tests/test_shading_knobs` holds
  it to changing the picture. The rain's fog is the colour its overcast
  sky comes to at the horizon.

### The job pool

- `native/ae3d_jobs.c`: a pool of worker threads, one for every hardware
  thread but the main one, and `ae3d_jobs_for(count, grain, fn, ctx)`, a
  parallel for over a range in runs of `grain` elements taken by the
  workers and the caller alike, done when it returns. The engine starts
  it at `engine_new` and stops it at `engine_free`; `AE3D_JOBS=n` sets
  the thread count, 1 is the main thread alone; `engine_jobs(e)` says how
  many workers there are.
- Every pass of the crowd runs over it: the heading, the shove (each
  figure gathering its neighbours' push, so no two threads write one
  entry), the step, the sort into tiers (counted a run at a time, offsets
  summed, written a run at a time in the order one thread would have
  written), the instance matrices from positions and yaws, the Vulkan
  instance stream's packing, and the weather's particles. Half a million
  zombies: 36 to 79 fps, the simulation 24 to 8 ms a frame.
- `crowd_tiers`: the sort by distance into three compacted tiers -- near,
  mid, far, with a cull -- in one pass, in place of a sort and a second
  sort over the first's far output. `crowd_bucket` is the two-tier case
  of it.
- `AE3D_PERF=1` says the update's time (`update_ms`, the behaviours' own:
  the simulation) beside the device's, and `zombie_city[perf] sim` names
  its passes every 120 frames. `scripts/perf.sh` has the column.
- `tests/test_jobs` runs a frame of the crowd alone and over a pool of
  three and holds the two to each other: the sort the same in the same
  order, the heading to the bit, everything the shove touches to a hair.

### Impostors: the horde's far tier as pictures

- `model_set_impostor(m, cols, rows, width, height)`: a crowd model whose
  mesh is one upright quad draws each instance as a picture -- the cell of
  its texture for the angle the camera sees it from (`cols` views around
  the figure, the first from its +X, which is the crowd's yaw zero) and the
  frame of its walk its phase is at (`rows`). Two triangles a figure instead
  of a hundred and sixty-eight. The picture is not a lit photograph: its
  texture is the figure's **albedo** and its normal map the figure's
  **normals** in its own frame, turned into the world by the instance's
  facing, and the scene's lights, shadow, fog and occlusion shade it the
  way they shade the mesh beside it. `zombie_city` draws every zombie past
  `AE3D_IMPOSTOR` metres (80; 0 turns it off) this way: 100,000 zombies 95
  to 138 fps, 500,000 21 to 35, with the scene's GPU time 28.8 to 8.0 ms
  and the shadow pass 19 to 2.7.
- `tools/bake_impostor.ae` bakes the atlases out of the figure with the
  engine: the gait baked in place into a pose bank, one instance of the
  figure at the origin, a camera on a circle around it through a narrow
  lens, a frame a cell, read back through the capture channel, keyed on a
  colour nothing in a zombie is, the colour bled under the transparent
  pixels so the filter and the mip levels have nothing of the key to blend
  in. `impostor_<figure>.png`, `_normal.png` and a `.json` with the grid
  and the metres a cell spans, beside the export. `ci.sh` rebakes one and
  holds it to every cell filled.
- `engine_set_capture_channel(e, n)`: the frame drawn as every surface's
  albedo (1) or its world-space normals (2), for a bake to read back and
  for a look at why a surface lights the way it does. `tests/test_impostor`
  reads the cells, the cutout, the turn, the phase, the tint and the
  normals back as exact colours on both backends.

### The scene's depth comes from the opaque pass

- On Vulkan the camera depth the occlusion, the reflection and the water
  read is the frame's own depth, resolved after the opaque draws (the
  nearest of the samples a pixel) into the target they sample; the scene
  pass is drawn in two halves around the resolve. The prepass that drew
  every visible triangle again, depth only, is gone: 20,000 zombies at
  `AE3D_NEAR=28` 100 to 120 fps, 1,193 draws to 419, and the depth is of
  what was drawn, pictures of figures included. (#320)
- The near tier's depth proxy (`model_set_depth_proxy`) is the shadow pass's
  alone now. Read by the occlusion at the pixel, the proxy's silhouette --
  the far tier's, a few pixels out from the mesh's -- put the figure behind
  into the occlusion of the figure in front, and every near zombie on
  Vulkan wore the one behind it as a ghost. The shadow map, soft, never
  showed it.
- A Vulkan crowd whose only change in a frame was its phases stood still:
  the phase rides in the one instance stream with the matrix and the
  colour, and a phase set alone did not send it. It does.
- OpenGL honours the depth proxy (#321): a vertex array binds a mesh to an
  instance stream, so a model with a proxy keeps a second one over the
  proxy's mesh and its own matrices, colours and phases, made the first
  time the shadow pass needs it and freed with the model. `tests/test_depth_proxy`
  holds both backends to it: a thirty-metre sphere's shadow through a
  proxy a fifth its size is 56 pixels of the 1,330 its own mesh casts.

### The crowd's depth passes

- `model_set_depth_proxy(m, proxy)`: the shadow map draws the proxy's mesh
  with the model's instances and pose. The city's near tier casts from its
  far tier's 168 triangles instead of its 26,636: 2,000 zombies 30 to 68
  fps, 20,000 at `AE3D_NEAR=28` 54 to 91, on Vulkan.

### The front page

- The README is a front page: a feature table, the scenes, a quick start, a
  program, the pipeline, the editor, the examples and the layout, with the
  reasoning behind each feature moved to `docs/rendering.md` and the
  pipeline, the critique and the agent channel to `docs/pipeline.md`.

### The editor's look

- A neutral dark palette: greys three tones apart for the viewport's ground,
  the panels and the bars, one accent, no blue cast. The viewport opens at
  the width the panels leave it, so the right panel and its scrollbar sit
  inside the window. With aether-ui's win32 fixes (widgets on the ground
  behind them, a scrollview that scrolls, hidden sections taking no room,
  labels refitting when their text or font changes) the editor on Windows
  looks as it does on the other platforms; `docs/editor-windows.png` is the
  window as captured there.

### The viewport

- The scene is drawn straight into the window's own GL context. aether-ui had
  no GPU surface when the editor was written, so the scene went into a
  framebuffer of its own, was read back to the CPU every frame and blitted into
  a canvas. It is now a GPU view with the gizmo canvas over it: the canvas keeps
  every event it had, so orbiting, picking and dragging are unchanged, and what
  it no longer carries is a copy of the scene.

  The resolution is the bigger half of it. A canvas holds the canvas's own point
  size, so the blit path rendered the viewport at 880x622 on a display whose
  viewport is 1760x1244 pixels and let the window scale it up. The picture is
  the screen's now.

  Two sizes follow, and mixing them is a bug nothing else would catch: the
  renderer and the camera work in the framebuffer's pixels, the gizmo is drawn
  and picked in the canvas's points.

  Where there is no GPU surface, and for Vulkan, which cannot draw into a GL
  context, the old path is what runs. `ci.sh` reads `viewport_path` out of the
  editor's report and fails an OpenGL run on macOS that took it, because
  falling back is invisible in a picture, and checks the snapshot's size against
  the size the scene was rendered at for the same reason.

  A snapshot on this path is the frame the renderer produced, so it no longer
  carries the gizmo drawn on the canvas above it.

### Building

- Building the editor first in a clean checkout failed on zlib. The editor and
  `build.sh` each decided for themselves what the engine library links, and
  disagreed: `build.sh` names zlib once, and the editor dropped it whenever the
  Aether toolchain's own libraries already carried it, which is a rule that
  belongs to a program's link line rather than to a library's. Every CI run
  passed because `build.sh` always ran first and left the library built, so the
  editor never linked it. `ci.sh` now removes the library before building the
  editor, which is the run that was missing.

### Scripts

- The engine's C is built once into `build/libae3d_native`, and every program,
  the editor and every script links it. Each platform had a different way in:
  macOS resolved a script's engine calls out of its host at load, Linux did the
  same once the host was told to export its symbols, and Windows, where a DLL
  has to resolve everything, linked the engine's objects into the script. That
  last one gives a script its own copy of the engine's C, GL loader state and
  all, which works for a script that moves a model and gives a script that
  draws a function table nothing ever filled in. One library is the same
  arrangement everywhere and leaves one copy of that state.

  `ci.sh` checks the invariant rather than the arrangement. Off Windows a built
  script must define none of the engine's C itself, which reads 56 when
  `native/ae3d_mesh.c` is linked into one. On Windows it must import the engine
  library, because linking against an import library leaves a thunk under the
  imported name and a DLL exports those too, so there every imported call reads
  as a definition.

### The agent channel

- A channel stopped while a client is still attached no longer hangs the
  program. Waking the serving thread by giving its `accept` a connection
  covered the case where nothing was attached; where something was, the thread
  is inside `recv` on the client instead, and closing that socket under it does
  not wake it on any platform. An editor that someone had attached an agent to
  hung on the way out, on macOS as well as Linux.

  Every wait in the thread is bounded now, so it notices the stop itself,
  closes the client it was serving and returns. `ae3d_agent_stop` asks, waits
  for the thread, and only then closes the sockets, rather than closing them
  under a thread still using them. The wake-by-connecting is gone with it: one
  mechanism that covers both cases rather than two that each cover one.

- `tests/test_agent_attached` is that case. Every other agent suite closes its
  socket before the engine comes down, so none of them could reach it.

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

### The agent channel

- A control and telemetry channel a program drives. `AE3D_AGENT=<port|auto>`
  opens newline-delimited JSON on loopback; every answer carries back the id of
  the request it answers. Reads the scene, models, materials, lights, camera and
  animations; writes transforms, materials, lights and the camera; loads and
  saves scenes; holds a frame still, steps an exact number, and captures one.
  Unset, it opens no socket, starts no thread and allocates nothing: the frame
  pays one load of a global. `tests/test_agent_cost` measures the open channel
  at 11ns a frame against an inactive drain of 0.6ns, and
  `scripts/check_agent_gating.sh` fails a build that reaches the agent from the
  frame loop outside that gate.

- Pixels as data. `frame.capture` reads the finished frame into the engine and
  `frame.pixel`, `frame.region`, `frame.hold` and `frame.diff` answer questions
  about it, summarised where the pixels are rather than shipped as JSON.

- `trace.model` follows one model from the Blender object it was authored as to
  the pixels it produced -- source, asset, mesh, node, animation, visibility,
  pixels -- and names the first stage where it stopped being right. Visibility
  says a model should be on screen; pixels says whether anything was drawn where
  it projects.

- `world` returns every entity and the relations between them in one answer, so
  a client stops joining four ops by hand on indices that shift.

- The editor answers the same channel from its own loop. A held frame there does
  not advance the simulation but still redraws, since an editor that skips
  unchanged frames would otherwise stop producing the frames an agent held it
  still to look at.

### Animation

- Clips, channels, samplers and playback, in glTF's shape because that is what
  Blender exports. Translation, rotation and scale channels; step, linear and
  cubic interpolation. Rotations lerp along the shorter arc, so a pair stored on
  opposite sides of the hypersphere turns the short way.

### The Blender pipeline

- `tools/blender/ae3d_export.py` exports geometry, materials and animation from
  the command line, with a manifest recording the source hash and a stable id
  per object. The same file exports to identical bytes twice.

- Bezier easing, which Blender uses by default, is carried as cubic segments
  rather than flattened to linear. Each clip records what Blender itself
  evaluated the curve to, and `tests/test_assets` holds the engine's sampler to
  those values: it agrees to 1.4e-4.

- Materials carry metallic, roughness and the base-colour texture. The MTL
  loader had read `Pm`, `Pr` and `map_Kd` all along and the exporter wrote none
  of them.

- `tools/blender/ae3d_agent_server.py` opens the engine's protocol inside a
  running Blender, so one client drives the modelling tool and the engine. Reads
  after a seek are of the evaluated pose.

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

- The agent's trace blamed a model that was plainly drawn. The pixels stage
  read its coverage out of the region node after handing that node to the
  object that answers, and got nothing back: the trace reported the model
  broke at pixels and printed the coverage it had just ignored in the same
  answer. Read first, hand over second.

- Reading an answer off a socket leaked the whole buffer. `std.net`'s
  `tcp_receive_raw` returns an owned buffer typed as a borrowed one, so nothing
  frees it and calling `string.free` on it changes nothing, which is the part
  that makes it hard to find. Filed as aether-lang-dev/aether#1987; the two
  tests that read answers use `tcp_receive_n_raw`, which says who owns what.
  595K and 1.17M of leaked bytes down to 12K and 16K.

- A player borrows its clip and the engine borrows a model, and neither said
  so. Both contracts are written where they are defined now, and the two tests
  that relied on guessing them free what they built. `test_trace` also never
  freed the model it deliberately orphans, which is the object that check
  exists to trace.

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

- `model_set_mesh` gives a model different geometry. A terrain that changes
  shape wants to stay the same object: the same pointer, the same place in the
  scene, the same entry in the undo history. Replacing the model instead moves
  the selection and turns one change into two steps. The mesh a model was
  holding is destroyed and the new one adopted, so a model still owns its mesh
  and there is never more than one owner.

  The renderers build their GPU state when a model is added and key it off the
  mesh, and core does not know which backend is holding a model, so the caller
  removes, sets and adds. That is written next to the function, and
  `tests/test_model_mesh.ae` drives that sequence and the setter on its own.

- Render scale is a backend capability. It landed on the OpenGL renderer only,
  so a scene that used it was silently OpenGL-only, the Vulkan path had no way
  to say whether it supported it, and the black hole's quality ladder reached
  past the abstraction to get at it. `set_render_scale` is in the vtable now,
  with `engine_set_render_scale` forwarding it.

  It answers the scale actually adopted rather than the one asked for. Vulkan
  builds its attachments against the swapchain extent and cannot yet draw the
  scene smaller than the window, so it answers 1.0 and says so: a silent no-op
  would have a caller believe a rung of its quality ladder helped when nothing
  changed, and stop stepping. The black hole prints what the backend really
  did.

- Window, input and timing over GLFW.
- Main loop with a fixed-step accumulator, frame pacing and `AE3D_FRAMES`,
  which caps any program at a frame count so every example is also a smoke test.

### Tooling

- The editor driver checks the panels are laid out on one grid: no two rules
  with nothing between them, every stack's button rows starting at the same
  edge, and no visible control without a size. Both faults these describe were
  found by measuring the widget tree by hand and neither by looking at
  screenshots, over several passes across the same panels.

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

### Continuous integration

- A suite, example or benchmark that hangs now fails the step by name. The
  Linux leg held a runner for three and a half hours with nothing in the log to
  say where it had stopped, and the job had no limit of its own to end it. Each
  run is bounded where coreutils' `timeout` exists, and the workflow job stops
  at 45 minutes against a whole run that takes twenty on the slowest platform.

### Ownership

- The engine borrows a model, a player borrows its clip, and the assets module
  keeps a record of where each loaded model came from. That last one kept every
  model it had ever described reachable, which hid the fact that nothing was
  freeing them: `assets.provenance_release` ends the table, `provenance_forget`
  drops one record when its model dies, and the suites and examples that load a
  manifest now release what they made. The leak check reads the same on this
  branch as on main.
- `agent.context_free` frees the queue of reads still waiting on a frame. A
  program that stopped mid-exchange lost the queue and everything in it.

### Editor

- More than one object can be selected. A click selects one, shift or command
  adds to the selection, and an edit reaches everything in it: the property
  paths have always written to the set, and what was missing was any way to put
  a second thing in it. The toolkit could not report a modifier until
  aether-lang-dev/aether-ui#99, so a plain click could not replace a selection
  the way every editor expects.

  The selected flag lives in the slot that already holds each object's script.
  A fourth list beside the models, the components and the scripts would be a
  fourth thing to keep in step, and those three going out of step has already
  caused one bug.

- The row callback no longer fights the editor. It fires both for a person's
  click and for the editor moving the highlight itself, and the second case
  read no modifier held and replaced the selection that had just been made.

- The viewport is drawn by the frame timer alone. A property change repainted
  it by hand, rate-capped, because a timer scheduled in the default mode did
  not fire during a drag (aether-lang-dev/aether-ui#97, fixed upstream): the
  viewport stopped the moment you grabbed the control meant to move it.
  Measured through a rapid drag, 62 fps before and after.

- A script can set itself up. `script_start` runs once when a script is
  attached, on the object it was given, which is what a script that needs to
  know the size or place it started from has to have: measuring it every frame
  measures its own last answer. `resources/scripts/pulse.ae` uses it.

- A script rebuilt while the editor is open is picked up. The editor compiles
  nothing; it watches the library the build step writes, waits for it to stop
  changing so a half-written one is never opened, and starts the script again
  on everything carrying it. Watching the library rather than the source is
  what leaves the last good behaviour running when a save has an error in it.

- **New script** writes a template into `resources/scripts` and says where it
  went. The shape of a script is a thing to be given rather than remembered,
  and the driver builds whatever the button writes, because a starter template
  that does not compile is worse than none.

- The check that a script moves what it is attached to counts every way a
  script can move something. It compared position and one component of the
  rotation, both of which a script that scales leaves alone, so `pulse` was
  reported as doing nothing at all.

- A behaviour is a script you assign, not a case in the editor. It was four
  hardcoded ones, so the editor knew how to spin, bob and orbit and a project
  could have no others. A script is now an ordinary Aether source file in
  `resources/scripts` with `script_update` in it, compiled into a shared
  library and opened at runtime, and the buttons in the BEHAVIOUR section are
  the files that are there: adding a behaviour is adding a file. Spin, bob and
  orbit moved out of the editor into scripts of their own, so the editor no
  longer contains any behaviour code.

  The scene records the assignment by name and gives it back on load, and a
  scene naming a script the project does not have gets none rather than a wrong
  one.

- A terrain is blocks or smooth, and voxels are one of the two rather than what
  a terrain is. Blocks draw a cube per filled cell; smooth meshes the same
  field into one surface, which the engine has been able to do since
  `world_build_surface` was written and the editor could not reach. Switching
  keeps the same object: the model leaves the backend, changes its geometry and
  comes back, so its place in the scene, its selection and its undo history are
  all where they were.

  Which style a terrain has is read off the model rather than remembered beside
  it. Blocks are instanced and smooth is not, so the style is a fact about the
  model; a fourth list beside the models, the components and the scripts would
  be a fourth thing to keep in step, and those three going out of step has
  already caused one bug.

- The title says what the selection is now. A terrain that changes shape keeps
  its name and changes its geometry underneath, and the title was written once
  on selection: it read twelve triangles while the model held five thousand.

- The colour swatch follows the selection. Nothing refreshed it when the
  selection changed, so the three sliders under it moved to the new object and
  the colour above them stayed on the old one: selecting a terrain left the
  sphere's blue sitting over an object with no material at all.

- A seed is spelled as a whole number. Every row writes two decimals, which
  reads well for a measurement and put two digits of nothing on the end of a
  seed, pushing it out of its box.

- The material rows no longer read through a null pointer. They dereference a
  model's material, and a model is not obliged to have one: a voxel terrain is
  built out of a bare cube and carries its colour per instance, so moving the
  roughness slider with one selected took the editor down. Nothing had ever
  selected a model without a material before, which is why it had not been
  seen. Every model the inspector writes to gets a material first.

- A terrain is one object whose shape is a property of it. The panel had five
  buttons, one per biome, which made the editor responsible for knowing what a
  desert is and put five entries in a list that should have one. There is a
  Terrain entry now, and a TERRAIN section on the object with the five shapes
  and its seed, which is where Unreal and Unity both put a landscape's
  settings. Changing either fills the same world again and rebuilds the
  instances on the model that is already there, so the object keeps its place
  in the scene and one change is one undo step.

- A scene has a sky, and the editor can set it. The scene format has carried
  one since it carried anything and the editor had no control over it, so every
  scene it saved recorded a colour nobody could choose and every scene it
  opened was drawn on the editor's own grey. Three channels in a SKY section,
  written into the file and read back out of it, and the renderer clears to it.

  A scene saved before this opens black, because that is the sky it recorded:
  the editor wrote the format's default into every file it ever saved. Setting
  it takes three clicks and is then remembered.

- The bar waits for a measurement rather than for a second frame. Those were
  the same thing until a gap longer than a quarter second stopped counting as a
  frame: a run that opens a scene can draw its first frames further apart than
  that, and the bar then wrote the zero the average still held.

- A gap longer than a quarter second is a stall, not a frame rate. The
  simulation step already ignored one for the same reason; the frame counter
  did not, so the first gap of a run that opens a scene spans reading it off
  disk and the bar opened on 4 fps while the editor was drawing at sixty. Found
  by the check on the first rate shown, on the leg that saves and loads before
  it runs.

- A scene remembers its post chain. The scene file has carried fxaa, bloom and
  the bloom parameters since it carried anything, and the editor filled none of
  them and read none of them back: every scene it saved recorded the defaults,
  so a scene saved with bloom on came back with it off, which is what the file
  had those fields for.

- A model added after the shading was chosen carries it. `apply_all` walks the
  models that exist when it runs, so a cube added afterwards was lit by
  whatever the shader defaults to while the panel said otherwise, and pressing
  Quality then adding an object gave an object that was not at quality. In the
  components scene the panel and the models disagreed on 29 settings.

- A loaded scene's shading is what the switches show. The file carries each
  model's uniforms, so a scene brings its own shading with it; the panel knew
  nothing about it, and the next switch pressed applied the panel's answer to
  every model and threw the loaded one away.

- Attaching a behaviour can be undone. It recorded nothing, so undo after
  choosing Spin stepped back through whatever came before it and put that back
  instead, which is the same fault the gizmo drag had and the same one the
  water rows had. A behaviour is a property of a model as far as undo is
  concerned, and travels as a property step in a slot no row uses.

- Setting a behaviour no longer refreshes the camera rows. Field of view, near
  plane and far plane have nothing to do with which script a model carries; the
  three calls were left over from something else.

- The status bar does not open on a rate the editor is not running at. It wrote
  its first sample before a single frame interval had been measured, and the
  running average it reports started from nothing, so the first thing the
  editor told anyone was 0 fps and then 1 and then 2 while frames were arriving
  sixty times a second. Nothing is written until there is something true to
  write, and the average is seeded with its first measurement rather than
  climbing to it.

- Sections fold. The inspector is eleven of them in one column, which is more
  than fits in the window, and a person working on a material should not have
  to scroll past a camera to reach the next one. Clicking a header folds what
  is under it and turns the caret, which is how all three of the editors this
  borrows from do it. The water section, which is hidden whole when a scene has
  no water, is now hidden as a section rather than a row at a time.

- The inspector has a SHADING section. The engine's shader has carried
  clearcoat, sheen, ambient occlusion, volumetric light, global illumination
  and soft shadows since it was written, and the editor offered no way to reach
  any of them: the whole advanced half of the renderer was three preset buttons
  wide. Each is a switch, and a preset loads its own answers into them, so a
  preset is where the settings start rather than a mode the scene is locked
  into.

  They apply to every model, the way the presets already do. Giving each model
  its own would mean a fourth list beside the models, the components and the
  scripts, all of which have to move together.

- The gizmo self-check counts drags that moved nothing apart from drags that
  could not be undone. A drag that never reaches the model leaves the model
  where it started, and the undo after it steps back through whatever came
  before and moves it away, so the count of drags that did not come back was
  also the count of drags that never happened. Told apart, one number says the
  gizmo did not pick or the drag arithmetic gave nothing and the other says the
  history did not put it back, which are different bugs in different files.

- The kind of each object in the scene list is a glyph that looks like the
  thing rather than punctuation. A pipe, a tilde, a hash and an asterisk say
  nothing about what they stand for and read as stray keystrokes down the edge
  of the list. Every editor this borrows from puts a small icon there, and a
  diamond, waves, a grid and a star are the nearest a text list gets.

- Buttons carry an edge. A flat fill on a panel one step darker reads as a soft
  block rather than as something to press, and a panel of ten of them had no
  lines in it at all. The fields beside them already had a border, so the two
  kinds of control now agree.

- The behaviour row is its own readout. It carried a caption and a word above
  four identical buttons, which said the same thing twice as soon as the
  buttons could say it once.

- A debug print shipped, in paint_segments, writing to the editor's console
  where it looked like a message the editor meant to write. It is gone, and
  `ci.sh` now refuses one.

- A row of choices says which one is on. The behaviour buttons and the
  rendering presets were four and three identical bars: they showed what could
  be chosen and never what was, so the only way to find out which preset was
  running was to remember pressing it. The one in effect now wears the accent,
  the way the transform modes over the viewport already did.

- The window has a menu bar: File, Edit, Add and View, with accelerators on the
  ones a person expects to press. Every editor this borrows from has one, and a
  window of panels with no menu reads as a demonstration of a toolkit rather
  than as an application: the menu bar is where someone looks first to find out
  what a program can do. Each item calls the same function its button does, so
  the three primitive builders moved out of their button closures and there is
  one action behind two ways of reaching it. The transform modes carry no
  accelerator, because the viewport already answers W, E, R and F.

- Section bars and rules reach both edges of their panel. A section header was
  a bar inset fourteen pixels each side, which reads as a chip laid on a card
  rather than as the header of a docked panel, and it is not what any of the
  three editors this borrows from draws. The gutter moved from the panel's stack
  to the rows, so the bars and the rules span the column while everything in
  them still lines up, and the scene list highlights the selected object edge to
  edge. A caption sits in a row of its own, because a text widget insets its
  children and not itself.

- A panel is one frame, not two. The stack inside the scroll view carried the
  same border as the scroll view around it, and a stack is only as tall as what
  it holds: the left column drew a rule across itself under the last button
  with nothing under it, so the panel read as a card that stopped short of the
  window. The frame belongs to the scroll view, which is the full height.

- Every bounded setting has a number box beside its slider. A slider is worth
  several hundredths of a value per pixel, so there was no way to ask for
  exactly 0.5 metallic or a field of view of 45, and the number printed beside
  it was dead text that looked like something to click. Typing moves the slider
  and dragging writes the box, and every value in the inspector is now entered
  the same way. Undo moves the slider back with it, which it did not do before:
  a replayed property change only rewrote the number.

- The water section hides its rule along with itself. With no water in the
  scene the rule above its heading stayed, so the panel drew two lines one
  after another with nothing between them.

### Tooling

- Every editor run is headless again, so a run of `ci.sh` opens no windows at
  all. It hung part way through under the flag, which was
  aether-lang-dev/aether-ui#123 and is fixed upstream.

- The driver presses menu items. It could not before: the closure ran on its
  own HTTP thread rather than the main queue, so an item that adds a model
  touched the GL context off-thread and took the editor down
  (aether-lang-dev/aether-ui#116, fixed upstream).

- A driver route that answers 404 is a note rather than the end of the run. One
  unreachable route aborted the script and took the twenty checks after it.

- The colour chip's readback is the toolkit's `ui.styled_bg` rather than a
  local declaration of the same entry point (aether-lang-dev/aether-ui#109,
  fixed upstream). It now reports the colour the layer paints, so the chip
  check catches a chip that is never painted; it did not before.

- Waiting for a file to be saved waits for it to be written again, not for it
  to exist. The second save in a run leaves the file already there, so the wait
  returned at once and the Load that followed could read what was on disk
  before rather than what had just been asked for. It passed almost every time
  and failed once inside a full run, which is the worst kind of check to have.

- `ci.sh` prints the driver's failing lines rather than the first twenty of its
  log. The driver runs more checks than that now, so a failure two thirds of
  the way down was reported as a wall of ok with no reason in it, and the run
  that found the race above could not say what had gone wrong.

- The report records the first frame rate the bar ever showed, and `ci.sh`
  refuses an implausible one. A driver cannot check this: by the time anything
  can ask, the average has climbed to something plausible, and a check on what
  the bar says now passes against a bar that opened on nothing. The first
  version of the check did exactly that and passed its own sabotage.

- The report says how many shading switches reach no model, and the driver
  presses one. A switch that sets a global and reaches nothing looks exactly
  like one that works, because the only witness is a frame nobody compares.

- The driver checks that choosing a behaviour moves the highlight, rather than
  that a particular button is blue. The tree reports the colour a widget was
  given and not the colour it has (aether-lang-dev/aether-ui#111), so a reading
  that never changes would pass against a highlight painted nowhere.

- The driver checks that every action is on a menu. It does not activate one:
  the driver runs a menu item's closure on its own HTTP thread rather than
  bouncing it to the main queue the way it does every widget route
  (aether-lang-dev/aether-ui#116), so an item that adds a model touches the GL
  context and segfaults.

- The driver finds a section by climbing to the widget that sits in the panel
  rather than by assuming the caption is a sibling of what the section holds,
  and reads a list row's name from anywhere under the row.

- The layout audit refuses a frame drawn inside a frame of the same colour.
  The inner one is shorter than the panel around it, so its bottom edge lands
  in the middle of the column as a rule with nothing under it.

- The editor driver reads visibility up the parent chain. A section is hidden
  by hiding its rows, and a hidden row's children still report themselves
  visible with whatever geometry they last had, so the water settings sat at
  the top of the inspector by their coordinates while being nowhere on it and
  the driver typed into one of those instead of the position it meant.

- `ci.sh` gates the editor build on warnings. Every other build in the file
  was already gated, so an unused variable in the largest Aether source in the
  repo went through without a word.

- Every button in the left panel shares an edge. A pair of buttons sat in a row
  that added its own inset over the panel's, so it started fourteen pixels
  right of the full-width buttons above and below it and ended fourteen short
  of the panel on the other side. The same control had two edges depending on
  whether it had a neighbour.

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
