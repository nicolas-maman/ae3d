### The Vulkan renderer's readback in Aether

- Reading a frame back moves from `native/gpu/vulkan.c` to `ae3d.vkreadback`
  (#402, slice 2), on `contrib.vulkan.vk`. This covers two readers: every
  offscreen frame, which is how the tests and the backend parity check see
  the Vulkan frame, and a windowed frame when a capture is asked for, which
  is the agent's grid and pixel reads and snapshots. The buffers, one per
  frame in flight, are `ae3d.vkhost`'s: host-readable, mapped for good,
  now shared with the meter, whose own copy of them went. `vulkan.c`'s frame
  hooks are a list, so more than one module records into the frame. The C
  lost its staging buffers, its capture state and its readback functions.
