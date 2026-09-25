### The Vulkan renderer's textures made in Aether

- Making a texture moves from `native/gpu/vulkan.c` to `ae3d.vktexture`
  (#402, slice 3), on `contrib.vulkan.vk`. It covers all three kinds:
  - a model's image, with its mip chain blitted level by level where the
    format can be filtered;
  - the clouds' 2D and 3D RGBA volumes;
  - the float pose banks.
- The module makes the image, its memory and its view, stages the texels
  in an `ae3d.vkhost` upload buffer, and records the copy, the chain and
  the layout changes into the backend's one-off command buffer. The
  backend adopts the result into its texture table
  (`ae3d_vk_texture_adopt`) with the sampler its kind is drawn with, and
  destroys it with the texture.
- The default texture the backend binds where a draw has none is made
  while the backend starts, through the maker the renderer installs
  first (`ae3d_vk_set_texture_maker`).
- `vulkan.c` loses its three texture creators, its mip chain and its
  layout helpers: 7,677 lines at the start of #402, 6,986 now.
- Measured: `test_backend_parity` gives the same numbers as before,
  channel for channel. `test_texture_swap`, `test_crowd_render` (the pose
  banks), `test_skinned_render` and `test_impostor` pass.
