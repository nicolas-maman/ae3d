### The overlay: text and rectangles over the frame, on both renderers

- `ae3d.hud` (#449, slice 2): a HUD's text and rectangles in the
  window's pixels, y down, asked for every frame and forgotten after it --
  `rect`, `rect_outline`, `text`, `text_outlined`, `text_shadowed`,
  `measure`, `set_font` -- as the engine's (`engine.engine_hud(e)`),
  cleared by the loop after each frame is drawn. Text is drawn from the
  glyphs' distance-field atlas, PT Sans by default, baked at a 6 px spread
  so an outline can grow a glyph by 1.9 px at 24 px text.
- Both renderers draw it in one call after the post chain, blended, from
  one shader source (`VERTEX_OVERLAY`, `FRAGMENT_OVERLAY`): OpenGL in
  `ae3d.gl`; Vulkan from Aether in `ae3d.vkoverlay`, recorded into the
  frame through `vulkan.c`'s hooks, which now run by stage -- the meter,
  then what draws over the frame, then the readback -- so a menu does not
  set the exposure and a capture is of what is shown.
  `tools/generate_shaders.ae` compiles the shader for Vulkan at
  `#version 450`, where `VULKAN` picks push constants. The backend vtable
  gains `set_overlay`.
- `tests/test_overlay.ae`: through each renderer offscreen and through
  the engine on OpenGL, a white rectangle is 255 to its every pixel and
  ends at its edges to the pixel, half-transparent red over the frame is
  red 137 exactly, HELLO at 48 px inks 1509 pixels inside its measured
  137 x 62 box and none beside or below it, an outline grows HUD at 24 px
  from 295 inked pixels to 776, and the next frame, asked for nothing, has
  none of it; Vulkan's frame and OpenGL's differ in no channel. 800 quads
  a frame cost 0.02 ms of CPU. `examples/game_hud.ae` is a frame counter, a
  health bar, a crosshair and a centred prompt over a scene; on Vulkan
  under the validation layer, synchronisation included, no error.
  [docs/ui.md](docs/ui.md).
