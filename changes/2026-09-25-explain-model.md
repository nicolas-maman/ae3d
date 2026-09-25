### Why a model is dark or the wrong colour: `explain.model`

- A new agent-channel request, `explain.model` (`id | object | index`, as
  `trace.model` takes), answers why a model on screen looks the way it does
  (#416). It walks six stages with their numbers -- the material (linear
  albedo, metallic, roughness, alpha), the texture (bound, loaded, and its mean
  colour from its texels read again from its file), the light (every light
  onto the model's top and onto the face the camera sees at its centre, as the
  default shader works it: temperature, fall-off, the reach past which the
  shader skips a lamp, a spot's cone, the back fill, and the key light's
  ambient), the shadow (a ray from its centre and its top toward the key light
  against every other model's triangles, naming the one in the way), the
  exposure (the material's and the eye adaptation's), and the pixels (the mean
  colour about its centre beside what it would show fully lit) -- and gives a
  verdict naming the first stage that accounts for the look: `dark: in the
  shadow of roof`, `dark: no direct light reaches it and the ambient is
  0.015`, `wrong colour: its texture's mean is (0.8, 0.102, 0.102)`, or
  `as lit`.
- `tests/test_agent_explain` builds a cube wrong in each way and checks the
  verdict names it. On Vulkan the prediction lands within 0.02 of the measured
  luminance for every lit cube (0.614 measured against 0.621 predicted for the
  grey one, 0.29 against 0.287 for the red one, 0.087 against 0.081 for the
  one no light reaches), and the cube under the roof measures 0.297 against
  0.621 unshadowed. On OpenGL the same cube measures 0.614: that renderer
  draws no shadow on it, and the verdict says `as lit; not shown in the
  pixels: in the shadow of roof` rather than blaming a shadow the frame does
  not show.
- The engine hands the agent the frame's exposure and whether the eye
  adaptation is on, so an explanation can say when the exposure is what pulls
  a picture down.
