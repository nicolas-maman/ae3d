# The C under the engine

ae3d is written in Aether. This directory holds the C that remains, and
it is shrinking in order ([#398](https://github.com/nicolas-maman/ae3d/issues/398)).
For a long time every file here had the same reason to be C: Aether had no
32-bit float, and everything a GPU reads is one. The language has `f32` now
([aether#2134](https://github.com/aether-lang-dev/aether/issues/2134)), and
what it made possible has moved: the mesh and instance stores
(`ae3d.geometry`), bone palettes and pose banks (`ae3d.posing`), the OBJ
builder (`ae3d.loader`) and the scene's mesh files (`ae3d.scene`), each the
same bits as the C it replaced. The OpenGL renderer has followed: its
calls are `ae3d.gl`'s, over `ae3d.glapi`, the GL entry points from Aether,
and draw every frame the same to the byte; `native/gpu/opengl.c` and
`capture.c` are gone. The Vulkan renderer is moving now, a part at a time,
through the frame hooks `vulkan.c` calls (`ae3d_vk_add_frame_hooks`) and on
Aether's own Vulkan bindings (`contrib.vulkan.vk`): the frame's light meter
(`ae3d.vkmeter`), the text and rectangles drawn over the frame
(`ae3d.vkoverlay`), reading a frame back, offscreen or captured
(`ae3d.vkreadback`), and making textures -- a model's image with its mip
chain, the clouds' volumes, the pose banks (`ae3d.vktexture`) -- are
Aether, their buffers and memory `ae3d.vkhost`'s.

What was C for other reasons has moved too: the crowd's kernels
(`ae3d.horde`), the flow field (`ae3d.nav`), the weather's particles
(`ae3d.weather`), the clouds' noise (`ae3d.cloudnoise`), the PNG writer
(`ae3d.png`), the file-as-bytes reader (`ae3d.blob`), the script loader
(`ae3d.script`), the agent channel, both sides (`ae3d.channel`, `ae3d.probe`), the job
pool (`ae3d.jobs`, on aephysics's scheduler, which the native loops run on too) and the window, input and
timing layer (`ae3d.platform`, calling GLFW itself) and the frame capture the agent reads a scene by (`ae3d.capture`) are Aether, each
measured against the C it replaced.

| folder | what | why still C |
|---|---|---|
| `gpu/` | `vulkan.c`, the Vulkan renderer but for its meter, its readback and making its textures; `opengl_api.c`, the resolver `ae3d.glapi` looks up every OpenGL entry point through; `offscreen.c`, the offscreen GL context (CGL on macOS, a hidden GLFW window elsewhere); `jobs.c`, the door through which the renderers' own loops reach `ae3d.jobs`; `stores.h`, the mesh and instance stores as the renderers read them (they are `ae3d.geometry`'s, in Aether; `stores.c` lets `tests/test_geometry` hold the two layouts together); `shaders/`, the Vulkan GLSL that `tools/generate_shaders.ae` derives from the OpenGL sources in `src/ae3d/shaders` (`vulkan_shaders.h`, the SPIR-V, and `vulkan_uniforms.h`, the uniform block, are its output too) | the OpenGL renderer is Aether (`ae3d.gl`, `ae3d.glapi`, `ae3d.offscreen`, `ae3d.capture`) but for the resolver and the contexts, which are the platform's; the Vulkan one moves next ([#398](https://github.com/nicolas-maman/ae3d/issues/398), [#402](https://github.com/nicolas-maman/ae3d/issues/402)) |
| `image/` | `image.c`, decoding through the vendored `stb_image.h` | a third-party decoder (see `THIRD_PARTY_LICENSES.md`); a decoder of our own is an Aether project of its own |
| `platform/` | `crash.c`, the native stack printed on a crash; `metal_surface.m`, the CAMetalLayer MoltenVK draws into on macOS | a signal handler may call only what is async-signal-safe and has to be installed when the library loads, before any entry point; the Objective-C runtime |
| `dlss/` | `streamline.cpp`, DLSS through NVIDIA Streamline; `stub.c`, what is built without the SDK | the SDK's interface is C++ |

GLFW is called from both sides -- the window and input from Aether, the
surface and the GL entry points from `gpu/` -- so it has to be one shared
library, the one every package manager ships; every program names it on
its link line beside the engine's.

`ae3d.h` is the C API the Aether modules bind through `extern`;
`internal.h` is what the files here share with each other. Every file
compiles under `-Wall -Wextra -Werror` on Linux, macOS and Windows (ci.sh
checks each one alone), and the folders include the two headers by name
(`-Inative`).
