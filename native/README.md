# The C under the engine

ae3d is written in Aether. This directory holds the C that remains, and
every file in it is here for one reason: Aether has no 32-bit float, and
everything a GPU reads is one -- vertex buffers, instance transforms, bone
palettes, uniform blocks, push constants, and every Vulkan and OpenGL
structure with a float member. Writing a float32 from Aether today is a
runtime call per element (`std.mem.set_float32`), which at half a million
figures is 8 million calls a frame. The ask is
[aether-lang-dev/aether#2134](https://github.com/aether-lang-dev/aether/issues/2134);
when the language has `f32`, the folders below empty in order.

What was C for any other reason has moved: the crowd's kernels
(`ae3d.horde`), the flow field (`ae3d.nav`), the weather's particles
(`ae3d.weather`), the clouds' noise (`ae3d.cloudnoise`), the PNG writer
(`ae3d.png`), the file-as-bytes reader (`ae3d.blob`), the script loader
(`ae3d.script`), the agent channel's asking side (`ae3d.probe`) and the job
pool (`ae3d.jobs`, on aephysics's scheduler) are Aether, each measured
against the C it replaced.

| folder | what | why still C |
|---|---|---|
| `gpu/` | `vulkan.c`, the Vulkan renderer; `opengl.c` and `opengl_api.c`, the OpenGL one and its entry points; `offscreen.c` and `capture.c`, the offscreen targets and frame readback; `jobs.c`, the pool the renderers' own loops run over; `shaders/`, the Vulkan GLSL that `tools/generate_shaders.ae` derives from the OpenGL sources in `src/ae3d/shaders` (`vulkan_shaders.h`, the SPIR-V, and `vulkan_uniforms.h`, the uniform block, are its output too) | Vulkan and OpenGL structures with float members; float32 uploads |
| `geometry/` | `mesh.c`, the interleaved float32 vertex store; `skin.c`, bone palettes and pose banks; `meshfile.c`, meshes read into those | float32 buffers |
| `image/` | `image.c`, decoding through the vendored `stb_image.h` | a third-party decoder (see `THIRD_PARTY_LICENSES.md`); a decoder of our own is an Aether project of its own |
| `platform/` | `window.c`, the window, input and timing over GLFW; `metal_surface.m`, the CAMetalLayer MoltenVK draws into on macOS | the Objective-C runtime; the GLFW layer is portable and next to move |
| `agent/` | `channel.c`, the loopback socket a running scene answers JSON on | `std.tcp` cannot yet bind loopback only, poll a listening socket or set `TCP_NODELAY` ([aether#2136](https://github.com/aether-lang-dev/aether/issues/2136)) |
| `dlss/` | `streamline.cpp`, DLSS through NVIDIA Streamline; `stub.c`, what is built without the SDK | the SDK's interface is C++ |

`ae3d.h` is the C API the Aether modules bind through `extern`;
`internal.h` is what the files here share with each other. Every file
compiles under `-Wall -Wextra -Werror` on Linux, macOS and Windows (ci.sh
checks each one alone), and the folders include the two headers by name
(`-Inative`).
