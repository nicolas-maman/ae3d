### Shadows as dark on OpenGL as on Vulkan

- An OpenGL shadow let through 0.32 of the direct light, a strength picked
  when a shadow still darkened the ambient too; Vulkan's unconfigured models
  shade with `rendering`'s defaults, 0.08, so the same shadow was four times
  as bright on OpenGL (#453). Both now take `rendering.SHADOW_INTENSITY` and
  `SHADOW_SOFTNESS`: `explain.model` reads the cube under the roof at 0.297
  on both, `dark: in the shadow of roof`, where OpenGL read 0.411 and called
  it lit. `tests/test_agent_explain` now requires that verdict on both.
