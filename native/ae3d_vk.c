#include "ae3d.h"
#include "ae3d_internal.h"
#include "ae3d_vk_scene_shaders.h"
#include "ae3d_vk_uniforms.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#  define VK_USE_PLATFORM_METAL_EXT
#else
#  define VK_USE_PLATFORM_XLIB_KHR
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#  include <windows.h>
#  define GLFW_EXPOSE_NATIVE_WIN32
#else
#  include <dlfcn.h>
#  if defined(__APPLE__)
#    define GLFW_EXPOSE_NATIVE_COCOA
#  else
#    define GLFW_EXPOSE_NATIVE_X11
#  endif
#endif
#include <GLFW/glfw3native.h>

#if defined(_WIN32)
#  define AE3D_VK_PLATFORM_SURFACE_EXTENSION VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#elif defined(__APPLE__)
#  define AE3D_VK_PLATFORM_SURFACE_EXTENSION VK_EXT_METAL_SURFACE_EXTENSION_NAME
#else
#  define AE3D_VK_PLATFORM_SURFACE_EXTENSION VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#endif

#define AE3D_VK_FRAMES 2
/* Asked for at binding 4 in place of a texture handle: the camera depth. */
#define AE3D_VK_AUX_SCENE_DEPTH (-1)
/* Timestamps a frame: start, after shadow, after scene, after post. */
#define AE3D_VK_STAMPS 4
/* A cached descriptor set whose texture has been destroyed. Not zero: zero is
   a texture handle nothing uses, and not -1 alone, which reads as an error. */
#define AE3D_VK_SET_FREE (-2)
#define AE3D_VK_SHADOW_SIZE 2048
#define AE3D_VK_PROGRAM_SCENE 0
#define AE3D_VK_PROGRAM_WATER 1
#define AE3D_VK_PROGRAM_COUNT 2
#define AE3D_VK_DRAWS_PER_FRAME 4096
#define AE3D_VK_MAX_TEXTURES 256
#define AE3D_VK_SKIN_STRIDE (8 * (unsigned)sizeof(float))
#define AE3D_VK_STRIDE (9 * (int)sizeof(float))

#define AE3D_VK_GLOBAL_FUNCS(X) \
    X(vkCreateInstance) \
    X(vkEnumerateInstanceExtensionProperties)

#define AE3D_VK_INSTANCE_FUNCS(X) \
    X(vkDestroyInstance) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateDevice) \
    X(vkGetDeviceProcAddr)

// Only present when a surface extension was enabled. An offscreen device asks
// for none, so these are loaded but never required.
#define AE3D_VK_SURFACE_FUNCS(X) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkDestroySurfaceKHR)

#define AE3D_VK_DEVICE_FUNCS(X) \
    X(vkGetDeviceQueue) \
    X(vkDestroyDevice) \
    X(vkDeviceWaitIdle) \
    X(vkQueueWaitIdle) \
    X(vkCreateImage) \
    X(vkDestroyImage) \
    X(vkGetImageMemoryRequirements) \
    X(vkBindImageMemory) \
    X(vkCreateImageView) \
    X(vkDestroyImageView) \
    X(vkAllocateMemory) \
    X(vkFreeMemory) \
    X(vkMapMemory) \
    X(vkUnmapMemory) \
    X(vkCreateBuffer) \
    X(vkDestroyBuffer) \
    X(vkGetBufferMemoryRequirements) \
    X(vkBindBufferMemory) \
    X(vkCreateRenderPass) \
    X(vkDestroyRenderPass) \
    X(vkCreateFramebuffer) \
    X(vkDestroyFramebuffer) \
    X(vkCreateShaderModule) \
    X(vkDestroyShaderModule) \
    X(vkCreatePipelineLayout) \
    X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) \
    X(vkDestroyPipeline) \
    X(vkCreateCommandPool) \
    X(vkDestroyCommandPool) \
    X(vkAllocateCommandBuffers) \
    X(vkFreeCommandBuffers) \
    X(vkBeginCommandBuffer) \
    X(vkEndCommandBuffer) \
    X(vkResetCommandBuffer) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdEndRenderPass) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdDrawIndexed) \
    X(vkCmdPushConstants) \
    X(vkCmdSetViewport) \
    X(vkCmdSetScissor) \
    X(vkCmdCopyBuffer) \
    X(vkCreateDescriptorSetLayout) \
    X(vkDestroyDescriptorSetLayout) \
    X(vkCreateDescriptorPool) \
    X(vkDestroyDescriptorPool) \
    X(vkAllocateDescriptorSets) \
    X(vkResetDescriptorPool) \
    X(vkUpdateDescriptorSets) \
    X(vkCmdBindDescriptorSets) \
    X(vkCreateSampler) \
    X(vkDestroySampler) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdBlitImage) \
    X(vkCreateSemaphore) \
    X(vkDestroySemaphore) \
    X(vkCreateFence) \
    X(vkDestroyFence) \
    X(vkWaitForFences) \
    X(vkResetFences) \
    X(vkQueueSubmit) \
    X(vkCreateQueryPool) \
    X(vkDestroyQueryPool) \
    X(vkCmdResetQueryPool) \
    X(vkCmdWriteTimestamp) \
    X(vkGetQueryPoolResults)

// Swapchain entry points come from the device extension an offscreen device
// does not enable, so they are loaded the same way and required the same way.
#define AE3D_VK_SWAPCHAIN_FUNCS(X) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR)

#define AE3D_VK_DECLARE(name) static PFN_##name ae3d_##name;
AE3D_VK_GLOBAL_FUNCS(AE3D_VK_DECLARE)
AE3D_VK_INSTANCE_FUNCS(AE3D_VK_DECLARE)
AE3D_VK_SURFACE_FUNCS(AE3D_VK_DECLARE)
AE3D_VK_DEVICE_FUNCS(AE3D_VK_DECLARE)
AE3D_VK_SWAPCHAIN_FUNCS(AE3D_VK_DECLARE)
#undef AE3D_VK_DECLARE

typedef struct {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    /* Joints and weights, for a mesh that has them. A mesh that does not
       allocates nothing here and is drawn by a pipeline whose vertex input
       does not name the binding. */
    VkBuffer skin_buffer;
    VkDeviceMemory skin_memory;
    int skinned;
    unsigned index_count;
    int in_use;
    // Geometry that appears many times is uploaded once, so the models using it
    // can be merged into a single instanced draw. The bytes it was built from
    // are kept for an exact comparison: a hash could collide and hand back the
    // wrong mesh. Instance streams are never shared, and carry no copy.
    float *vertices;
    unsigned *indices;
    int vertex_count;
    int shared;
    int refs;
    /* An instance stream that moves every frame is written in place into a
       ring of host-visible buffers, one per frame in flight, and the frame's
       draws bind that frame's. Nothing is waited for and nothing destroyed:
       the slot a frame writes was last read by the frame whose fence
       frame_begin already waited on. Made on the first update, sized to the
       largest stream seen. */
    VkBuffer ring[AE3D_VK_FRAMES];
    /* An instance stream of points: eight floats an instance, drawn through
       the point pipelines, whose second binding strides by that. */
    int points;
    VkDeviceMemory ring_memory[AE3D_VK_FRAMES];
    void *ring_mapped[AE3D_VK_FRAMES];
    VkDeviceSize ring_size;
    int streaming;
} ae3d_vk_mesh;

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    int width;
    int height;
    int in_use;
} ae3d_vk_texture;

typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    unsigned char *mapped;
    unsigned stride;
    unsigned capacity;
    unsigned used;
} ae3d_vk_uniform_ring;

static struct {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue graphics_queue;
    VkQueue present_queue;
    unsigned graphics_family;
    unsigned present_family;
    VkPhysicalDeviceMemoryProperties memory_properties;
    char device_name[256];

    VkSwapchainKHR swapchain;
    VkFormat color_format;
    VkColorSpaceKHR color_space;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;
    unsigned image_count;
    VkImage *images;
    VkImageView *image_views;
    VkFramebuffer *framebuffers;

    VkFormat depth_format;
    VkImage depth_image;
    VkDeviceMemory depth_memory;
    VkImageView depth_view;

    VkSampleCountFlagBits samples;
    VkImage colour_image;
    VkDeviceMemory colour_memory;
    VkImageView colour_view;

    VkRenderPass render_pass;
    VkRenderPass scene_pass;
    VkRenderPass post_pass;
    VkRenderPass shadow_pass;
    VkFramebuffer shadow_framebuffer;
    VkImage shadow_image;
    VkDeviceMemory shadow_memory;
    VkImageView shadow_view;
    VkSampler shadow_sampler;
    VkPipeline shadow_pipeline;
    int normal_map;
    VkPipeline skinned_shadow_pipeline;
    VkPipeline water_pipeline[2];
    VkPipeline water_pipeline_blend;
    int program;
    int shadow_enabled;
    // Screen-space reflection: a camera-space depth prepass, sampled by the SSR
    // post pass to reflect on-screen geometry onto wet surfaces. Off unless a
    // scene asks for it, so the default render pays nothing and cannot break;
    // the target is built the first time it is turned on. The depth is single-
    // sample and sampleable by construction (the shadow map's pattern), which
    // is why it does not have to resolve the multisampled scene depth.
    int ssr_enabled;
    VkRenderPass camdepth_pass;
    VkFramebuffer camdepth_framebuffer;
    VkImage camdepth_image;
    VkDeviceMemory camdepth_memory;
    VkImageView camdepth_view;
    VkSampler camdepth_sampler;
    int camdepth_width;
    int camdepth_height;
    int pass_open;
    int in_shadow_pass;
    int in_camdepth;
    /* Whether something other than the reflection wants the camera depth
       drawn: a water surface reading the ground under it. */
    int scene_depth_wanted;
    VkDescriptorSet ssr_set[AE3D_VK_FRAMES];
    int ssr_set_built;
    float ssr_road_height;
    float ssr_strength;
    // Ambient occlusion from the same camera depth: a fullscreen multiply
    // drawn into the scene pass after the opaque geometry, before anything
    // transparent, with its own descriptor sets since the reflection's are
    // written for the post stage with the scene colour bound.
    int ssao_enabled;
    float ssao_radius;
    float ssao_intensity;
    VkPipeline ssao_pipeline;
    VkDescriptorSet ssao_set[AE3D_VK_FRAMES];
    // The reflective composite writes here rather than to the swapchain, so the
    // ordinary bloom/fxaa composite can then read it and bloom the mirrored
    // lamps too. It is a single-sample colour target that ends the pass in
    // sampler-read layout, built and torn down with the camera-depth target.
    VkRenderPass ssr_pass;
    VkFramebuffer ssr_framebuffer;
    int ssr_reflect_texture;
    float scene_clear[4];
    VkFramebuffer scene_framebuffer;
    VkFramebuffer *post_framebuffers;
    int post_texture;
    VkPipeline post_pipelines[4];
    VkPipeline sky_pipeline;
    int screen_quad;
    int fxaa;
    int bloom;
    int post_active;

    /* GPU time per pass. Four timestamps a frame -- start, after the shadow
       pass, after the scene pass, after post -- in one pool, read back when
       the frame's fence says the GPU has finished with them, which is
       AE3D_VK_FRAMES frames later and never a wait. */
    VkQueryPool timestamps;
    double timestamp_ms;          /* milliseconds per tick, from the device */
    int timestamps_usable;        /* the graphics queue reports valid bits */
    int stamped[AE3D_VK_FRAMES];  /* this frame's four stamps were all written */
    int stamp_next;               /* how many of this frame's stamps are written */
    double pass_ms[3];

    /* What a frame costs in changes of mind, the same way the OpenGL backend
       counts them. A pipeline bound twice in a row is bound once. */
    VkPipeline bound_pipeline;
    int bound_texture;
    int bound_normal;
    int pipeline_binds;
    int set_binds;
    VkPipelineLayout pipeline_layout;
    // Indexed by whether back faces are culled. Cull mode is fixed at pipeline
    // creation before Vulkan 1.3 and the loader targets 1.1, so the two states
    // are two pipelines rather than one dynamic state.
    VkPipeline pipeline[2];
    VkPipeline pipeline_blend;
    VkPipeline skinned_pipeline[2];
    VkPipeline skinned_pipeline_blend;
    /* A crowd: instanced and skinned, every instance posed from the pose bank
       at its own phase. Its own vertex shader, so its own pipelines, and a
       depth variant for the shadow pass. */
    VkPipeline crowd_pipeline[2];
    VkPipeline crowd_pipeline_blend;
    VkPipeline crowd_shadow_pipeline;
    /* Point instances: the scene pipelines again over a second binding of
       eight floats an instance, and a depth variant for the shadow pass. */
    VkPipeline point_pipeline[2];
    VkPipeline point_pipeline_blend;
    VkPipeline point_shadow_pipeline;
    /* The pose bank the next draws are posed from, as a texture handle; zero
       between crowds. Part of the descriptor set's key, at binding 4. */
    int pose_bank;
    int cull;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool descriptor_pool;
    VkCommandPool command_pool;

    ae3d_vk_uniform_ring uniforms[AE3D_VK_FRAMES];
    VkDescriptorSet sets[AE3D_VK_FRAMES][AE3D_VK_MAX_TEXTURES];
    int set_texture[AE3D_VK_FRAMES][AE3D_VK_MAX_TEXTURES];
    int set_normal[AE3D_VK_FRAMES][AE3D_VK_MAX_TEXTURES];
    int set_bank[AE3D_VK_FRAMES][AE3D_VK_MAX_TEXTURES];
    int set_count[AE3D_VK_FRAMES];

    ae3d_vk_texture textures[AE3D_VK_MAX_TEXTURES];
    int default_texture;

    // One block per program, the way OpenGL gives every program its own uniform
    // state. Sharing one would let a model's uniforms leak into the next draw
    // that uses a different program, since the two blocks share member names.
    ae3d_vk_scene scene[AE3D_VK_PROGRAM_COUNT];
    int blend;
    int draw_calls;
    float bloom_threshold;
    float bloom_intensity;
    VkBuffer identity_instance;
    VkDeviceMemory identity_instance_memory;
    /* One unused joint and weight, for the draws whose pipeline strides zero
       through it. */
    VkBuffer empty_skin;
    VkDeviceMemory empty_skin_memory;

    VkCommandBuffer command_buffers[AE3D_VK_FRAMES];
    VkSemaphore image_available[AE3D_VK_FRAMES];
    VkSemaphore render_finished[AE3D_VK_FRAMES];
    VkFence in_flight[AE3D_VK_FRAMES];

    unsigned frame;
    unsigned image_index;
    int recording;
    int needs_resize;
    int pending_width;
    int pending_height;

    ae3d_vk_mesh *meshes;
    int mesh_capacity;

    VkImage readback_image;
    VkDeviceMemory readback_memory;
    // One staging buffer per frame in flight, so a frame can be copied out
    // while the one before it is still being read.
    VkBuffer readback_buffer[AE3D_VK_FRAMES];
    VkDeviceMemory readback_buffer_memory[AE3D_VK_FRAMES];
    unsigned char *readback_mapped[AE3D_VK_FRAMES];
    int readback_frame;
    int readback_width;
    int readback_height;
    int offscreen;

    // Reading a windowed frame back. The swapchain image is copied to the
    // per-frame staging buffers above, but only when a capture is asked for --
    // a readback stalls the frame it reads, so it is off by default and armed
    // for the one frame a snapshot or an agent grid needs.
    int can_capture;      /* the surface allows a transfer-source swapchain */
    int capture_request;  /* copy the next frame that is submitted */
    int capture_slot;     /* which staging buffer holds the last capture, or -1 */

    int ready;
} vk;

static char g_vk_error[512] = "vulkan not initialised";

static int ae3d_vk_fail(const char *message) {
    snprintf(g_vk_error, sizeof(g_vk_error), "%s", message);
    return 0;
}

static int ae3d_vk_fail_code(const char *message, VkResult result) {
    snprintf(g_vk_error, sizeof(g_vk_error), "%s (VkResult %d)", message, (int)result);
    return 0;
}

const char *ae3d_vk_last_error(void) { return g_vk_error; }
const char *ae3d_vk_device_name(void) { return vk.device_name; }

// The loader is opened by absolute path rather than by leaf name: on macOS the
// Homebrew loader lives outside dyld's default search path, so a leaf-name
// dlopen fails and GLFW caches that failure at glfwInit time. Opening it here
// keeps Vulkan an optional runtime dependency with no link-time coupling and no
// environment variables for the user to set.
static PFN_vkGetInstanceProcAddr g_gipa;

static const char *const ae3d_vk_loader_paths[] = {
#if defined(_WIN32)
    "vulkan-1.dll",
#elif defined(__APPLE__)
    "libvulkan.1.dylib",
    "/opt/homebrew/lib/libvulkan.1.dylib",
    "/usr/local/lib/libvulkan.1.dylib",
    "libMoltenVK.dylib",
    "/opt/homebrew/lib/libMoltenVK.dylib",
    "/usr/local/lib/libMoltenVK.dylib",
#else
    "libvulkan.so.1",
    "libvulkan.so",
#endif
    NULL
};

/* Guarded to match its only reader, ae3d_vk_hint_icd() below, which is itself
 * `#if !defined(_WIN32)`: the Windows loader finds its ICDs through the
 * registry and needs no hint. Without this the array is defined and never used
 * on Windows, which -Werror rejects:
 *
 *   error: 'ae3d_vk_icd_paths' defined but not used
 *          [-Werror=unused-const-variable=] */
#if !defined(_WIN32)
static const char *const ae3d_vk_icd_paths[] = {
#if defined(__APPLE__)
    "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json",
    "/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json",
    "/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json",
#endif
    NULL
};
#endif

#if defined(_WIN32)
static void *ae3d_vk_dlopen(const char *path) { return (void *)LoadLibraryA(path); }
static void *ae3d_vk_dlsym(void *handle, const char *name) {
    return (void *)GetProcAddress((HMODULE)handle, name);
}
#else
static void *ae3d_vk_dlopen(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *ae3d_vk_dlsym(void *handle, const char *name) { return dlsym(handle, name); }
#endif

// Homebrew installs the MoltenVK manifest under etc/, which the loader does not
// scan. Pointing it at the manifest we can actually find is what makes a stock
// `brew install molten-vk vulkan-loader` work with no user setup.
static void ae3d_vk_hint_icd(void) {
#if !defined(_WIN32)
    int i;
    if (getenv("VK_ICD_FILENAMES") || getenv("VK_DRIVER_FILES")) return;
    for (i = 0; ae3d_vk_icd_paths[i]; i++) {
        FILE *probe = fopen(ae3d_vk_icd_paths[i], "r");
        if (probe) {
            fclose(probe);
            setenv("VK_ICD_FILENAMES", ae3d_vk_icd_paths[i], 0);
            return;
        }
    }
#endif
}

static int ae3d_vk_load_global(void) {
    void *library = NULL;
    int i;

    if (g_gipa) return 1;

    ae3d_vk_hint_icd();

    for (i = 0; ae3d_vk_loader_paths[i] && !library; i++) {
        library = ae3d_vk_dlopen(ae3d_vk_loader_paths[i]);
    }
    if (!library) return ae3d_vk_fail("no Vulkan loader found");

    g_gipa = (PFN_vkGetInstanceProcAddr)ae3d_vk_dlsym(library, "vkGetInstanceProcAddr");
    if (!g_gipa) return ae3d_vk_fail("vkGetInstanceProcAddr missing from the loader");

#define AE3D_VK_LOAD_GLOBAL(name) \
    ae3d_##name = (PFN_##name)g_gipa(NULL, #name); \
    if (!ae3d_##name) return ae3d_vk_fail("missing " #name);
    AE3D_VK_GLOBAL_FUNCS(AE3D_VK_LOAD_GLOBAL)
#undef AE3D_VK_LOAD_GLOBAL
    return 1;
}

static int ae3d_vk_load_instance(void) {
    PFN_vkGetInstanceProcAddr gipa = g_gipa;
    if (!gipa) return ae3d_vk_fail("vkGetInstanceProcAddr unavailable");

#define AE3D_VK_LOAD_INSTANCE(name) \
    ae3d_##name = (PFN_##name)gipa(vk.instance, #name); \
    if (!ae3d_##name) return ae3d_vk_fail("missing " #name);
    AE3D_VK_INSTANCE_FUNCS(AE3D_VK_LOAD_INSTANCE)
#undef AE3D_VK_LOAD_INSTANCE

#define AE3D_VK_LOAD_SURFACE(name) ae3d_##name = (PFN_##name)gipa(vk.instance, #name);
    AE3D_VK_SURFACE_FUNCS(AE3D_VK_LOAD_SURFACE)
#undef AE3D_VK_LOAD_SURFACE

    if (!vk.offscreen) {
#define AE3D_VK_NEED_SURFACE(name) if (!ae3d_##name) return ae3d_vk_fail("missing " #name);
        AE3D_VK_SURFACE_FUNCS(AE3D_VK_NEED_SURFACE)
#undef AE3D_VK_NEED_SURFACE
    }
    return 1;
}

static int ae3d_vk_load_device(void) {
#define AE3D_VK_LOAD_DEVICE(name) \
    ae3d_##name = (PFN_##name)ae3d_vkGetDeviceProcAddr(vk.device, #name); \
    if (!ae3d_##name) return ae3d_vk_fail("missing " #name);
    AE3D_VK_DEVICE_FUNCS(AE3D_VK_LOAD_DEVICE)
#undef AE3D_VK_LOAD_DEVICE

#define AE3D_VK_LOAD_SWAPCHAIN(name) ae3d_##name = (PFN_##name)ae3d_vkGetDeviceProcAddr(vk.device, #name);
    AE3D_VK_SWAPCHAIN_FUNCS(AE3D_VK_LOAD_SWAPCHAIN)
#undef AE3D_VK_LOAD_SWAPCHAIN

    if (!vk.offscreen) {
#define AE3D_VK_NEED_SWAPCHAIN(name) if (!ae3d_##name) return ae3d_vk_fail("missing " #name);
        AE3D_VK_SWAPCHAIN_FUNCS(AE3D_VK_NEED_SWAPCHAIN)
#undef AE3D_VK_NEED_SWAPCHAIN
    }
    return 1;
}

int ae3d_vk_available(void) {
    static int probed;
    static int result;

    if (probed) return result;
    probed = 1;
    result = ae3d_vk_load_global();
    if (result) snprintf(g_vk_error, sizeof(g_vk_error), "%s", "");
    return result;
}

static int ae3d_vk_has_extension(const VkExtensionProperties *list, unsigned count, const char *name) {
    unsigned i;
    for (i = 0; i < count; i++) {
        if (strcmp(list[i].extensionName, name) == 0) return 1;
    }
    return 0;
}

static int ae3d_vk_create_instance(void) {
    VkApplicationInfo app;
    VkInstanceCreateInfo info;
    const char *extensions[8];
    unsigned extension_count = 0, available_count = 0;
    VkExtensionProperties *available = NULL;
    VkResult result;
    int portability = 0;

    // An offscreen device needs no surface at all, so the platform surface
    // extension is not requested and a machine with no window system can still
    // render.
    if (!vk.offscreen) {
        extensions[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
        extensions[extension_count++] = AE3D_VK_PLATFORM_SURFACE_EXTENSION;
    }

    ae3d_vkEnumerateInstanceExtensionProperties(NULL, &available_count, NULL);
    if (available_count > 0) {
        available = (VkExtensionProperties *)calloc(available_count, sizeof(VkExtensionProperties));
        if (!available) return ae3d_vk_fail("out of memory");
        ae3d_vkEnumerateInstanceExtensionProperties(NULL, &available_count, available);
        portability = ae3d_vk_has_extension(available, available_count,
                                            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }
    if (portability) extensions[extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    free(available);

    memset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "ae3d";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "ae3d";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = extension_count;
    info.ppEnabledExtensionNames = extensions;
    if (portability) info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    result = ae3d_vkCreateInstance(&info, NULL, &vk.instance);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateInstance failed", result);
    return ae3d_vk_load_instance();
}

static int ae3d_vk_create_surface(void *win) {
    VkResult result;

#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR info;
    PFN_vkCreateWin32SurfaceKHR create =
        (PFN_vkCreateWin32SurfaceKHR)g_gipa(vk.instance, "vkCreateWin32SurfaceKHR");
    if (!create) return ae3d_vk_fail("vkCreateWin32SurfaceKHR unavailable");
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = GetModuleHandleW(NULL);
    info.hwnd = glfwGetWin32Window((GLFWwindow *)win);
    result = create(vk.instance, &info, NULL, &vk.surface);
#elif defined(__APPLE__)
    VkMetalSurfaceCreateInfoEXT info;
    void *layer;
    PFN_vkCreateMetalSurfaceEXT create =
        (PFN_vkCreateMetalSurfaceEXT)g_gipa(vk.instance, "vkCreateMetalSurfaceEXT");
    if (!create) return ae3d_vk_fail("vkCreateMetalSurfaceEXT unavailable");
    layer = ae3d_vk_native_layer(win);
    if (!layer) return ae3d_vk_fail("could not attach a CAMetalLayer to the window");
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    info.pLayer = (const CAMetalLayer *)layer;
    result = create(vk.instance, &info, NULL, &vk.surface);
#else
    VkXlibSurfaceCreateInfoKHR info;
    PFN_vkCreateXlibSurfaceKHR create =
        (PFN_vkCreateXlibSurfaceKHR)g_gipa(vk.instance, "vkCreateXlibSurfaceKHR");
    if (!create) return ae3d_vk_fail("vkCreateXlibSurfaceKHR unavailable");
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    info.dpy = glfwGetX11Display();
    info.window = glfwGetX11Window((GLFWwindow *)win);
    result = create(vk.instance, &info, NULL, &vk.surface);
#endif

    if (result != VK_SUCCESS) return ae3d_vk_fail_code("surface creation failed", result);
    return 1;
}

static int ae3d_vk_pick_device(void) {
    VkPhysicalDevice *devices;
    unsigned count = 0, i, q;
    int chosen = -1;

    ae3d_vkEnumeratePhysicalDevices(vk.instance, &count, NULL);
    if (count == 0) return ae3d_vk_fail("no Vulkan physical device");

    devices = (VkPhysicalDevice *)calloc(count, sizeof(VkPhysicalDevice));
    if (!devices) return ae3d_vk_fail("out of memory");
    ae3d_vkEnumeratePhysicalDevices(vk.instance, &count, devices);

    for (i = 0; i < count && chosen < 0; i++) {
        VkQueueFamilyProperties *families;
        unsigned family_count = 0;
        int graphics = -1, present = -1, timestamp_bits = 0;

        ae3d_vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, NULL);
        if (family_count == 0) continue;
        families = (VkQueueFamilyProperties *)calloc(family_count, sizeof(VkQueueFamilyProperties));
        if (!families) continue;
        ae3d_vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, families);

        for (q = 0; q < family_count; q++) {
            VkBool32 supported = VK_FALSE;
            if (graphics < 0 && (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                graphics = (int)q;
                timestamp_bits = (int)families[q].timestampValidBits;
            }
            if (vk.offscreen) {
                if (present < 0 && graphics >= 0) present = graphics;
                continue;
            }
            ae3d_vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, vk.surface, &supported);
            if (present < 0 && supported == VK_TRUE) present = (int)q;
        }
        free(families);

        if (graphics >= 0 && present >= 0) {
            VkPhysicalDeviceProperties properties;
            vk.physical = devices[i];
            vk.graphics_family = (unsigned)graphics;
            vk.present_family = (unsigned)present;
            ae3d_vkGetPhysicalDeviceProperties(vk.physical, &properties);
            snprintf(vk.device_name, sizeof(vk.device_name), "%s", properties.deviceName);
            vk.timestamp_ms = (double)properties.limits.timestampPeriod / 1000000.0;
            vk.timestamps_usable = timestamp_bits > 0;
            ae3d_vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory_properties);
            chosen = (int)i;
        }
    }
    free(devices);

    if (chosen < 0) return ae3d_vk_fail("no device with both graphics and present queues");
    return 1;
}

static int ae3d_vk_create_device(void) {
    VkDeviceQueueCreateInfo queues[2];
    VkDeviceCreateInfo info;
    VkExtensionProperties *available = NULL;
    const char *extensions[2];
    unsigned extension_count = 0, available_count = 0, queue_count = 1;
    float priority = 1.0f;
    VkResult result;

    memset(queues, 0, sizeof(queues));
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].queueFamilyIndex = vk.graphics_family;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = &priority;
    if (vk.present_family != vk.graphics_family) {
        queues[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[1].queueFamilyIndex = vk.present_family;
        queues[1].queueCount = 1;
        queues[1].pQueuePriorities = &priority;
        queue_count = 2;
    }

    if (!vk.offscreen) extensions[extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    ae3d_vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &available_count, NULL);
    if (available_count > 0) {
        available = (VkExtensionProperties *)calloc(available_count, sizeof(VkExtensionProperties));
        if (!available) return ae3d_vk_fail("out of memory");
        ae3d_vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &available_count, available);
        if (ae3d_vk_has_extension(available, available_count, "VK_KHR_portability_subset")) {
            extensions[extension_count++] = "VK_KHR_portability_subset";
        }
    }
    free(available);

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = queue_count;
    info.pQueueCreateInfos = queues;
    info.enabledExtensionCount = extension_count;
    info.ppEnabledExtensionNames = extensions;

    result = ae3d_vkCreateDevice(vk.physical, &info, NULL, &vk.device);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateDevice failed", result);
    if (!ae3d_vk_load_device()) return 0;

    ae3d_vkGetDeviceQueue(vk.device, vk.graphics_family, 0, &vk.graphics_queue);
    ae3d_vkGetDeviceQueue(vk.device, vk.present_family, 0, &vk.present_queue);
    return 1;
}

static int ae3d_vk_memory_type(unsigned bits, VkMemoryPropertyFlags wanted) {
    unsigned i;
    for (i = 0; i < vk.memory_properties.memoryTypeCount; i++) {
        if ((bits & (1u << i)) &&
            (vk.memory_properties.memoryTypes[i].propertyFlags & wanted) == wanted) {
            return (int)i;
        }
    }
    return -1;
}

static int ae3d_vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer *buffer, VkDeviceMemory *memory) {
    VkBufferCreateInfo info;
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation;
    int type;
    VkResult result;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = ae3d_vkCreateBuffer(vk.device, &info, NULL, buffer);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateBuffer failed", result);

    ae3d_vkGetBufferMemoryRequirements(vk.device, *buffer, &requirements);
    type = ae3d_vk_memory_type(requirements.memoryTypeBits, properties);
    if (type < 0) {
        ae3d_vkDestroyBuffer(vk.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return ae3d_vk_fail("no compatible memory type");
    }

    memset(&allocation, 0, sizeof(allocation));
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = (unsigned)type;

    result = ae3d_vkAllocateMemory(vk.device, &allocation, NULL, memory);
    if (result != VK_SUCCESS) {
        ae3d_vkDestroyBuffer(vk.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return ae3d_vk_fail_code("vkAllocateMemory failed", result);
    }
    ae3d_vkBindBufferMemory(vk.device, *buffer, *memory, 0);
    return 1;
}

// Uploads through a host-visible staging buffer so the resident copy stays
// device-local: on a discrete GPU that is the difference between reading vertices
// over PCIe every frame and reading them from VRAM.
static int ae3d_vk_upload_buffer(const void *data, VkDeviceSize size,
                                 VkBufferUsageFlags usage,
                                 VkBuffer *buffer, VkDeviceMemory *memory) {
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo allocation;
    VkCommandBufferBeginInfo begin;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSubmitInfo submit;
    VkBufferCopy region;
    void *mapped = NULL;

    if (!ae3d_vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &staging, &staging_memory)) {
        return 0;
    }
    if (ae3d_vkMapMemory(vk.device, staging_memory, 0, size, 0, &mapped) != VK_SUCCESS) {
        ae3d_vkDestroyBuffer(vk.device, staging, NULL);
        ae3d_vkFreeMemory(vk.device, staging_memory, NULL);
        return ae3d_vk_fail("vkMapMemory failed");
    }
    memcpy(mapped, data, (size_t)size);
    ae3d_vkUnmapMemory(vk.device, staging_memory);

    if (!ae3d_vk_create_buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buffer, memory)) {
        ae3d_vkDestroyBuffer(vk.device, staging, NULL);
        ae3d_vkFreeMemory(vk.device, staging_memory, NULL);
        return 0;
    }

    memset(&allocation, 0, sizeof(allocation));
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = vk.command_pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    ae3d_vkAllocateCommandBuffers(vk.device, &allocation, &command);

    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ae3d_vkBeginCommandBuffer(command, &begin);

    memset(&region, 0, sizeof(region));
    region.size = size;
    ae3d_vkCmdCopyBuffer(command, staging, *buffer, 1, &region);
    ae3d_vkEndCommandBuffer(command);

    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    ae3d_vkQueueSubmit(vk.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    ae3d_vkQueueWaitIdle(vk.graphics_queue);

    ae3d_vkFreeCommandBuffers(vk.device, vk.command_pool, 1, &command);
    ae3d_vkDestroyBuffer(vk.device, staging, NULL);
    ae3d_vkFreeMemory(vk.device, staging_memory, NULL);
    return 1;
}

static int ae3d_vk_create_image(int width, int height, int mip_levels, VkFormat format,
                                VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                                VkImageAspectFlags aspect, VkImageTiling tiling,
                                VkMemoryPropertyFlags properties,
                                VkImage *image, VkDeviceMemory *memory, VkImageView *view);
void ae3d_vk_texture_destroy(int handle);

static void ae3d_vk_destroy_shadow_target(void);
static void ae3d_vk_destroy_camdepth_target(void);

static void ae3d_vk_destroy_swapchain(void) {
    unsigned i;

    if (vk.framebuffers) {
        for (i = 0; i < vk.image_count; i++) {
            if (vk.framebuffers[i]) ae3d_vkDestroyFramebuffer(vk.device, vk.framebuffers[i], NULL);
        }
        free(vk.framebuffers);
        vk.framebuffers = NULL;
    }
    if (vk.image_views) {
        for (i = 0; i < vk.image_count; i++) {
            if (vk.image_views[i]) ae3d_vkDestroyImageView(vk.device, vk.image_views[i], NULL);
        }
        free(vk.image_views);
        vk.image_views = NULL;
    }
    // An offscreen target's image is owned here, unlike a swapchain's, which
    // the swapchain owns and destroys with itself.
    // The staging buffers are sized to the extent, so they go with the target
    // they copy from -- the offscreen image here, or the swapchain, whose
    // windowed capture buffers are the same array. Buffers never created are
    // VK_NULL_HANDLE and skipped, so this is safe on both paths.
    {
        unsigned slot;
        for (slot = 0; slot < AE3D_VK_FRAMES; slot++) {
            if (vk.readback_mapped[slot]) {
                ae3d_vkUnmapMemory(vk.device, vk.readback_buffer_memory[slot]);
                vk.readback_mapped[slot] = NULL;
            }
            if (vk.readback_buffer[slot]) {
                ae3d_vkDestroyBuffer(vk.device, vk.readback_buffer[slot], NULL);
                vk.readback_buffer[slot] = VK_NULL_HANDLE;
            }
            if (vk.readback_buffer_memory[slot]) {
                ae3d_vkFreeMemory(vk.device, vk.readback_buffer_memory[slot], NULL);
                vk.readback_buffer_memory[slot] = VK_NULL_HANDLE;
            }
        }
    }
    vk.capture_slot = -1;
    if (vk.offscreen && vk.images && vk.images[0]) {
        ae3d_vkDestroyImage(vk.device, vk.images[0], NULL);
        if (vk.readback_memory) {
            ae3d_vkFreeMemory(vk.device, vk.readback_memory, NULL);
            vk.readback_memory = VK_NULL_HANDLE;
        }
        vk.readback_width = 0;
        vk.readback_height = 0;
    }
    free(vk.images);
    vk.images = NULL;

    if (vk.post_framebuffers) {
        for (i = 0; i < vk.image_count; i++) {
            if (vk.post_framebuffers[i]) {
                ae3d_vkDestroyFramebuffer(vk.device, vk.post_framebuffers[i], NULL);
            }
        }
        free(vk.post_framebuffers);
        vk.post_framebuffers = NULL;
    }
    ae3d_vk_destroy_shadow_target();
    ae3d_vk_destroy_camdepth_target();
    if (vk.scene_framebuffer) {
        ae3d_vkDestroyFramebuffer(vk.device, vk.scene_framebuffer, NULL);
        vk.scene_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.post_texture) {
        ae3d_vk_texture_destroy(vk.post_texture);
        vk.post_texture = 0;
    }

    if (vk.colour_view) { ae3d_vkDestroyImageView(vk.device, vk.colour_view, NULL); vk.colour_view = VK_NULL_HANDLE; }
    if (vk.colour_image) { ae3d_vkDestroyImage(vk.device, vk.colour_image, NULL); vk.colour_image = VK_NULL_HANDLE; }
    if (vk.colour_memory) { ae3d_vkFreeMemory(vk.device, vk.colour_memory, NULL); vk.colour_memory = VK_NULL_HANDLE; }
    if (vk.depth_view) { ae3d_vkDestroyImageView(vk.device, vk.depth_view, NULL); vk.depth_view = VK_NULL_HANDLE; }
    if (vk.depth_image) { ae3d_vkDestroyImage(vk.device, vk.depth_image, NULL); vk.depth_image = VK_NULL_HANDLE; }
    if (vk.depth_memory) { ae3d_vkFreeMemory(vk.device, vk.depth_memory, NULL); vk.depth_memory = VK_NULL_HANDLE; }

    if (vk.swapchain && ae3d_vkDestroySwapchainKHR) {
        ae3d_vkDestroySwapchainKHR(vk.device, vk.swapchain, NULL);
        vk.swapchain = VK_NULL_HANDLE;
    }
    vk.image_count = 0;
}

static int ae3d_vk_choose_surface_format(void) {
    VkSurfaceFormatKHR *formats;
    if (vk.offscreen) {
        vk.color_format = VK_FORMAT_R8G8B8A8_UNORM;
        vk.color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        vk.present_mode = VK_PRESENT_MODE_FIFO_KHR;
        vk.depth_format = VK_FORMAT_D32_SFLOAT;
        return 1;
    }
    {
    VkPresentModeKHR *modes;
    unsigned count = 0, i;

    ae3d_vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &count, NULL);
    if (count == 0) return ae3d_vk_fail("surface reports no formats");
    formats = (VkSurfaceFormatKHR *)calloc(count, sizeof(VkSurfaceFormatKHR));
    if (!formats) return ae3d_vk_fail("out of memory");
    ae3d_vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &count, formats);

    vk.color_format = formats[0].format;
    vk.color_space = formats[0].colorSpace;
    for (i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            vk.color_format = formats[i].format;
            vk.color_space = formats[i].colorSpace;
            break;
        }
    }
    free(formats);

    count = 0;
    vk.present_mode = VK_PRESENT_MODE_FIFO_KHR;
    ae3d_vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, NULL);
    if (count > 0) {
        modes = (VkPresentModeKHR *)calloc(count, sizeof(VkPresentModeKHR));
        if (modes) {
            ae3d_vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &count, modes);
            free(modes);
        }
    }

    vk.depth_format = VK_FORMAT_D32_SFLOAT;
    }
    return 1;
}

static int ae3d_vk_create_depth(void) {
    VkImageCreateInfo image;
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation;
    VkImageViewCreateInfo view;
    int type;
    VkResult result;

    memset(&image, 0, sizeof(image));
    image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = vk.depth_format;
    image.extent.width = vk.extent.width;
    image.extent.height = vk.extent.height;
    image.extent.depth = 1;
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = vk.samples;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    result = ae3d_vkCreateImage(vk.device, &image, NULL, &vk.depth_image);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("depth vkCreateImage failed", result);

    ae3d_vkGetImageMemoryRequirements(vk.device, vk.depth_image, &requirements);
    type = ae3d_vk_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0) return ae3d_vk_fail("no device-local memory for the depth buffer");

    memset(&allocation, 0, sizeof(allocation));
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = (unsigned)type;
    result = ae3d_vkAllocateMemory(vk.device, &allocation, NULL, &vk.depth_memory);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("depth vkAllocateMemory failed", result);
    ae3d_vkBindImageMemory(vk.device, vk.depth_image, vk.depth_memory, 0);

    memset(&view, 0, sizeof(view));
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = vk.depth_image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = vk.depth_format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    result = ae3d_vkCreateImageView(vk.device, &view, NULL, &vk.depth_view);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("depth vkCreateImageView failed", result);
    return 1;
}

// The offscreen equivalent of a swapchain: one image the pass resolves into,
// which is then copied to a host-visible buffer for the caller to read.
static int ae3d_vk_create_offscreen_target(int width, int height) {
    VkDeviceSize size;

    if (width < 1) width = 1;
    if (height < 1) height = 1;
    vk.extent.width = (unsigned)width;
    vk.extent.height = (unsigned)height;

    vk.image_count = 1;
    vk.images = (VkImage *)calloc(1, sizeof(VkImage));
    vk.image_views = (VkImageView *)calloc(1, sizeof(VkImageView));
    if (!vk.images || !vk.image_views) return ae3d_vk_fail("out of memory");

    if (!ae3d_vk_create_image(width, height, 1, vk.color_format, VK_SAMPLE_COUNT_1_BIT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TILING_OPTIMAL,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              &vk.images[0], &vk.readback_memory, &vk.image_views[0])) {
        return 0;
    }

    size = (VkDeviceSize)width * height * 4;
    {
        unsigned slot;
        for (slot = 0; slot < AE3D_VK_FRAMES; slot++) {
            void *mapped = NULL;
            if (!ae3d_vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                           | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       &vk.readback_buffer[slot],
                                       &vk.readback_buffer_memory[slot])) {
                return 0;
            }
            if (ae3d_vkMapMemory(vk.device, vk.readback_buffer_memory[slot], 0, size, 0,
                                 &mapped) != VK_SUCCESS) {
                return ae3d_vk_fail("readback vkMapMemory failed");
            }
            vk.readback_mapped[slot] = (unsigned char *)mapped;
        }
    }
    vk.readback_width = width;
    vk.readback_height = height;

    if (vk.samples != VK_SAMPLE_COUNT_1_BIT) {
        if (!ae3d_vk_create_image(width, height, 1, vk.color_format, vk.samples,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TILING_OPTIMAL,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  &vk.colour_image, &vk.colour_memory, &vk.colour_view)) {
            return 0;
        }
    }
    return ae3d_vk_create_depth();
}

static int ae3d_vk_create_swapchain(int width, int height) {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSwapchainCreateInfoKHR info;
    unsigned queue_families[2];
    unsigned desired;
    unsigned i;
    VkResult result;

    ae3d_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities);

    if (capabilities.currentExtent.width != 0xFFFFFFFFu) {
        vk.extent = capabilities.currentExtent;
    } else {
        vk.extent.width = (unsigned)width;
        vk.extent.height = (unsigned)height;
        if (vk.extent.width < capabilities.minImageExtent.width) vk.extent.width = capabilities.minImageExtent.width;
        if (vk.extent.height < capabilities.minImageExtent.height) vk.extent.height = capabilities.minImageExtent.height;
        if (vk.extent.width > capabilities.maxImageExtent.width) vk.extent.width = capabilities.maxImageExtent.width;
        if (vk.extent.height > capabilities.maxImageExtent.height) vk.extent.height = capabilities.maxImageExtent.height;
    }
    if (vk.extent.width == 0 || vk.extent.height == 0) return ae3d_vk_fail("surface has zero extent");

    desired = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && desired > capabilities.maxImageCount) {
        desired = capabilities.maxImageCount;
    }

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = vk.surface;
    info.minImageCount = desired;
    info.imageFormat = vk.color_format;
    info.imageColorSpace = vk.color_space;
    info.imageExtent = vk.extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    // Reading a finished frame back -- for a snapshot, or for the agent to see
    // what it drew -- means copying the swapchain image, which needs it to be a
    // transfer source. Nearly every surface allows it; where one does not the
    // capture path stays off rather than failing to create the swapchain.
    vk.can_capture = (capabilities.supportedUsageFlags
                      & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ? 1 : 0;
    if (vk.can_capture) info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = vk.present_mode;
    info.clipped = VK_TRUE;

    if (vk.graphics_family != vk.present_family) {
        queue_families[0] = vk.graphics_family;
        queue_families[1] = vk.present_family;
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = queue_families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    result = ae3d_vkCreateSwapchainKHR(vk.device, &info, NULL, &vk.swapchain);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateSwapchainKHR failed", result);

    ae3d_vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.image_count, NULL);
    vk.images = (VkImage *)calloc(vk.image_count, sizeof(VkImage));
    vk.image_views = (VkImageView *)calloc(vk.image_count, sizeof(VkImageView));
    if (!vk.images || !vk.image_views) return ae3d_vk_fail("out of memory");
    ae3d_vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.image_count, vk.images);

    for (i = 0; i < vk.image_count; i++) {
        VkImageViewCreateInfo view;
        memset(&view, 0, sizeof(view));
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = vk.images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = vk.color_format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        result = ae3d_vkCreateImageView(vk.device, &view, NULL, &vk.image_views[i]);
        if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateImageView failed", result);
    }

    if (vk.samples != VK_SAMPLE_COUNT_1_BIT) {
        if (!ae3d_vk_create_image((int)vk.extent.width, (int)vk.extent.height, 1, vk.color_format,
                                  vk.samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TILING_OPTIMAL,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  &vk.colour_image, &vk.colour_memory, &vk.colour_view)) {
            return 0;
        }
    }
    return ae3d_vk_create_depth();
}

static int ae3d_vk_build_render_pass(VkImageLayout present_layout, VkRenderPass *out) {
    VkAttachmentDescription attachments[3];
    VkAttachmentReference colour_ref, depth_ref, resolve_ref;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency;
    VkRenderPassCreateInfo info;
    int multisampled = vk.samples != VK_SAMPLE_COUNT_1_BIT;
    unsigned count = multisampled ? 3u : 2u;

    memset(attachments, 0, sizeof(attachments));

    // Attachment 0 is what the subpass draws into: the multisampled image when
    // MSAA is on, otherwise the presentable image itself.
    attachments[0].format = vk.color_format;
    attachments[0].samples = vk.samples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                          : VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = multisampled ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                              : present_layout;

    attachments[1].format = vk.depth_format;
    attachments[1].samples = vk.samples;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachments[2].format = vk.color_format;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = present_layout;

    memset(&colour_ref, 0, sizeof(colour_ref));
    colour_ref.attachment = 0;
    colour_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&depth_ref, 0, sizeof(depth_ref));
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&resolve_ref, 0, sizeof(resolve_ref));
    resolve_ref.attachment = 2;
    resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colour_ref;
    subpass.pDepthStencilAttachment = &depth_ref;
    if (multisampled) subpass.pResolveAttachments = &resolve_ref;

    memset(&dependency, 0, sizeof(dependency));
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = count;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (ae3d_vkCreateRenderPass(vk.device, &info, NULL, out) != VK_SUCCESS) {
        return ae3d_vk_fail("vkCreateRenderPass failed");
    }
    return 1;
}

// Depth written as colour, so the main pass samples it like any other texture.
// The attachment ends in shader-read layout, which is what lets the very next
// pass in the same submission read it without a barrier of its own.
static int ae3d_vk_build_shadow_pass(void) {
    VkAttachmentDescription attachments[2];
    VkAttachmentReference colour_ref, depth_ref;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency;
    VkRenderPassCreateInfo info;

    // Depth, and nothing else. The pass used to write gl_FragCoord.z into an
    // R32_SFLOAT colour attachment beside a depth attachment holding that same
    // number, and then throw the depth away: every shadow texel written twice
    // and one of the two read. The depth image is what is sampled now.
    memset(attachments, 0, sizeof(attachments));
    attachments[0].format = vk.depth_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Left in the layout the descriptor asks for. Binding 2 holds the default
    // white texture when shadows are off, so both the shadow map and that
    // colour image have to be readable through the same declared layout.
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    memset(&colour_ref, 0, sizeof(colour_ref));

    memset(&depth_ref, 0, sizeof(depth_ref));
    depth_ref.attachment = 0;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depth_ref;

    // The write to wait on is the depth store, not a colour one.
    memset(&dependency, 0, sizeof(dependency));
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (ae3d_vkCreateRenderPass(vk.device, &info, NULL, &vk.shadow_pass) != VK_SUCCESS) {
        return ae3d_vk_fail("shadow vkCreateRenderPass failed");
    }
    return 1;
}

// The camera-space depth prepass, for screen-space reflection. The same
// depth-only pass the shadow map uses -- a single-sample depth attachment
// cleared, stored, and left readable -- but rendered from the camera rather
// than the light, so the SSR post pass can sample the scene's own depth.
static int ae3d_vk_build_camdepth_pass(void) {
    VkAttachmentDescription attachment;
    VkAttachmentReference depth_ref;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency;
    VkRenderPassCreateInfo info;

    memset(&attachment, 0, sizeof(attachment));
    attachment.format = vk.depth_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    memset(&depth_ref, 0, sizeof(depth_ref));
    depth_ref.attachment = 0;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depth_ref;

    memset(&dependency, 0, sizeof(dependency));
    dependency.srcSubpass = 0;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (ae3d_vkCreateRenderPass(vk.device, &info, NULL, &vk.camdepth_pass) != VK_SUCCESS) {
        return ae3d_vk_fail("camera-depth vkCreateRenderPass failed");
    }
    return 1;
}

static void ae3d_vk_destroy_camdepth_target(void) {
    int frame, index;
    /* Sets that bound the depth at binding 4 name a view about to go. */
    for (frame = 0; frame < AE3D_VK_FRAMES; frame++) {
        for (index = 0; index < vk.set_count[frame]; index++) {
            if (vk.set_bank[frame][index] == AE3D_VK_AUX_SCENE_DEPTH) {
                vk.set_texture[frame][index] = AE3D_VK_SET_FREE;
            }
        }
    }
    if (vk.camdepth_framebuffer) {
        ae3d_vkDestroyFramebuffer(vk.device, vk.camdepth_framebuffer, NULL);
        vk.camdepth_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.camdepth_sampler) {
        ae3d_vkDestroySampler(vk.device, vk.camdepth_sampler, NULL);
        vk.camdepth_sampler = VK_NULL_HANDLE;
    }
    if (vk.camdepth_view) {
        ae3d_vkDestroyImageView(vk.device, vk.camdepth_view, NULL);
        vk.camdepth_view = VK_NULL_HANDLE;
    }
    if (vk.camdepth_image) {
        ae3d_vkDestroyImage(vk.device, vk.camdepth_image, NULL);
        vk.camdepth_image = VK_NULL_HANDLE;
    }
    if (vk.camdepth_memory) {
        ae3d_vkFreeMemory(vk.device, vk.camdepth_memory, NULL);
        vk.camdepth_memory = VK_NULL_HANDLE;
    }
    vk.camdepth_width = 0;
    vk.camdepth_height = 0;
    if (vk.ssr_framebuffer) {
        ae3d_vkDestroyFramebuffer(vk.device, vk.ssr_framebuffer, NULL);
        vk.ssr_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.ssr_reflect_texture) {
        ae3d_vk_texture_destroy(vk.ssr_reflect_texture);
        vk.ssr_reflect_texture = 0;
    }
    // The SSR sets sample this target; force them to be reallocated and
    // rewritten against the new one when SSR is next drawn.
    {
        int f;
        for (f = 0; f < AE3D_VK_FRAMES; f++) { vk.ssr_set[f] = VK_NULL_HANDLE; vk.ssao_set[f] = VK_NULL_HANDLE; }
    }
}

// Built at the swapchain's size, the first time SSR is turned on and again
// whenever the swapchain is remade at a new size.
static int ae3d_vk_create_camdepth_target(int width, int height) {
    VkSamplerCreateInfo sampler;
    VkImageView attachments[1];
    VkFramebufferCreateInfo info;

    if (!ae3d_vk_create_image(width, height, 1, vk.depth_format, VK_SAMPLE_COUNT_1_BIT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_TILING_OPTIMAL,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              &vk.camdepth_image, &vk.camdepth_memory, &vk.camdepth_view)) {
        return 0;
    }

    memset(&sampler, 0, sizeof(sampler));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.maxLod = 1.0f;
    if (ae3d_vkCreateSampler(vk.device, &sampler, NULL, &vk.camdepth_sampler) != VK_SUCCESS) {
        return ae3d_vk_fail("camera-depth vkCreateSampler failed");
    }

    attachments[0] = vk.camdepth_view;
    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = vk.camdepth_pass;
    info.attachmentCount = 1;
    info.pAttachments = attachments;
    info.width = (unsigned)width;
    info.height = (unsigned)height;
    info.layers = 1;
    if (ae3d_vkCreateFramebuffer(vk.device, &info, NULL, &vk.camdepth_framebuffer) != VK_SUCCESS) {
        return ae3d_vk_fail("camera-depth vkCreateFramebuffer failed");
    }
    vk.camdepth_width = width;
    vk.camdepth_height = height;

    // The intermediate the reflective composite writes into: a colour target
    // the ordinary composite then samples, sized to match, filtered linearly so
    // the bloom that reads it can blur across it.
    {
        ae3d_vk_texture *reflect = NULL;
        VkSamplerCreateInfo rsampler;
        VkImageView rattach[1];
        VkFramebufferCreateInfo rinfo;
        int slot;

        for (slot = 0; slot < AE3D_VK_MAX_TEXTURES; slot++) {
            if (!vk.textures[slot].in_use) { reflect = &vk.textures[slot]; break; }
        }
        if (!reflect) return ae3d_vk_fail("texture table full");

        if (!ae3d_vk_create_image(width, height, 1, vk.color_format, VK_SAMPLE_COUNT_1_BIT,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TILING_OPTIMAL,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  &reflect->image, &reflect->memory, &reflect->view)) {
            return 0;
        }

        memset(&rsampler, 0, sizeof(rsampler));
        rsampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        rsampler.magFilter = VK_FILTER_LINEAR;
        rsampler.minFilter = VK_FILTER_LINEAR;
        rsampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        rsampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        rsampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        rsampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        rsampler.maxLod = 1.0f;
        if (ae3d_vkCreateSampler(vk.device, &rsampler, NULL, &reflect->sampler) != VK_SUCCESS) {
            return ae3d_vk_fail("ssr-reflect vkCreateSampler failed");
        }
        reflect->width = width;
        reflect->height = height;
        reflect->in_use = 1;
        vk.ssr_reflect_texture = slot + 1;

        rattach[0] = reflect->view;
        memset(&rinfo, 0, sizeof(rinfo));
        rinfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        rinfo.renderPass = vk.ssr_pass;
        rinfo.attachmentCount = 1;
        rinfo.pAttachments = rattach;
        rinfo.width = (unsigned)width;
        rinfo.height = (unsigned)height;
        rinfo.layers = 1;
        if (ae3d_vkCreateFramebuffer(vk.device, &rinfo, NULL, &vk.ssr_framebuffer) != VK_SUCCESS) {
            return ae3d_vk_fail("ssr-reflect vkCreateFramebuffer failed");
        }
    }
    return 1;
}

// The composite pass reads what the scene pass produced, so it takes a single
// resolved colour attachment and no depth.
static int ae3d_vk_build_post_pass(VkImageLayout present_layout, VkRenderPass *out) {
    VkAttachmentDescription attachment;
    VkAttachmentReference colour_ref;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency;
    VkRenderPassCreateInfo info;

    memset(&attachment, 0, sizeof(attachment));
    attachment.format = vk.color_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = present_layout;

    memset(&colour_ref, 0, sizeof(colour_ref));
    colour_ref.attachment = 0;
    colour_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colour_ref;

    memset(&dependency, 0, sizeof(dependency));
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    if (ae3d_vkCreateRenderPass(vk.device, &info, NULL, out) != VK_SUCCESS) {
        return ae3d_vk_fail("post vkCreateRenderPass failed");
    }
    return 1;
}

// Two passes over the same attachment descriptions: one ends in the layout the
// display wants, the other in the layout a sampler wants. They stay compatible,
// so the scene pipelines are valid in either.
static int ae3d_vk_create_render_pass(void) {
    VkImageLayout present_layout = vk.offscreen ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                                : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    if (!ae3d_vk_build_render_pass(present_layout, &vk.render_pass)) return 0;
    if (!ae3d_vk_build_render_pass(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &vk.scene_pass)) return 0;
    if (!ae3d_vk_build_shadow_pass()) return 0;
    if (!ae3d_vk_build_camdepth_pass()) return 0;
    // The reflective composite writes into an off-screen colour target the
    // final composite then samples, so its pass ends in sampler-read layout;
    // it stays compatible with the swapchain post pass, so one set of post
    // pipelines is valid in either.
    if (!ae3d_vk_build_post_pass(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &vk.ssr_pass)) return 0;
    return ae3d_vk_build_post_pass(present_layout, &vk.post_pass);
}

// The scene renders into a sampled image when post-processing is on, and the
// composite pass reads it. It is registered as an ordinary texture so the
// existing descriptor cache binds it with no second code path.
// The map, its own depth buffer, and the sampler the main pass reads it with.
// How many shadow targets the backend is holding. A rebuild makes a fresh one,
// so the old one has to go with the swapchain it was built alongside; this is
// what a test can watch to know that it did.
static int g_shadow_targets;

int ae3d_vk_live_shadow_targets(void) { return g_shadow_targets; }

static void ae3d_vk_destroy_shadow_target(void) {
    if (vk.shadow_framebuffer || vk.shadow_image) g_shadow_targets--;
    if (vk.shadow_framebuffer) {
        ae3d_vkDestroyFramebuffer(vk.device, vk.shadow_framebuffer, NULL);
        vk.shadow_framebuffer = VK_NULL_HANDLE;
    }
    if (vk.shadow_sampler) {
        ae3d_vkDestroySampler(vk.device, vk.shadow_sampler, NULL);
        vk.shadow_sampler = VK_NULL_HANDLE;
    }
    if (vk.shadow_view) {
        ae3d_vkDestroyImageView(vk.device, vk.shadow_view, NULL);
        vk.shadow_view = VK_NULL_HANDLE;
    }
    if (vk.shadow_image) {
        ae3d_vkDestroyImage(vk.device, vk.shadow_image, NULL);
        vk.shadow_image = VK_NULL_HANDLE;
    }
    if (vk.shadow_memory) {
        ae3d_vkFreeMemory(vk.device, vk.shadow_memory, NULL);
        vk.shadow_memory = VK_NULL_HANDLE;
    }
}

static int ae3d_vk_create_shadow_target(void) {
    VkSamplerCreateInfo sampler;
    VkImageView attachments[2];
    VkFramebufferCreateInfo info;

    // One image: the depth attachment is also what the lit pass samples.
    if (!ae3d_vk_create_image(AE3D_VK_SHADOW_SIZE, AE3D_VK_SHADOW_SIZE, 1,
                              vk.depth_format, VK_SAMPLE_COUNT_1_BIT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_TILING_OPTIMAL,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              &vk.shadow_image, &vk.shadow_memory, &vk.shadow_view)) {
        return 0;
    }

    memset(&sampler, 0, sizeof(sampler));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.maxLod = 1.0f;
    if (ae3d_vkCreateSampler(vk.device, &sampler, NULL, &vk.shadow_sampler) != VK_SUCCESS) {
        return ae3d_vk_fail("shadow vkCreateSampler failed");
    }

    attachments[0] = vk.shadow_view;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = vk.shadow_pass;
    info.attachmentCount = 1;
    info.pAttachments = attachments;
    info.width = AE3D_VK_SHADOW_SIZE;
    info.height = AE3D_VK_SHADOW_SIZE;
    info.layers = 1;
    if (ae3d_vkCreateFramebuffer(vk.device, &info, NULL, &vk.shadow_framebuffer) != VK_SUCCESS) {
        return ae3d_vk_fail("shadow vkCreateFramebuffer failed");
    }
    g_shadow_targets++;
    return 1;
}

static int ae3d_vk_create_post_target(void) {
    ae3d_vk_texture *texture = NULL;
    VkSamplerCreateInfo sampler;
    VkImageView attachments[3];
    VkFramebufferCreateInfo info;
    int multisampled = vk.samples != VK_SAMPLE_COUNT_1_BIT;
    unsigned i;
    int slot;

    for (slot = 0; slot < AE3D_VK_MAX_TEXTURES; slot++) {
        if (!vk.textures[slot].in_use) { texture = &vk.textures[slot]; break; }
    }
    if (!texture) return ae3d_vk_fail("texture table full");

    if (!ae3d_vk_create_image((int)vk.extent.width, (int)vk.extent.height, 1, vk.color_format,
                              VK_SAMPLE_COUNT_1_BIT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TILING_OPTIMAL,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              &texture->image, &texture->memory, &texture->view)) {
        return 0;
    }

    memset(&sampler, 0, sizeof(sampler));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = 1.0f;
    if (ae3d_vkCreateSampler(vk.device, &sampler, NULL, &texture->sampler) != VK_SUCCESS) {
        return ae3d_vk_fail("post vkCreateSampler failed");
    }

    texture->width = (int)vk.extent.width;
    texture->height = (int)vk.extent.height;
    texture->in_use = 1;
    vk.post_texture = slot + 1;

    if (multisampled) {
        attachments[0] = vk.colour_view;
        attachments[1] = vk.depth_view;
        attachments[2] = texture->view;
    } else {
        attachments[0] = texture->view;
        attachments[1] = vk.depth_view;
    }

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = vk.scene_pass;
    info.attachmentCount = multisampled ? 3u : 2u;
    info.pAttachments = attachments;
    info.width = vk.extent.width;
    info.height = vk.extent.height;
    info.layers = 1;
    if (ae3d_vkCreateFramebuffer(vk.device, &info, NULL, &vk.scene_framebuffer) != VK_SUCCESS) {
        return ae3d_vk_fail("scene vkCreateFramebuffer failed");
    }

    vk.post_framebuffers = (VkFramebuffer *)calloc(vk.image_count, sizeof(VkFramebuffer));
    if (!vk.post_framebuffers) return ae3d_vk_fail("out of memory");
    for (i = 0; i < vk.image_count; i++) {
        info.renderPass = vk.post_pass;
        info.attachmentCount = 1;
        info.pAttachments = &vk.image_views[i];
        if (ae3d_vkCreateFramebuffer(vk.device, &info, NULL, &vk.post_framebuffers[i]) != VK_SUCCESS) {
            return ae3d_vk_fail("post vkCreateFramebuffer failed");
        }
    }
    return 1;
}

static int ae3d_vk_create_framebuffers(void) {
    unsigned i;

    vk.framebuffers = (VkFramebuffer *)calloc(vk.image_count, sizeof(VkFramebuffer));
    if (!vk.framebuffers) return ae3d_vk_fail("out of memory");

    for (i = 0; i < vk.image_count; i++) {
        VkImageView attachments[3];
        VkFramebufferCreateInfo info;
        VkResult result;
        int multisampled = vk.samples != VK_SAMPLE_COUNT_1_BIT;

        if (multisampled) {
            attachments[0] = vk.colour_view;
            attachments[1] = vk.depth_view;
            attachments[2] = vk.image_views[i];
        } else {
            attachments[0] = vk.image_views[i];
            attachments[1] = vk.depth_view;
        }

        memset(&info, 0, sizeof(info));
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = vk.render_pass;
        info.attachmentCount = multisampled ? 3u : 2u;
        info.pAttachments = attachments;
        info.width = vk.extent.width;
        info.height = vk.extent.height;
        info.layers = 1;

        result = ae3d_vkCreateFramebuffer(vk.device, &info, NULL, &vk.framebuffers[i]);
        if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateFramebuffer failed", result);
    }
    if (!ae3d_vk_create_shadow_target()) return 0;
    return ae3d_vk_create_post_target();
}

static VkShaderModule ae3d_vk_shader(const unsigned char *bytes, unsigned length) {
    VkShaderModuleCreateInfo info;
    VkShaderModule module = VK_NULL_HANDLE;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = length;
    info.pCode = (const unsigned *)(const void *)bytes;
    if (ae3d_vkCreateShaderModule(vk.device, &info, NULL, &module) != VK_SUCCESS) return VK_NULL_HANDLE;
    return module;
}


static VkSampleCountFlagBits ae3d_vk_pick_samples(void) {
    VkPhysicalDeviceProperties properties;
    VkSampleCountFlags counts;

    ae3d_vkGetPhysicalDeviceProperties(vk.physical, &properties);
    counts = properties.limits.framebufferColorSampleCounts &
             properties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

static int ae3d_vk_create_image(int width, int height, int mip_levels, VkFormat format,
                                VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                                VkImageAspectFlags aspect, VkImageTiling tiling,
                                VkMemoryPropertyFlags properties,
                                VkImage *image, VkDeviceMemory *memory, VkImageView *view) {
    VkImageCreateInfo info;
    VkMemoryRequirements requirements;
    VkMemoryAllocateInfo allocation;
    int type;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = (unsigned)width;
    info.extent.height = (unsigned)height;
    info.extent.depth = 1;
    info.mipLevels = (unsigned)mip_levels;
    info.arrayLayers = 1;
    info.samples = samples;
    info.tiling = tiling;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (ae3d_vkCreateImage(vk.device, &info, NULL, image) != VK_SUCCESS) {
        return ae3d_vk_fail("vkCreateImage failed");
    }

    ae3d_vkGetImageMemoryRequirements(vk.device, *image, &requirements);
    type = ae3d_vk_memory_type(requirements.memoryTypeBits, properties);
    if (type < 0) return ae3d_vk_fail("no memory type for an image");

    memset(&allocation, 0, sizeof(allocation));
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = (unsigned)type;
    if (ae3d_vkAllocateMemory(vk.device, &allocation, NULL, memory) != VK_SUCCESS) {
        return ae3d_vk_fail("image vkAllocateMemory failed");
    }
    ae3d_vkBindImageMemory(vk.device, *image, *memory, 0);

    if (view) {
        VkImageViewCreateInfo view_info;
        memset(&view_info, 0, sizeof(view_info));
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = *image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = format;
        view_info.subresourceRange.aspectMask = aspect;
        view_info.subresourceRange.levelCount = (unsigned)mip_levels;
        view_info.subresourceRange.layerCount = 1;
        if (ae3d_vkCreateImageView(vk.device, &view_info, NULL, view) != VK_SUCCESS) {
            return ae3d_vk_fail("image vkCreateImageView failed");
        }
    }
    return 1;
}

// One command buffer, submitted and waited on. Used by uploads and layout
// transitions, which happen outside the frame loop.
static VkCommandBuffer ae3d_vk_begin_once(void) {
    VkCommandBufferAllocateInfo allocation;
    VkCommandBufferBeginInfo begin;
    VkCommandBuffer command = VK_NULL_HANDLE;

    memset(&allocation, 0, sizeof(allocation));
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = vk.command_pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (ae3d_vkAllocateCommandBuffers(vk.device, &allocation, &command) != VK_SUCCESS) return VK_NULL_HANDLE;

    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ae3d_vkBeginCommandBuffer(command, &begin);
    return command;
}

static void ae3d_vk_end_once(VkCommandBuffer command) {
    VkSubmitInfo submit;
    if (!command) return;
    ae3d_vkEndCommandBuffer(command);
    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    ae3d_vkQueueSubmit(vk.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    ae3d_vkQueueWaitIdle(vk.graphics_queue);
    ae3d_vkFreeCommandBuffers(vk.device, vk.command_pool, 1, &command);
}

static void ae3d_vk_transition_levels(VkCommandBuffer command, VkImage image,
                                      VkImageLayout from, VkImageLayout to,
                                      VkAccessFlags src_access, VkAccessFlags dst_access,
                                      VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
                                      int base_level, int level_count) {
    VkImageMemoryBarrier barrier;
    memset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = (unsigned)base_level;
    barrier.subresourceRange.levelCount = (unsigned)level_count;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    ae3d_vkCmdPipelineBarrier(command, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static void ae3d_vk_transition(VkCommandBuffer command, VkImage image,
                               VkImageLayout from, VkImageLayout to,
                               VkAccessFlags src_access, VkAccessFlags dst_access,
                               VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage) {
    ae3d_vk_transition_levels(command, image, from, to, src_access, dst_access,
                              src_stage, dst_stage, 0, 1);
}

// Each level is a filtered halving of the one above, the same chain
// glGenerateMipmap builds, and every level ends readable by the shader.
static void ae3d_vk_generate_mipmaps(VkCommandBuffer command, VkImage image,
                                     int width, int height, int mip_levels) {
    int level;
    int w = width;
    int h = height;

    for (level = 1; level < mip_levels; level++) {
        VkImageBlit blit;
        int next_w = w > 1 ? w / 2 : 1;
        int next_h = h > 1 ? h / 2 : 1;

        ae3d_vk_transition_levels(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  level - 1, 1);

        memset(&blit, 0, sizeof(blit));
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = (unsigned)(level - 1);
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1].x = w;
        blit.srcOffsets[1].y = h;
        blit.srcOffsets[1].z = 1;
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = (unsigned)level;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1].x = next_w;
        blit.dstOffsets[1].y = next_h;
        blit.dstOffsets[1].z = 1;
        ae3d_vkCmdBlitImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                            VK_FILTER_LINEAR);

        ae3d_vk_transition_levels(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, level - 1, 1);
        w = next_w;
        h = next_h;
    }

    ae3d_vk_transition_levels(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, mip_levels - 1, 1);
}

int ae3d_vk_texture_create(int width, int height, const void *rgba) {
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkCommandBuffer command;
    VkBufferImageCopy region;
    VkSamplerCreateInfo sampler;
    VkFormatProperties format_properties;
    ae3d_vk_texture *texture = NULL;
    void *mapped = NULL;
    VkDeviceSize size;
    int slot, handle = 0;
    int mip_levels = 1;
    int extent = width > height ? width : height;

    if (!vk.device || width <= 0 || height <= 0 || !rgba) return 0;
    size = (VkDeviceSize)width * height * 4;

    // Minification without mipmaps is what separates a sharp distant surface
    // from an aliased one, and the OpenGL backend generates them, so a texture
    // has to look the same here. Blitting needs the format to be filterable.
    ae3d_vkGetPhysicalDeviceFormatProperties(vk.physical, VK_FORMAT_R8G8B8A8_UNORM,
                                             &format_properties);
    if (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) {
        while (extent > 1) { extent /= 2; mip_levels++; }
    }

    for (slot = 0; slot < AE3D_VK_MAX_TEXTURES; slot++) {
        if (!vk.textures[slot].in_use) { texture = &vk.textures[slot]; handle = slot + 1; break; }
    }
    if (!texture) { ae3d_vk_fail("texture table full"); return 0; }

    if (!ae3d_vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &staging, &staging_memory)) {
        return 0;
    }
    ae3d_vkMapMemory(vk.device, staging_memory, 0, size, 0, &mapped);
    memcpy(mapped, rgba, (size_t)size);
    ae3d_vkUnmapMemory(vk.device, staging_memory);

    if (!ae3d_vk_create_image(width, height, mip_levels, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_SAMPLE_COUNT_1_BIT,
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TILING_OPTIMAL,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              &texture->image, &texture->memory, &texture->view)) {
        ae3d_vkDestroyBuffer(vk.device, staging, NULL);
        ae3d_vkFreeMemory(vk.device, staging_memory, NULL);
        return 0;
    }

    command = ae3d_vk_begin_once();
    ae3d_vk_transition(command, texture->image, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (unsigned)width;
    region.imageExtent.height = (unsigned)height;
    region.imageExtent.depth = 1;
    ae3d_vkCmdCopyBufferToImage(command, staging, texture->image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    ae3d_vk_generate_mipmaps(command, texture->image, width, height, mip_levels);
    ae3d_vk_end_once(command);

    ae3d_vkDestroyBuffer(vk.device, staging, NULL);
    ae3d_vkFreeMemory(vk.device, staging_memory, NULL);

    memset(&sampler, 0, sizeof(sampler));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = (float)mip_levels;
    if (ae3d_vkCreateSampler(vk.device, &sampler, NULL, &texture->sampler) != VK_SUCCESS) {
        ae3d_vk_fail("vkCreateSampler failed");
        return 0;
    }

    texture->width = width;
    texture->height = height;
    texture->in_use = 1;
    return handle;
}

void ae3d_vk_texture_destroy(int handle) {
    ae3d_vk_texture *texture;
    int frame, index;

    if (!vk.device || handle < 1 || handle > AE3D_VK_MAX_TEXTURES) return;
    texture = &vk.textures[handle - 1];
    if (!texture->in_use) return;
    ae3d_vkDeviceWaitIdle(vk.device);

    /* Descriptor sets are cached against the texture handle, and handles are
       reused: a set written for the image being destroyed here would be handed
       to whichever texture is created in this slot next, naming an image view
       that no longer exists. A hardware driver has usually survived reading
       one; lavapipe dereferences it, which is where this was found. The sets
       go back on the free list and are written again when they are claimed. */
    for (frame = 0; frame < AE3D_VK_FRAMES; frame++) {
        for (index = 0; index < vk.set_count[frame]; index++) {
            if (vk.set_texture[frame][index] == handle) {
                vk.set_texture[frame][index] = AE3D_VK_SET_FREE;
            }
        }
    }
    if (texture->sampler) ae3d_vkDestroySampler(vk.device, texture->sampler, NULL);
    if (texture->view) ae3d_vkDestroyImageView(vk.device, texture->view, NULL);
    if (texture->image) ae3d_vkDestroyImage(vk.device, texture->image, NULL);
    if (texture->memory) ae3d_vkFreeMemory(vk.device, texture->memory, NULL);
    memset(texture, 0, sizeof(*texture));
}

static int ae3d_vk_create_descriptors(void) {
    VkDescriptorSetLayoutBinding bindings[5];
    VkDescriptorSetLayoutCreateInfo layout;
    VkDescriptorPoolSize sizes[2];
    VkDescriptorPoolCreateInfo pool;
    VkPhysicalDeviceProperties properties;
    unsigned alignment, stride;
    int frame;

    memset(bindings, 0, sizeof(bindings));
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // The shadow map the fragment shader declares. OpenGL fills it from its
    // depth pass; here it holds the default texture until Vulkan has one, which
    // reads as fully lit and keeps the two backends agreeing.
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // The normal map. A material without one binds the default texture, whose
    // flat colour the shader never reads, because hasNormalMap is off.
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // The pose bank: a float texture of baked bone palettes the crowd vertex
    // shader fetches by (bone, frame). Read in the vertex stage, which is
    // what sets this binding apart from the other three.
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    memset(&layout, 0, sizeof(layout));
    layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.bindingCount = 5;
    layout.pBindings = bindings;
    if (ae3d_vkCreateDescriptorSetLayout(vk.device, &layout, NULL, &vk.set_layout) != VK_SUCCESS) {
        return ae3d_vk_fail("vkCreateDescriptorSetLayout failed");
    }

    memset(sizes, 0, sizeof(sizes));
    sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    sizes[0].descriptorCount = AE3D_VK_FRAMES * AE3D_VK_MAX_TEXTURES;
    sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = AE3D_VK_FRAMES * AE3D_VK_MAX_TEXTURES * 4;

    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.maxSets = AE3D_VK_FRAMES * AE3D_VK_MAX_TEXTURES;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    if (ae3d_vkCreateDescriptorPool(vk.device, &pool, NULL, &vk.descriptor_pool) != VK_SUCCESS) {
        return ae3d_vk_fail("vkCreateDescriptorPool failed");
    }

    // Every draw gets its own slice of one mapped buffer, bound with a dynamic
    // offset, so a frame's uniforms are written once and never reallocated.
    ae3d_vkGetPhysicalDeviceProperties(vk.physical, &properties);
    alignment = (unsigned)properties.limits.minUniformBufferOffsetAlignment;
    if (alignment == 0) alignment = 1;
    stride = (AE3D_VK_SCENE_SIZE + alignment - 1) / alignment * alignment;

    for (frame = 0; frame < AE3D_VK_FRAMES; frame++) {
        ae3d_vk_uniform_ring *ring = &vk.uniforms[frame];
        void *mapped = NULL;
        ring->stride = stride;
        ring->capacity = AE3D_VK_DRAWS_PER_FRAME;
        if (!ae3d_vk_create_buffer((VkDeviceSize)stride * AE3D_VK_DRAWS_PER_FRAME,
                                   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &ring->buffer, &ring->memory)) {
            return 0;
        }
        if (ae3d_vkMapMemory(vk.device, ring->memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
            return ae3d_vk_fail("uniform vkMapMemory failed");
        }
        ring->mapped = (unsigned char *)mapped;
    }
    return 1;
}

/* A set is keyed on the pair of images it binds, not on the colour alone: two
   materials can share a colour and differ in their normal map, and keying on
   one of them hands the second the first one's surface. */
static VkDescriptorSet ae3d_vk_set_for(int frame, int texture_handle, int normal_handle,
                                       int bank_handle) {
    VkDescriptorSetAllocateInfo allocation;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorBufferInfo buffer;
    VkDescriptorImageInfo image;
    VkDescriptorImageInfo shadow;
    VkDescriptorImageInfo bumps;
    VkDescriptorImageInfo bank;
    VkWriteDescriptorSet writes[5];
    ae3d_vk_texture *texture;
    ae3d_vk_texture *normal;
    ae3d_vk_texture *poses;
    int index;

    int reuse = -1;

    if (normal_handle < 1 || normal_handle > AE3D_VK_MAX_TEXTURES ||
        !vk.textures[normal_handle - 1].in_use) {
        normal_handle = vk.default_texture;
    }
    /* Binding 4 is whichever auxiliary image the draw needs: a crowd's pose
       bank by its texture handle, or the camera depth for a water surface,
       asked for as AE3D_VK_AUX_SCENE_DEPTH. */
    if (bank_handle == AE3D_VK_AUX_SCENE_DEPTH) {
        if (!vk.camdepth_view || !vk.camdepth_sampler) bank_handle = vk.default_texture;
    } else if (bank_handle < 1 || bank_handle > AE3D_VK_MAX_TEXTURES ||
               !vk.textures[bank_handle - 1].in_use) {
        bank_handle = vk.default_texture;
    }
    for (index = 0; index < vk.set_count[frame]; index++) {
        if (vk.set_texture[frame][index] == texture_handle &&
            vk.set_normal[frame][index] == normal_handle &&
            vk.set_bank[frame][index] == bank_handle) return vk.sets[frame][index];
        /* Left behind by a texture that was destroyed. The set is still
           allocated and can be written again, which is what keeps a scene that
           swaps textures from exhausting the pool. */
        if (vk.set_texture[frame][index] == AE3D_VK_SET_FREE && reuse < 0) reuse = index;
    }
    if (reuse < 0 && vk.set_count[frame] >= AE3D_VK_MAX_TEXTURES) return VK_NULL_HANDLE;

    texture = &vk.textures[texture_handle - 1];
    if (!texture->in_use) return VK_NULL_HANDLE;
    normal = &vk.textures[normal_handle - 1];
    poses = bank_handle == AE3D_VK_AUX_SCENE_DEPTH ? NULL : &vk.textures[bank_handle - 1];

    if (reuse >= 0) {
        set = vk.sets[frame][reuse];
    } else {
        memset(&allocation, 0, sizeof(allocation));
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = vk.descriptor_pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &vk.set_layout;
        if (ae3d_vkAllocateDescriptorSets(vk.device, &allocation, &set) != VK_SUCCESS) {
            return VK_NULL_HANDLE;
        }
    }

    memset(&buffer, 0, sizeof(buffer));
    buffer.buffer = vk.uniforms[frame].buffer;
    buffer.offset = 0;
    buffer.range = AE3D_VK_SCENE_SIZE;

    memset(&image, 0, sizeof(image));
    image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image.imageView = texture->view;
    image.sampler = texture->sampler;

    memset(writes, 0, sizeof(writes));
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &buffer;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &image;

    memset(&bumps, 0, sizeof(bumps));
    bumps.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bumps.imageView = normal->view;
    bumps.sampler = normal->sampler;
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = set;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &bumps;

    // Binding 2 is the shadow map. With shadows off it holds the default white
    // texture, which reads as nothing occluded.
    memset(&shadow, 0, sizeof(shadow));
    shadow.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (vk.shadow_enabled && vk.shadow_view) {
        shadow.imageView = vk.shadow_view;
        shadow.sampler = vk.shadow_sampler;
    } else {
        shadow.imageView = vk.textures[vk.default_texture - 1].view;
        shadow.sampler = vk.textures[vk.default_texture - 1].sampler;
    }

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &shadow;

    // Binding 4 is the pose bank the crowd vertex shader reads its bones
    // from; a draw that is not a crowd binds the default texture there and
    // never samples it.
    memset(&bank, 0, sizeof(bank));
    bank.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (poses) {
        bank.imageView = poses->view;
        bank.sampler = poses->sampler;
    } else {
        bank.imageView = vk.camdepth_view;
        bank.sampler = vk.camdepth_sampler;
    }
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = set;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].pImageInfo = &bank;

    ae3d_vkUpdateDescriptorSets(vk.device, 5, writes, 0, NULL);

    index = reuse >= 0 ? reuse : vk.set_count[frame]++;
    vk.sets[frame][index] = set;
    vk.set_texture[frame][index] = texture_handle;
    vk.set_normal[frame][index] = normal_handle;
    vk.set_bank[frame][index] = bank_handle;
    return set;
}

void ae3d_vk_scene_set_float(int offset, double value) {
    ae3d_vk_set_float(&vk.scene[vk.program], offset, (float)value);
}

void ae3d_vk_scene_set_int(int offset, int value) {
    ae3d_vk_set_int(&vk.scene[vk.program], offset, value);
}

void ae3d_vk_scene_set_vec3(int offset, double x, double y, double z) {
    ae3d_vk_set_vec3(&vk.scene[vk.program], offset, x, y, z);
}

void ae3d_vk_scene_set_mat4(int offset, const double *m) {
    if (!m) return;
    ae3d_vk_set_mat4(&vk.scene[vk.program], offset, m);
}

/* The bone palette, which is already the floats the block wants: std140 gives
   a mat4 array a stride of 64 bytes, which is the matrix itself, so the run
   copies whole rather than a matrix at a time. */
void ae3d_vk_scene_set_mat4v(int offset, int count, const void *values) {
    ae3d_vk_scene *block = &vk.scene[vk.program];
    size_t bytes = (size_t)count * 16 * sizeof(float);
    if (!values || count <= 0 || offset < 0) return;
    if ((size_t)offset + bytes > sizeof(block->bytes)) return;
    memcpy(block->bytes + offset, values, bytes);
}

// Vulkan clip space puts y downward and z in [0, 1] while the engine's matrices
// are OpenGL-shaped, so the correction is folded in here rather than making
// every caller keep two projections. It applies to whichever matrix reaches
// clip space last, the combined view-projection for the scene and the
// projection alone for the skybox, which multiplies the two in the shader.
void ae3d_vk_scene_set_clip_mat4(int offset, const double *m) {
    double corrected[16];
    int column;
    if (!m) return;
    for (column = 0; column < 4; column++) {
        corrected[column * 4 + 0] = m[column * 4 + 0];
        corrected[column * 4 + 1] = -m[column * 4 + 1];
        corrected[column * 4 + 2] = 0.5 * (m[column * 4 + 2] + m[column * 4 + 3]);
        corrected[column * 4 + 3] = m[column * 4 + 3];
    }
    ae3d_vk_set_mat4(&vk.scene[vk.program], offset, corrected);
}

/* The inverse of the clip-corrected view-projection, for the SSR shader to
 * rebuild a world position from a screen pixel and its depth. It must invert
 * the SAME matrix the vertex stage used (the corrected one above, mapping world
 * to Vulkan NDC: y down, z in 0..1), so the correction is applied first and the
 * result inverted. Column-major throughout, matching the rest of the block. */
void ae3d_vk_scene_set_inv_viewproj(int offset, const double *m) {
    double c[16], inv[16], det;
    int column, i;
    if (!m) return;
    for (column = 0; column < 4; column++) {
        c[column * 4 + 0] = m[column * 4 + 0];
        c[column * 4 + 1] = -m[column * 4 + 1];
        c[column * 4 + 2] = 0.5 * (m[column * 4 + 2] + m[column * 4 + 3]);
        c[column * 4 + 3] = m[column * 4 + 3];
    }
    inv[0]  =  c[5]*c[10]*c[15] - c[5]*c[11]*c[14] - c[9]*c[6]*c[15] + c[9]*c[7]*c[14] + c[13]*c[6]*c[11] - c[13]*c[7]*c[10];
    inv[4]  = -c[4]*c[10]*c[15] + c[4]*c[11]*c[14] + c[8]*c[6]*c[15] - c[8]*c[7]*c[14] - c[12]*c[6]*c[11] + c[12]*c[7]*c[10];
    inv[8]  =  c[4]*c[9]*c[15]  - c[4]*c[11]*c[13] - c[8]*c[5]*c[15] + c[8]*c[7]*c[13] + c[12]*c[5]*c[11] - c[12]*c[7]*c[9];
    inv[12] = -c[4]*c[9]*c[14]  + c[4]*c[10]*c[13] + c[8]*c[5]*c[14] - c[8]*c[6]*c[13] - c[12]*c[5]*c[10] + c[12]*c[6]*c[9];
    inv[1]  = -c[1]*c[10]*c[15] + c[1]*c[11]*c[14] + c[9]*c[2]*c[15] - c[9]*c[3]*c[14] - c[13]*c[2]*c[11] + c[13]*c[3]*c[10];
    inv[5]  =  c[0]*c[10]*c[15] - c[0]*c[11]*c[14] - c[8]*c[2]*c[15] + c[8]*c[3]*c[14] + c[12]*c[2]*c[11] - c[12]*c[3]*c[10];
    inv[9]  = -c[0]*c[9]*c[15]  + c[0]*c[11]*c[13] + c[8]*c[1]*c[15] - c[8]*c[3]*c[13] - c[12]*c[1]*c[11] + c[12]*c[3]*c[9];
    inv[13] =  c[0]*c[9]*c[14]  - c[0]*c[10]*c[13] - c[8]*c[1]*c[14] + c[8]*c[2]*c[13] + c[12]*c[1]*c[10] - c[12]*c[2]*c[9];
    inv[2]  =  c[1]*c[6]*c[15]  - c[1]*c[7]*c[14]  - c[5]*c[2]*c[15] + c[5]*c[3]*c[14] + c[13]*c[2]*c[7]  - c[13]*c[3]*c[6];
    inv[6]  = -c[0]*c[6]*c[15]  + c[0]*c[7]*c[14]  + c[4]*c[2]*c[15] - c[4]*c[3]*c[14] - c[12]*c[2]*c[7]  + c[12]*c[3]*c[6];
    inv[10] =  c[0]*c[5]*c[15]  - c[0]*c[7]*c[13]  - c[4]*c[1]*c[15] + c[4]*c[3]*c[13] + c[12]*c[1]*c[7]  - c[12]*c[3]*c[5];
    inv[14] = -c[0]*c[5]*c[14]  + c[0]*c[6]*c[13]  + c[4]*c[1]*c[14] - c[4]*c[2]*c[13] - c[12]*c[1]*c[6]  + c[12]*c[2]*c[5];
    inv[3]  = -c[1]*c[6]*c[11]  + c[1]*c[7]*c[10]  + c[5]*c[2]*c[11] - c[5]*c[3]*c[10] - c[9]*c[2]*c[7]   + c[9]*c[3]*c[6];
    inv[7]  =  c[0]*c[6]*c[11]  - c[0]*c[7]*c[10]  - c[4]*c[2]*c[11] + c[4]*c[3]*c[10] + c[8]*c[2]*c[7]   - c[8]*c[3]*c[6];
    inv[11] = -c[0]*c[5]*c[11]  + c[0]*c[7]*c[9]   + c[4]*c[1]*c[11] - c[4]*c[3]*c[9]  - c[8]*c[1]*c[7]   + c[8]*c[3]*c[5];
    inv[15] =  c[0]*c[5]*c[10]  - c[0]*c[6]*c[9]   - c[4]*c[1]*c[10] + c[4]*c[2]*c[9]  + c[8]*c[1]*c[6]   - c[8]*c[2]*c[5];
    det = c[0]*inv[0] + c[1]*inv[4] + c[2]*inv[8] + c[3]*inv[12];
    if (det > -1e-12 && det < 1e-12) { ae3d_vk_set_mat4(&vk.scene[vk.program], offset, c); return; }
    det = 1.0 / det;
    for (i = 0; i < 16; i++) inv[i] *= det;
    ae3d_vk_set_mat4(&vk.scene[vk.program], offset, inv);
}

// A model's own uniforms arrive by name. The offset is resolved once and cached
// on the uniform, so the strcmp walk happens the first time a name is seen and
// never again.
int ae3d_vk_scene_offset(const char *name) {
    return ae3d_vk_uniform_offset(name);
}

// std140 gives every array element a 16-byte slot whatever it holds, so a wave
// table cannot be memcpy'd in as a flat run of floats.
void ae3d_vk_scene_set_float_array(int offset, void *handle) {
    int count = ae3d_farr_count(handle);
    int i;
    if (offset < 0 || count <= 0) return;
    for (i = 0; i < count; i++) {
        ae3d_vk_set_float(&vk.scene[vk.program], offset + i * 16, (float)ae3d_farr_get(handle, i));
    }
}

void ae3d_vk_scene_set_vec3_array(int offset, void *handle) {
    int count = ae3d_farr_count(handle) / 3;
    int i;
    if (offset < 0 || count <= 0) return;
    for (i = 0; i < count; i++) {
        ae3d_vk_set_vec3(&vk.scene[vk.program], offset + i * 16, ae3d_farr_get(handle, i * 3),
                         ae3d_farr_get(handle, i * 3 + 1), ae3d_farr_get(handle, i * 3 + 2));
    }
}

void ae3d_vk_set_blend(int on) { vk.blend = on; }

/* Which normal map the next draws use. Renderer state rather than a draw
   argument, the way the blend and the program already are: a material sets it
   once and every draw of that material reads it. */
void ae3d_vk_set_normal_map(int handle) { vk.normal_map = handle; }

// Every pipeline shares the scene's vertex input and descriptor set layout, so
// a pass differs only in its shaders and its depth and blend state. Attributes a
// shader ignores cost nothing, which is why the fullscreen and skybox passes can
// be described by the same vertex input as the scene.
static VkPipeline ae3d_vk_build_pipeline(VkShaderModule vertex_module,
                                        VkShaderModule fragment_module, int blend,
                                        int depth_test, int depth_write,
                                        VkRenderPass render_pass, int cull,
                                        int skinned, int points) {
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription bindings[3];
    VkVertexInputAttributeDescription attributes[12];
    VkPipelineVertexInputStateCreateInfo vertex_input;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineDepthStencilStateCreateInfo depth;
    VkPipelineColorBlendAttachmentState blend_attachment;
    VkPipelineColorBlendStateCreateInfo colour_blend;
    VkPipelineDynamicStateCreateInfo dynamic;
    VkDynamicState dynamic_states[2];
    VkGraphicsPipelineCreateInfo info;
    VkPipeline pipeline = VK_NULL_HANDLE;
    int i;

    memset(bindings, 0, sizeof(bindings));
    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_module;
    stages[1].pName = "main";

    // Binding 0 is the mesh, binding 1 is the per-instance matrix and colour,
    // the same split the OpenGL backend uses for its instanced attributes.
    memset(bindings, 0, sizeof(bindings));
    bindings[0].binding = 0;
    bindings[0].stride = AE3D_VK_STRIDE;
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = 16 * (unsigned)sizeof(float) + 4 * (unsigned)sizeof(float);
    /* Points: position and scale, colour, phase -- eight floats. The matrix's
       other three columns are named at offset zero, since the input has to
       describe every attribute the shader declares, and a point draw never
       reads them. */
    if (points) bindings[1].stride = 8 * (unsigned)sizeof(float);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    memset(attributes, 0, sizeof(attributes));
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = 3 * (unsigned)sizeof(float);
    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = 5 * (unsigned)sizeof(float);
    /* Baked occlusion, in the vertex rather than a binding of its own, so no
       pipeline has to know whether a mesh has any. */
    attributes[10].location = 10;
    attributes[10].binding = 0;
    attributes[10].format = VK_FORMAT_R32_SFLOAT;
    attributes[10].offset = 8 * (unsigned)sizeof(float);
    for (i = 0; i < 4; i++) {
        attributes[3 + i].location = (unsigned)(3 + i);
        attributes[3 + i].binding = 1;
        attributes[3 + i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[3 + i].offset = points ? 0u : (unsigned)(i * 4 * (int)sizeof(float));
    }
    attributes[7].location = 7;
    attributes[7].binding = 1;
    attributes[7].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[7].offset = (points ? 4u : 16u) * (unsigned)sizeof(float);
    /* The instance's phase in its walk, in the float after its colour that
       the stream always carried and nothing read. Every pipeline names it;
       only the crowd's shaders read it. */
    attributes[11].location = 11;
    attributes[11].binding = 1;
    attributes[11].format = VK_FORMAT_R32_SFLOAT;
    attributes[11].offset = (points ? 7u : 19u) * (unsigned)sizeof(float);

    /* Vulkan has no equivalent of leaving an attribute disabled: the vertex
       input has to describe every input the shader reads, and one shader
       serves both kinds of draw. So both pipelines name the binding, and what
       differs is the stride -- the skinned one steps through a joint and
       weight per vertex, the plain one strides zero and every vertex reads the
       same unused element out of a buffer that is one element long. That is
       what makes an unskinned mesh cost nothing: no skin buffer, no allocation
       that scales with the mesh, and a branch the shader does not take. */
    bindings[2].binding = 2;
    bindings[2].stride = skinned ? AE3D_VK_SKIN_STRIDE : 0;
    bindings[2].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    attributes[8].location = 8;
    attributes[8].binding = 2;
    attributes[8].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[8].offset = 0;
    attributes[9].location = 9;
    attributes[9].binding = 2;
    attributes[9].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[9].offset = 4 * (unsigned)sizeof(float);

    memset(&vertex_input, 0, sizeof(vertex_input));
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 3;
    vertex_input.pVertexBindingDescriptions = bindings;
    vertex_input.vertexAttributeDescriptionCount = 12;
    vertex_input.pVertexAttributeDescriptions = attributes;

    memset(&assembly, 0, sizeof(assembly));
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    memset(&viewport_state, 0, sizeof(viewport_state));
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    memset(&raster, 0, sizeof(raster));
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = cull ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    memset(&multisample, 0, sizeof(multisample));
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = vk.samples;
    if (render_pass == vk.post_pass || render_pass == vk.shadow_pass) {
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    }

    memset(&depth, 0, sizeof(depth));
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = depth_test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.maxDepthBounds = 1.0f;

    memset(&blend_attachment, 0, sizeof(blend_attachment));
    blend_attachment.blendEnable = blend ? VK_TRUE : VK_FALSE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    /* 2 is a multiply: what is drawn scales what is there, which is how the
       occlusion pass darkens the opaque scene under it. */
    if (blend == 2) {
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    }
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    memset(&colour_blend, 0, sizeof(colour_blend));
    colour_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    // As many blend attachments as the subpass has colour attachments, which
    // for the shadow pass is none: it writes depth and nothing else.
    colour_blend.attachmentCount = (render_pass == vk.shadow_pass) ? 0 : 1;
    colour_blend.pAttachments = &blend_attachment;

    dynamic_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1] = VK_DYNAMIC_STATE_SCISSOR;
    memset(&dynamic, 0, sizeof(dynamic));
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = render_pass == vk.post_pass ? NULL : &depth;
    info.pColorBlendState = &colour_blend;
    info.pDynamicState = &dynamic;
    info.layout = vk.pipeline_layout;
    info.renderPass = render_pass;
    info.subpass = 0;

    if (ae3d_vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

// The sky is drawn behind everything with depth writes off, and the composite
// passes have no depth at all, matching what the OpenGL backend sets by hand.
// Only the passthrough composite is required: an effect that fails to build
// falls back to it and the scene still reaches the screen, which is what the
// OpenGL backend does when one of its effect programs fails to compile.
static int ae3d_vk_create_pass_pipelines(void) {
    struct { const unsigned char *vert; unsigned vert_size;
             const unsigned char *frag; unsigned frag_size;
             int blend, depth_test, depth_write, required, cull;
             VkRenderPass pass; VkPipeline *out; } builds[] = {
        { ae3d_vk_sky_vert_spv, sizeof(ae3d_vk_sky_vert_spv),
          ae3d_vk_sky_frag_spv, sizeof(ae3d_vk_sky_frag_spv),
          0, 1, 0, 0, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_screen_vert_spv, sizeof(ae3d_vk_screen_vert_spv),
          ae3d_vk_passthrough_frag_spv, sizeof(ae3d_vk_passthrough_frag_spv),
          0, 0, 0, 1, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_screen_vert_spv, sizeof(ae3d_vk_screen_vert_spv),
          ae3d_vk_fxaa_frag_spv, sizeof(ae3d_vk_fxaa_frag_spv),
          0, 0, 0, 0, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_screen_vert_spv, sizeof(ae3d_vk_screen_vert_spv),
          ae3d_vk_bloom_frag_spv, sizeof(ae3d_vk_bloom_frag_spv),
          0, 0, 0, 0, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_depth_vert_spv, sizeof(ae3d_vk_depth_vert_spv),
          ae3d_vk_depth_frag_spv, sizeof(ae3d_vk_depth_frag_spv),
          0, 1, 1, 1, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_water_vert_spv, sizeof(ae3d_vk_water_vert_spv),
          ae3d_vk_water_frag_spv, sizeof(ae3d_vk_water_frag_spv),
          0, 1, 1, 1, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_water_vert_spv, sizeof(ae3d_vk_water_vert_spv),
          ae3d_vk_water_frag_spv, sizeof(ae3d_vk_water_frag_spv),
          1, 1, 0, 1, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_water_vert_spv, sizeof(ae3d_vk_water_vert_spv),
          ae3d_vk_water_frag_spv, sizeof(ae3d_vk_water_frag_spv),
          0, 1, 1, 1, 1, VK_NULL_HANDLE, NULL },
        { ae3d_vk_depth_vert_spv, sizeof(ae3d_vk_depth_vert_spv),
          ae3d_vk_depth_frag_spv, sizeof(ae3d_vk_depth_frag_spv),
          0, 1, 1, 1, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_screen_vert_spv, sizeof(ae3d_vk_screen_vert_spv),
          ae3d_vk_ssr_frag_spv, sizeof(ae3d_vk_ssr_frag_spv),
          0, 0, 0, 0, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_crowd_depth_vert_spv, sizeof(ae3d_vk_crowd_depth_vert_spv),
          ae3d_vk_depth_frag_spv, sizeof(ae3d_vk_depth_frag_spv),
          0, 1, 1, 0, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_screen_vert_spv, sizeof(ae3d_vk_screen_vert_spv),
          ae3d_vk_ssao_frag_spv, sizeof(ae3d_vk_ssao_frag_spv),
          2, 0, 0, 0, 0, VK_NULL_HANDLE, NULL },
        { ae3d_vk_depth_vert_spv, sizeof(ae3d_vk_depth_vert_spv),
          ae3d_vk_depth_frag_spv, sizeof(ae3d_vk_depth_frag_spv),
          0, 1, 1, 0, 0, VK_NULL_HANDLE, NULL },
    };
    unsigned i;

    builds[0].pass = vk.render_pass; builds[0].out = &vk.sky_pipeline;
    builds[1].pass = vk.post_pass;   builds[1].out = &vk.post_pipelines[0];
    builds[2].pass = vk.post_pass;   builds[2].out = &vk.post_pipelines[1];
    builds[3].pass = vk.post_pass;   builds[3].out = &vk.post_pipelines[2];
    builds[4].pass = vk.shadow_pass; builds[4].out = &vk.shadow_pipeline;
    builds[8].pass = vk.shadow_pass; builds[8].out = &vk.skinned_shadow_pipeline;
    builds[5].pass = vk.render_pass; builds[5].out = &vk.water_pipeline[0];
    builds[6].pass = vk.render_pass; builds[6].out = &vk.water_pipeline_blend;
    builds[7].pass = vk.render_pass; builds[7].out = &vk.water_pipeline[1];
    builds[9].pass = vk.post_pass;   builds[9].out = &vk.post_pipelines[3];
    builds[10].pass = vk.shadow_pass; builds[10].out = &vk.crowd_shadow_pipeline;
    builds[11].pass = vk.render_pass; builds[11].out = &vk.ssao_pipeline;
    builds[12].pass = vk.shadow_pass; builds[12].out = &vk.point_shadow_pipeline;

    for (i = 0; i < sizeof(builds) / sizeof(builds[0]); i++) {
        VkShaderModule vertex_module = ae3d_vk_shader(builds[i].vert, builds[i].vert_size);
        VkShaderModule fragment_module = ae3d_vk_shader(builds[i].frag, builds[i].frag_size);
        if (!vertex_module || !fragment_module) return ae3d_vk_fail("pass vkCreateShaderModule failed");
        int skinned = (builds[i].out == &vk.skinned_shadow_pipeline
                       || builds[i].out == &vk.crowd_shadow_pipeline);
        int points = builds[i].out == &vk.point_shadow_pipeline;
        *builds[i].out = ae3d_vk_build_pipeline(vertex_module, fragment_module, builds[i].blend,
                                                builds[i].depth_test, builds[i].depth_write,
                                                builds[i].pass, builds[i].cull, skinned, points);
        ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);
        ae3d_vkDestroyShaderModule(vk.device, fragment_module, NULL);
        if (!*builds[i].out && builds[i].required) {
            return ae3d_vk_fail("pass vkCreateGraphicsPipelines failed");
        }
    }
    return 1;
}

static int ae3d_vk_create_pipeline(void) {
    VkShaderModule vertex_module, fragment_module;
    VkPipelineLayoutCreateInfo layout;
    int cull;

    vertex_module = ae3d_vk_shader(ae3d_vk_scene_vert_spv, (unsigned)sizeof(ae3d_vk_scene_vert_spv));
    fragment_module = ae3d_vk_shader(ae3d_vk_scene_frag_spv, (unsigned)sizeof(ae3d_vk_scene_frag_spv));
    if (!vertex_module || !fragment_module) return ae3d_vk_fail("scene vkCreateShaderModule failed");

    memset(&layout, 0, sizeof(layout));
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &vk.set_layout;
    if (ae3d_vkCreatePipelineLayout(vk.device, &layout, NULL, &vk.pipeline_layout) != VK_SUCCESS) {
        ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);
        ae3d_vkDestroyShaderModule(vk.device, fragment_module, NULL);
        return ae3d_vk_fail("vkCreatePipelineLayout failed");
    }

    // A transparent surface is never culled: you see through it to its own far
    // side, so removing that side removes something the frame should show. The
    // OpenGL backend turns culling off for the transparent pass for the same
    // reason, which is why only the opaque pipeline comes in two variants.
    vk.pipeline_blend = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 0,
                                               vk.render_pass, 0, 0, 0);
    vk.skinned_pipeline_blend = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 0,
                                                       vk.render_pass, 0, 1, 0);
    for (cull = 0; cull < 2; cull++) {
        vk.pipeline[cull] = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 1,
                                                   vk.render_pass, cull, 0, 0);
        vk.skinned_pipeline[cull] = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 1,
                                                           vk.render_pass, cull, 1, 0);
        if (!vk.pipeline[cull] || !vk.skinned_pipeline[cull]) {
            ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);
            ae3d_vkDestroyShaderModule(vk.device, fragment_module, NULL);
            return ae3d_vk_fail("vkCreateGraphicsPipelines failed");
        }
    }
    if (!vk.pipeline_blend || !vk.skinned_pipeline_blend) {
        ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);
        ae3d_vkDestroyShaderModule(vk.device, fragment_module, NULL);
        return ae3d_vk_fail("vkCreateGraphicsPipelines failed");
    }
    /* Points: not required, a device that will not build them draws a point
       stream through the matrix pipelines, at the stride of a matrix, which
       is wrong but not a crash; the parity test says which it is. */
    vk.point_pipeline_blend = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 0,
                                                     vk.render_pass, 0, 0, 1);
    for (cull = 0; cull < 2; cull++) {
        vk.point_pipeline[cull] = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 1,
                                                         vk.render_pass, cull, 0, 1);
    }
    ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);

    // The crowd: the same fragment shader after a vertex shader that poses
    // each instance from the pose bank. Not required: a device that will not
    // build it draws a crowd unposed instead of not at all.
    vertex_module = ae3d_vk_shader(ae3d_vk_crowd_vert_spv, (unsigned)sizeof(ae3d_vk_crowd_vert_spv));
    if (vertex_module) {
        vk.crowd_pipeline_blend = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 0,
                                                         vk.render_pass, 0, 1, 0);
        for (cull = 0; cull < 2; cull++) {
            vk.crowd_pipeline[cull] = ae3d_vk_build_pipeline(vertex_module, fragment_module, 1, 1, 1,
                                                             vk.render_pass, cull, 1, 0);
        }
        ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);
    }
    ae3d_vkDestroyShaderModule(vk.device, fragment_module, NULL);
    return ae3d_vk_create_pass_pipelines();
}

// A white pixel and one identity instance, so a draw with no texture and no
// instance buffer still has something to bind: the shader always samples and
// always reads an instance matrix.
static int ae3d_vk_create_defaults(void) {
    unsigned char white[4];
    float identity[20];
    int i;

    white[0] = 255; white[1] = 255; white[2] = 255; white[3] = 255;
    vk.default_texture = ae3d_vk_texture_create(1, 1, white);
    if (!vk.default_texture) return ae3d_vk_fail("could not create the default texture");

    memset(identity, 0, sizeof(identity));
    identity[0] = 1.0f; identity[5] = 1.0f; identity[10] = 1.0f; identity[15] = 1.0f;
    identity[16] = 1.0f; identity[17] = 1.0f; identity[18] = 1.0f;

    for (i = 0; i < 16; i++) {
        if (identity[i] != identity[i]) return ae3d_vk_fail("identity is not finite");
    }
    if (!ae3d_vk_upload_buffer(identity, sizeof(identity), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               &vk.identity_instance, &vk.identity_instance_memory)) {
        return 0;
    }
    {
        float empty[8];
        memset(empty, 0, sizeof(empty));
        if (!ae3d_vk_upload_buffer(empty, sizeof(empty), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   &vk.empty_skin, &vk.empty_skin_memory)) {
            return 0;
        }
    }
    return 1;
}

static int ae3d_vk_create_commands(void) {
    VkCommandPoolCreateInfo pool;
    VkCommandBufferAllocateInfo allocation;
    VkSemaphoreCreateInfo semaphore;
    VkFenceCreateInfo fence;
    unsigned i;
    VkResult result;

    memset(&pool, 0, sizeof(pool));
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = vk.graphics_family;
    result = ae3d_vkCreateCommandPool(vk.device, &pool, NULL, &vk.command_pool);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateCommandPool failed", result);

    memset(&allocation, 0, sizeof(allocation));
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = vk.command_pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = AE3D_VK_FRAMES;
    result = ae3d_vkAllocateCommandBuffers(vk.device, &allocation, vk.command_buffers);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkAllocateCommandBuffers failed", result);

    memset(&semaphore, 0, sizeof(semaphore));
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    memset(&fence, 0, sizeof(fence));
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (i = 0; i < AE3D_VK_FRAMES; i++) {
        if (ae3d_vkCreateSemaphore(vk.device, &semaphore, NULL, &vk.image_available[i]) != VK_SUCCESS ||
            ae3d_vkCreateSemaphore(vk.device, &semaphore, NULL, &vk.render_finished[i]) != VK_SUCCESS ||
            ae3d_vkCreateFence(vk.device, &fence, NULL, &vk.in_flight[i]) != VK_SUCCESS) {
            return ae3d_vk_fail("could not create frame synchronisation objects");
        }
        vk.stamped[i] = 0;
    }

    if (vk.timestamps_usable) {
        VkQueryPoolCreateInfo pool;
        memset(&pool, 0, sizeof(pool));
        pool.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        pool.queryType = VK_QUERY_TYPE_TIMESTAMP;
        pool.queryCount = AE3D_VK_FRAMES * AE3D_VK_STAMPS;
        if (ae3d_vkCreateQueryPool(vk.device, &pool, NULL, &vk.timestamps) != VK_SUCCESS) {
            /* Not fatal: a frame without a timer still draws. */
            vk.timestamps = VK_NULL_HANDLE;
            vk.timestamps_usable = 0;
        }
    }
    return 1;
}

/* One timestamp, at the next slot of this frame's four. Bottom of pipe, so
   the stamp lands when everything recorded before it has finished. */
static void ae3d_vk_stamp(void) {
    if (!vk.timestamps || vk.stamp_next >= AE3D_VK_STAMPS) return;
    ae3d_vkCmdWriteTimestamp(vk.command_buffers[vk.frame], VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             vk.timestamps, vk.frame * AE3D_VK_STAMPS + (unsigned)vk.stamp_next);
    vk.stamp_next++;
}

/* Writes every stamp up to and including this one. A pass that did not run
   gets two stamps in the same place and costs nothing, rather than being
   charged for whatever ran instead of it. */
static void ae3d_vk_stamp_through(int index) {
    while (vk.stamp_next <= index) ae3d_vk_stamp();
}

/* Reads back the stamps this frame slot wrote last time round. Called after
   the slot's fence has been waited on, so the results exist and nothing here
   waits for the GPU. */
static void ae3d_vk_collect_stamps(void) {
    unsigned long long ticks[AE3D_VK_STAMPS];
    int i;
    if (!vk.stamped[vk.frame]) return;
    vk.stamped[vk.frame] = 0;
    if (ae3d_vkGetQueryPoolResults(vk.device, vk.timestamps, vk.frame * AE3D_VK_STAMPS, AE3D_VK_STAMPS,
                                   sizeof(ticks), ticks, sizeof(ticks[0]),
                                   VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
        return;
    }
    for (i = 0; i < 3; i++) {
        vk.pass_ms[i] = ticks[i + 1] >= ticks[i]
            ? (double)(ticks[i + 1] - ticks[i]) * vk.timestamp_ms : 0.0;
    }
}

double ae3d_vk_pass_ms(int pass) {
    if (pass < 0 || pass > 2) return 0.0;
    return vk.pass_ms[pass];
}

int ae3d_vk_pipeline_binds(void) { return vk.pipeline_binds; }
int ae3d_vk_set_binds(void) { return vk.set_binds; }

// A null window means offscreen: no surface, no swapchain, and the frame is
// read back rather than presented. That is what lets the editor host the
// Vulkan renderer inside a toolkit that owns the real window.
int ae3d_vk_init(void *win, int width, int height) {
    // Whether shadows and post-processing are wanted are settings rather than
    // device state, and a program sets them before the device exists. Clearing
    // them here discarded the request: the demo asked for FXAA and bloom and
    // the Vulkan frame drew without either, and nothing said so until the
    // channel reported which passes had run.
    int shadows = vk.shadow_enabled;
    int fxaa = vk.fxaa, bloom = vk.bloom;
    float bloom_threshold = vk.bloom_threshold, bloom_intensity = vk.bloom_intensity;

    if (vk.ready) return 1;
    if (!ae3d_vk_available()) return 0;

    memset(&vk, 0, sizeof(vk));
    vk.shadow_enabled = shadows;
    vk.fxaa = fxaa;
    vk.bloom = bloom;
    vk.bloom_threshold = bloom_threshold;
    vk.bloom_intensity = bloom_intensity;
    vk.offscreen = win == NULL;
    vk.readback_frame = -1;
    vk.capture_slot = -1;

    if (!ae3d_vk_create_instance()) return 0;

    if (!vk.offscreen && !ae3d_vk_create_surface(win)) return 0;

    if (!ae3d_vk_pick_device()) return 0;
    if (!ae3d_vk_create_device()) return 0;
    vk.samples = ae3d_vk_pick_samples();
    if (!ae3d_vk_choose_surface_format()) return 0;
    if (vk.offscreen) {
        if (!ae3d_vk_create_offscreen_target(width, height)) return 0;
    } else {
        if (!ae3d_vk_create_swapchain(width, height)) return 0;
    }
    if (!ae3d_vk_create_render_pass()) return 0;
    if (!ae3d_vk_create_framebuffers()) return 0;
    if (!ae3d_vk_create_commands()) return 0;
    if (!ae3d_vk_create_descriptors()) return 0;
    if (!ae3d_vk_create_defaults()) return 0;
    if (!ae3d_vk_create_pipeline()) return 0;

    snprintf(g_vk_error, sizeof(g_vk_error), "%s", "");
    vk.ready = 1;
    return 1;
}

// Offscreen has no surface, so it rebuilds the image it renders into rather
// than a swapchain. Taking the surface path here dereferenced a null surface.
static int ae3d_vk_rebuild_swapchain(int width, int height) {
    unsigned frame;

    ae3d_vkDeviceWaitIdle(vk.device);

    // Descriptor sets are cached per texture and the rebuild destroys the
    // post-processing texture, so a set handed out before the resize would name
    // an image that no longer exists. The cache goes with the images.
    if (vk.descriptor_pool) ae3d_vkResetDescriptorPool(vk.device, vk.descriptor_pool, 0);
    for (frame = 0; frame < AE3D_VK_FRAMES; frame++) vk.set_count[frame] = 0;

    ae3d_vk_destroy_swapchain();
    if (vk.offscreen) {
        if (!ae3d_vk_create_offscreen_target(width, height)) return 0;
    } else {
        if (!ae3d_vk_create_swapchain(width, height)) return 0;
    }
    if (!ae3d_vk_create_framebuffers()) return 0;
    vk.needs_resize = 0;
    return 1;
}

void ae3d_vk_resize(int width, int height) {
    if (!vk.ready) return;
    vk.needs_resize = 1;
    vk.pending_width = width;
    vk.pending_height = height;
}

// The scene pass is opened by the first scene draw rather than here, so a
// shadow pass can be recorded into the same command buffer ahead of it. Vulkan
// forbids nesting render passes, and a shadow map has to be finished before the
// pass that samples it starts.
static void ae3d_vk_open_scene_pass(void) {
    VkRenderPassBeginInfo pass;
    VkClearValue clears[2];
    VkViewport viewport;
    VkRect2D scissor;

    if (vk.pass_open) return;
    ae3d_vk_stamp_through(1);

    memset(clears, 0, sizeof(clears));
    clears[0].color.float32[0] = vk.scene_clear[0];
    clears[0].color.float32[1] = vk.scene_clear[1];
    clears[0].color.float32[2] = vk.scene_clear[2];
    clears[0].color.float32[3] = vk.scene_clear[3];
    clears[1].depthStencil.depth = 1.0f;

    memset(&pass, 0, sizeof(pass));
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = vk.post_active ? vk.scene_pass : vk.render_pass;
    pass.framebuffer = vk.post_active ? vk.scene_framebuffer : vk.framebuffers[vk.image_index];
    pass.renderArea.extent = vk.extent;
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    ae3d_vkCmdBeginRenderPass(vk.command_buffers[vk.frame], &pass, VK_SUBPASS_CONTENTS_INLINE);

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)vk.extent.width;
    viewport.height = (float)vk.extent.height;
    viewport.maxDepth = 1.0f;
    ae3d_vkCmdSetViewport(vk.command_buffers[vk.frame], 0, 1, &viewport);

    memset(&scissor, 0, sizeof(scissor));
    scissor.extent = vk.extent;
    ae3d_vkCmdSetScissor(vk.command_buffers[vk.frame], 0, 1, &scissor);
    vk.pass_open = 1;
}

int ae3d_vk_frame_begin(double r, double g, double b, double a) {
    VkCommandBufferBeginInfo begin;
    VkResult result;

    if (!vk.ready || vk.recording) return 0;

    if (vk.needs_resize) {
        if (!ae3d_vk_rebuild_swapchain(vk.pending_width, vk.pending_height)) return 0;
    }

    ae3d_vkWaitForFences(vk.device, 1, &vk.in_flight[vk.frame], VK_TRUE, UINT64_MAX);
    ae3d_vk_collect_stamps();

    if (vk.offscreen) {
        vk.image_index = 0;
    } else {
        result = ae3d_vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX,
                                            vk.image_available[vk.frame], VK_NULL_HANDLE,
                                            &vk.image_index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!ae3d_vk_rebuild_swapchain((int)vk.extent.width, (int)vk.extent.height)) return 0;
            return 0;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            return ae3d_vk_fail_code("vkAcquireNextImageKHR failed", result);
        }
    }

    ae3d_vkResetFences(vk.device, 1, &vk.in_flight[vk.frame]);
    ae3d_vkResetCommandBuffer(vk.command_buffers[vk.frame], 0);

    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ae3d_vkBeginCommandBuffer(vk.command_buffers[vk.frame], &begin);

    vk.stamp_next = 0;
    if (vk.timestamps) {
        ae3d_vkCmdResetQueryPool(vk.command_buffers[vk.frame], vk.timestamps,
                                 vk.frame * AE3D_VK_STAMPS, AE3D_VK_STAMPS);
        ae3d_vk_stamp();
    }
    vk.bound_pipeline = VK_NULL_HANDLE;
    vk.bound_texture = -1;
    vk.bound_normal = -1;
    vk.pipeline_binds = 0;
    vk.set_binds = 0;

    vk.scene_clear[0] = (float)r;
    vk.scene_clear[1] = (float)g;
    vk.scene_clear[2] = (float)b;
    vk.scene_clear[3] = (float)a;

    vk.post_active = (vk.fxaa || vk.bloom || vk.ssr_enabled) && vk.screen_quad > 0 && vk.post_texture > 0;
    vk.pass_open = 0;
    vk.in_shadow_pass = 0;

    vk.uniforms[vk.frame].used = 0;
    vk.draw_calls = 0;
    vk.recording = 1;
    return 1;
}

int ae3d_vk_draw_calls(void) { return vk.draw_calls; }

int ae3d_vk_sample_count(void) { return (int)vk.samples; }

// Vulkan clip space puts y downward and z in [0, 1]; the engine's matrices are
// OpenGL-shaped, so the correction is folded into the MVP here rather than

// One draw takes a slice of the frame's uniform buffer, so the scene block the
// caller has been filling is snapshotted per draw rather than shared.
static void ae3d_vk_draw_pipeline(VkPipeline pipeline, int handle, int texture_handle,
                                  int instance_handle, int instance_count) {
    /* Whether the pipeline names binding 2 is decided by the mesh, which is
       what chose the pipeline: the two cannot disagree. */
    ae3d_vk_uniform_ring *ring;
    VkDescriptorSet set;
    VkDeviceSize offsets[1];
    ae3d_vk_mesh *mesh;
    unsigned slot;
    unsigned dynamic_offset;

    if (!vk.recording || handle <= 0 || handle > vk.mesh_capacity) return;
    ae3d_vk_open_scene_pass();
    mesh = &vk.meshes[handle - 1];
    if (!mesh->in_use || mesh->index_count == 0) return;

    if (texture_handle < 1 || texture_handle > AE3D_VK_MAX_TEXTURES) texture_handle = vk.default_texture;
    if (!vk.textures[texture_handle - 1].in_use) texture_handle = vk.default_texture;

    ring = &vk.uniforms[vk.frame];
    if (ring->used >= ring->capacity) return;
    slot = ring->used++;
    dynamic_offset = slot * ring->stride;
    memcpy(ring->mapped + dynamic_offset, vk.scene[vk.program].bytes, AE3D_VK_SCENE_SIZE);

    set = ae3d_vk_set_for((int)vk.frame, texture_handle, vk.normal_map, vk.pose_bank);
    if (set == VK_NULL_HANDLE) return;

    if (pipeline != vk.bound_pipeline) {
        ae3d_vkCmdBindPipeline(vk.command_buffers[vk.frame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vk.bound_pipeline = pipeline;
        vk.pipeline_binds++;
    }
    /* The set is bound every draw because its dynamic offset moves every
       draw; what is counted is the material behind it changing. */
    if (texture_handle != vk.bound_texture || vk.normal_map != vk.bound_normal) {
        vk.bound_texture = texture_handle;
        vk.bound_normal = vk.normal_map;
        vk.set_binds++;
    }
    ae3d_vkCmdBindDescriptorSets(vk.command_buffers[vk.frame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk.pipeline_layout, 0, 1, &set, 1, &dynamic_offset);

    offsets[0] = 0;
    ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 0, 1, &mesh->vertex_buffer, offsets);

    if (instance_handle > 0 && instance_handle <= vk.mesh_capacity && instance_count > 0) {
        ae3d_vk_mesh *instances = &vk.meshes[instance_handle - 1];
        if (instances->in_use) {
            ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 1, 1,
                                        &instances->vertex_buffer, offsets);
        }
    } else {
        ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 1, 1,
                                    &vk.identity_instance, offsets);
        instance_count = 1;
    }

    if (mesh->skinned && mesh->skin_buffer) {
        ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 2, 1,
                                    &mesh->skin_buffer, offsets);
    } else {
        ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 2, 1,
                                    &vk.empty_skin, offsets);
    }

    ae3d_vkCmdBindIndexBuffer(vk.command_buffers[vk.frame], mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    ae3d_vkCmdDrawIndexed(vk.command_buffers[vk.frame], mesh->index_count,
                          (unsigned)instance_count, 0, 0, 0);
    vk.draw_calls++;
}

// Which family of pipelines the next draws use. A program the backend does not
// have falls back to the scene one, which is what the OpenGL backend does with
// a shader that fails to compile.
void ae3d_vk_set_program(int program) {
    vk.program = (program >= 0 && program < AE3D_VK_PROGRAM_COUNT) ? program : AE3D_VK_PROGRAM_SCENE;
}

int ae3d_vk_program_count(void) { return AE3D_VK_PROGRAM_COUNT; }

void ae3d_vk_set_face_culling(int on) { vk.cull = on ? 1 : 0; }

static int ae3d_vk_instances_are_points(int instance_handle, int instance_count) {
    if (instance_count <= 0 || instance_handle <= 0 || instance_handle > vk.mesh_capacity) return 0;
    return vk.meshes[instance_handle - 1].in_use && vk.meshes[instance_handle - 1].points;
}

void ae3d_vk_draw(int handle, int texture_handle, int instance_handle, int instance_count) {
    VkPipeline opaque = vk.pipeline[vk.cull];
    VkPipeline blended = vk.pipeline_blend;
    int skinned = handle > 0 && handle <= vk.mesh_capacity && vk.meshes[handle - 1].skinned;

    if (ae3d_vk_instances_are_points(instance_handle, instance_count)
        && vk.point_pipeline[vk.cull] && vk.point_pipeline_blend) {
        opaque = vk.point_pipeline[vk.cull];
        blended = vk.point_pipeline_blend;
    } else if (vk.program == AE3D_VK_PROGRAM_WATER && vk.water_pipeline[vk.cull]
        && vk.water_pipeline_blend) {
        opaque = vk.water_pipeline[vk.cull];
        blended = vk.water_pipeline_blend;
    } else if (skinned && vk.pose_bank > 0 && vk.crowd_pipeline[vk.cull]
               && vk.crowd_pipeline_blend) {
        opaque = vk.crowd_pipeline[vk.cull];
        blended = vk.crowd_pipeline_blend;
    } else if (skinned && vk.skinned_pipeline[vk.cull] && vk.skinned_pipeline_blend) {
        opaque = vk.skinned_pipeline[vk.cull];
        blended = vk.skinned_pipeline_blend;
    }
    ae3d_vk_draw_pipeline(vk.blend ? blended : opaque, handle, texture_handle,
                          instance_handle, instance_count);
}

// A flat sky needs no geometry: the clear colour already fills every pixel the
// scene does not cover, so only a textured sky is drawn.
void ae3d_vk_draw_sky(int mesh_handle, int texture_handle) {
    if (!vk.sky_pipeline || texture_handle <= 0) return;
    vk.program = AE3D_VK_PROGRAM_SCENE;
    ae3d_vk_draw_pipeline(vk.sky_pipeline, mesh_handle, texture_handle, 0, 1);
}

void ae3d_vk_set_screen_quad(int mesh_handle) { vk.screen_quad = mesh_handle; }

// Descriptor sets are cached per texture, so binding 2 keeps whatever image it
// was written with. Toggling shadows rewrites it on every set already handed
// out, otherwise the scene would sample the previous state forever.
void ae3d_vk_set_shadows(int on) {
    VkDescriptorImageInfo shadow;
    VkWriteDescriptorSet write;
    int frame;
    int index;

    on = on ? 1 : 0;
    if (vk.shadow_enabled == on) return;
    vk.shadow_enabled = on;
    if (!vk.ready) return;

    ae3d_vkDeviceWaitIdle(vk.device);

    memset(&shadow, 0, sizeof(shadow));
    shadow.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (vk.shadow_enabled && vk.shadow_view) {
        shadow.imageView = vk.shadow_view;
        shadow.sampler = vk.shadow_sampler;
    } else {
        shadow.imageView = vk.textures[vk.default_texture - 1].view;
        shadow.sampler = vk.textures[vk.default_texture - 1].sampler;
    }

    for (frame = 0; frame < AE3D_VK_FRAMES; frame++) {
        for (index = 0; index < vk.set_count[frame]; index++) {
            memset(&write, 0, sizeof(write));
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = vk.sets[frame][index];
            write.dstBinding = 2;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &shadow;
            ae3d_vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
        }
    }
}

int ae3d_vk_shadows(void) { return vk.shadow_enabled; }

// Turn screen-space reflection on or off. The camera-depth target it samples is
// built the first time it is turned on, and rebuilt if the swapchain has since
// been remade at a different size, so a scene that never asks for SSR allocates
// nothing. The march itself is wired on top of this in a later step; for now
// this owns the target's lifetime.
void ae3d_vk_set_ssr(int on) {
    on = on ? 1 : 0;
    vk.ssr_enabled = on;
    if (!vk.ready || !on) return;
    if (!vk.camdepth_framebuffer ||
        vk.camdepth_width != (int)vk.extent.width ||
        vk.camdepth_height != (int)vk.extent.height) {
        ae3d_vkDeviceWaitIdle(vk.device);
        ae3d_vk_destroy_camdepth_target();
        ae3d_vk_create_camdepth_target((int)vk.extent.width, (int)vk.extent.height);
    }
}

int ae3d_vk_ssr(void) { return vk.ssr_enabled; }

/* Ambient occlusion on or off, with how far a thing shadows its neighbours
   (metres) and how dark that goes. The camera depth it reads is the
   reflection's target, made the same way when it is first wanted. */
void ae3d_vk_set_ssao(int on, double radius, double intensity) {
    on = on ? 1 : 0;
    vk.ssao_enabled = on;
    vk.ssao_radius = (float)radius;
    vk.ssao_intensity = (float)intensity;
    if (!vk.ready || !on) return;
    if (!vk.camdepth_framebuffer ||
        vk.camdepth_width != (int)vk.extent.width ||
        vk.camdepth_height != (int)vk.extent.height) {
        ae3d_vkDeviceWaitIdle(vk.device);
        ae3d_vk_destroy_camdepth_target();
        ae3d_vk_create_camdepth_target((int)vk.extent.width, (int)vk.extent.height);
    }
}

int ae3d_vk_ssao(void) { return vk.ssao_enabled; }

/* The one-pixel white texture every set falls back to, for a draw that has
   to bind an image it does not read: the sky drawn from the sun. */
int ae3d_vk_default_texture(void) { return vk.default_texture; }

/* The camera depth drawn for its own sake: a water surface reads the scene's
   depth under it for its shore, whether or not the road reflects. The same
   target the reflection uses, made the same way. */
void ae3d_vk_set_scene_depth(int on) {
    on = on ? 1 : 0;
    vk.scene_depth_wanted = on;
    if (!vk.ready || !on) return;
    if (!vk.camdepth_framebuffer ||
        vk.camdepth_width != (int)vk.extent.width ||
        vk.camdepth_height != (int)vk.extent.height) {
        ae3d_vkDeviceWaitIdle(vk.device);
        ae3d_vk_destroy_camdepth_target();
        ae3d_vk_create_camdepth_target((int)vk.extent.width, (int)vk.extent.height);
    }
}

int ae3d_vk_scene_depth_ready(void) {
    return vk.camdepth_framebuffer != VK_NULL_HANDLE && vk.camdepth_view != VK_NULL_HANDLE;
}

// The pass leaves the map in shader-read layout, so the scene pass that follows
// in the same command buffer samples it without a barrier of its own.
int ae3d_vk_shadow_begin(void) {
    VkRenderPassBeginInfo pass;
    VkClearValue clears[2];
    VkViewport viewport;
    VkRect2D scissor;

    if (!vk.recording || !vk.shadow_enabled || !vk.shadow_framebuffer) return 0;
    if (vk.pass_open) return 0;

    memset(clears, 0, sizeof(clears));
    clears[0].depthStencil.depth = 1.0f;

    memset(&pass, 0, sizeof(pass));
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = vk.shadow_pass;
    pass.framebuffer = vk.shadow_framebuffer;
    pass.renderArea.extent.width = AE3D_VK_SHADOW_SIZE;
    pass.renderArea.extent.height = AE3D_VK_SHADOW_SIZE;
    pass.clearValueCount = 1;
    pass.pClearValues = clears;
    ae3d_vkCmdBeginRenderPass(vk.command_buffers[vk.frame], &pass, VK_SUBPASS_CONTENTS_INLINE);

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)AE3D_VK_SHADOW_SIZE;
    viewport.height = (float)AE3D_VK_SHADOW_SIZE;
    viewport.maxDepth = 1.0f;
    ae3d_vkCmdSetViewport(vk.command_buffers[vk.frame], 0, 1, &viewport);

    memset(&scissor, 0, sizeof(scissor));
    scissor.extent.width = AE3D_VK_SHADOW_SIZE;
    scissor.extent.height = AE3D_VK_SHADOW_SIZE;
    ae3d_vkCmdSetScissor(vk.command_buffers[vk.frame], 0, 1, &scissor);
    vk.in_shadow_pass = 1;
    vk.pass_open = 1;
    return 1;
}

void ae3d_vk_shadow_draw(int mesh_handle, int instance_handle, int instance_count) {
    VkPipeline pipeline = vk.shadow_pipeline;
    // Also draws in the camera-depth prepass: it is depth-only from the same
    // geometry, and its render pass is compatible with the shadow one, so the
    // shadow depth pipeline is exactly the pipeline it wants.
    if (!vk.shadow_pipeline || (!vk.in_shadow_pass && !vk.in_camdepth)) return;
    if (ae3d_vk_instances_are_points(instance_handle, instance_count) && vk.point_shadow_pipeline) {
        pipeline = vk.point_shadow_pipeline;
    } else if (mesh_handle > 0 && mesh_handle <= vk.mesh_capacity
        && vk.meshes[mesh_handle - 1].skinned && vk.skinned_shadow_pipeline) {
        pipeline = vk.skinned_shadow_pipeline;
        /* A crowd casts from the poses its bank gives each instance, or
           its shadows would stand in the bind pose under walking figures. */
        if (vk.pose_bank > 0 && vk.crowd_shadow_pipeline) pipeline = vk.crowd_shadow_pipeline;
    }
    vk.program = AE3D_VK_PROGRAM_SCENE;
    ae3d_vk_draw_pipeline(pipeline, mesh_handle, vk.default_texture,
                          instance_handle, instance_count);
}

void ae3d_vk_shadow_end(void) {
    VkViewport viewport;
    VkRect2D scissor;

    if (!vk.recording || !vk.in_shadow_pass) return;
    ae3d_vkCmdEndRenderPass(vk.command_buffers[vk.frame]);
    vk.in_shadow_pass = 0;
    vk.pass_open = 0;
    ae3d_vk_stamp_through(1);

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)vk.extent.width;
    viewport.height = (float)vk.extent.height;
    viewport.maxDepth = 1.0f;
    ae3d_vkCmdSetViewport(vk.command_buffers[vk.frame], 0, 1, &viewport);

    memset(&scissor, 0, sizeof(scissor));
    scissor.extent = vk.extent;
    ae3d_vkCmdSetScissor(vk.command_buffers[vk.frame], 0, 1, &scissor);
}

// The camera-space depth prepass. Same shape as the shadow pass -- begin the
// depth-only pass, draw the casters (with the camera view-projection in the
// light-space slot the depth shader reads), end -- into the full-size camdepth
// target the SSR pass samples. Only when SSR is on and the target exists.
int ae3d_vk_camdepth_begin(void) {
    VkRenderPassBeginInfo pass;
    VkClearValue clear;
    VkViewport viewport;
    VkRect2D scissor;

    if (!vk.recording || !(vk.ssr_enabled || vk.scene_depth_wanted || vk.ssao_enabled)) return 0;
    /* The target is made when a feature that reads it is turned on, but a
       scene turns the occlusion on at start, before there is a swapchain
       to size it by; so it is made here too, at the frame that first
       needs it, and remade when the swapchain has changed size. */
    if (!vk.camdepth_framebuffer ||
        vk.camdepth_width != (int)vk.extent.width ||
        vk.camdepth_height != (int)vk.extent.height) {
        ae3d_vkDeviceWaitIdle(vk.device);
        ae3d_vk_destroy_camdepth_target();
        if (!ae3d_vk_create_camdepth_target((int)vk.extent.width, (int)vk.extent.height)) return 0;
    }
    if (!vk.camdepth_framebuffer) return 0;
    if (vk.pass_open) return 0;

    memset(&clear, 0, sizeof(clear));
    clear.depthStencil.depth = 1.0f;

    memset(&pass, 0, sizeof(pass));
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = vk.camdepth_pass;
    pass.framebuffer = vk.camdepth_framebuffer;
    pass.renderArea.extent = vk.extent;
    pass.clearValueCount = 1;
    pass.pClearValues = &clear;
    ae3d_vkCmdBeginRenderPass(vk.command_buffers[vk.frame], &pass, VK_SUBPASS_CONTENTS_INLINE);

    memset(&viewport, 0, sizeof(viewport));
    viewport.width = (float)vk.extent.width;
    viewport.height = (float)vk.extent.height;
    viewport.maxDepth = 1.0f;
    ae3d_vkCmdSetViewport(vk.command_buffers[vk.frame], 0, 1, &viewport);

    memset(&scissor, 0, sizeof(scissor));
    scissor.extent = vk.extent;
    ae3d_vkCmdSetScissor(vk.command_buffers[vk.frame], 0, 1, &scissor);
    vk.in_camdepth = 1;
    vk.pass_open = 1;
    return 1;
}

void ae3d_vk_camdepth_end(void) {
    if (!vk.recording || !vk.in_camdepth) return;
    ae3d_vkCmdEndRenderPass(vk.command_buffers[vk.frame]);
    vk.in_camdepth = 0;
    vk.pass_open = 0;
    // Viewport and scissor are already the full extent; the scene pass sets its
    // own besides.
}

void ae3d_vk_set_post(int fxaa, int bloom, double threshold, double intensity) {
    vk.fxaa = fxaa;
    vk.bloom = bloom;
    vk.bloom_threshold = (float)threshold;
    vk.bloom_intensity = (float)intensity;
}

int ae3d_vk_post_active(void) { return vk.post_active; }

/* The bloom intensity on its own, so a caller can read it, dial it out to see
 * what the bloom is worth, and put it back, without knowing the other post
 * parameters the scene set. Every bloom term scales by it, so zero is off with
 * the pipeline otherwise unchanged. */
void ae3d_vk_set_bloom_intensity(double intensity) {
    vk.bloom_intensity = (float)intensity;
}

double ae3d_vk_bloom_intensity(void) { return vk.bloom_intensity; }

// Post parameters are written here rather than by the caller because the scene
// draws share this block and a material's own bloom settings would otherwise be
// what the composite pass read.
// The sky, the shadow pass and the composites all read the scene program's
// block. Whichever program the last model selected is still current here, and a
// composite reading the water block gets nonsense for its edge thresholds.
void ae3d_vk_set_ssr_params(double road_height, double strength) {
    vk.ssr_road_height = (float)road_height;
    vk.ssr_strength = (float)strength;
}

// The SSR composite draw. Its own descriptor set puts the scene colour at
// binding 1 and the camera depth at binding 2 (where the scene sets keep the
// shadow map), and it reads the same uniform block as everything else, so the
// SSR shader gets viewProjection, invViewProjection and the road parameters
// the frame set. Written every draw because both images are remade on resize.
static void ae3d_vk_draw_screen(VkPipeline pipeline, VkDescriptorSet *set_slot, int colour_texture) {
    VkDescriptorSetAllocateInfo alloc;
    VkDescriptorBufferInfo buffer;
    VkDescriptorImageInfo scene_img, depth_img, def_img;
    VkWriteDescriptorSet writes[4];
    VkDeviceSize offsets[1];
    ae3d_vk_uniform_ring *ring;
    ae3d_vk_mesh *mesh;
    ae3d_vk_texture *scene_tex;
    VkDescriptorSet set;
    unsigned slot, dynamic_offset;

    if (!pipeline || vk.screen_quad <= 0 || vk.screen_quad > vk.mesh_capacity) return;
    if (!vk.camdepth_view || colour_texture <= 0 || colour_texture > AE3D_VK_MAX_TEXTURES) return;
    mesh = &vk.meshes[vk.screen_quad - 1];
    if (!mesh->in_use || mesh->index_count == 0) return;
    scene_tex = &vk.textures[colour_texture - 1];
    if (!scene_tex->in_use) return;

    if (!*set_slot) {
        memset(&alloc, 0, sizeof(alloc));
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = vk.descriptor_pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &vk.set_layout;
        if (ae3d_vkAllocateDescriptorSets(vk.device, &alloc, &set) != VK_SUCCESS) return;
        *set_slot = set;
    }
    set = *set_slot;

    ring = &vk.uniforms[vk.frame];
    if (ring->used >= ring->capacity) return;
    slot = ring->used++;
    dynamic_offset = slot * ring->stride;
    memcpy(ring->mapped + dynamic_offset, vk.scene[AE3D_VK_PROGRAM_SCENE].bytes, AE3D_VK_SCENE_SIZE);

    memset(&buffer, 0, sizeof(buffer));
    buffer.buffer = ring->buffer;
    buffer.offset = 0;
    buffer.range = AE3D_VK_SCENE_SIZE;

    memset(&scene_img, 0, sizeof(scene_img));
    scene_img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    scene_img.imageView = scene_tex->view;
    scene_img.sampler = scene_tex->sampler;

    memset(&depth_img, 0, sizeof(depth_img));
    depth_img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    depth_img.imageView = vk.camdepth_view;
    depth_img.sampler = vk.camdepth_sampler;

    memset(&def_img, 0, sizeof(def_img));
    def_img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    def_img.imageView = vk.textures[vk.default_texture - 1].view;
    def_img.sampler = vk.textures[vk.default_texture - 1].sampler;

    memset(writes, 0, sizeof(writes));
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writes[0].pBufferInfo = &buffer;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set; writes[1].dstBinding = 1; writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &scene_img;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = set; writes[2].dstBinding = 2; writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &depth_img;
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = set; writes[3].dstBinding = 3; writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &def_img;
    ae3d_vkUpdateDescriptorSets(vk.device, 4, writes, 0, NULL);

    if (pipeline != vk.bound_pipeline) {
        ae3d_vkCmdBindPipeline(vk.command_buffers[vk.frame], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vk.bound_pipeline = pipeline;
        vk.pipeline_binds++;
    }
    ae3d_vkCmdBindDescriptorSets(vk.command_buffers[vk.frame], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk.pipeline_layout, 0, 1, &set, 1, &dynamic_offset);
    offsets[0] = 0;
    ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 0, 1, &mesh->vertex_buffer, offsets);
    ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 1, 1, &vk.identity_instance, offsets);
    ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 2, 1, &vk.empty_skin, offsets);
    ae3d_vkCmdBindIndexBuffer(vk.command_buffers[vk.frame], mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    ae3d_vkCmdDrawIndexed(vk.command_buffers[vk.frame], mesh->index_count, 1, 0, 0, 0);
    vk.draw_calls++;
}

// The post uniforms live in the shared scene block, so both the reflective
// composite and the ordinary one read them; set them once before either draws.
static void ae3d_vk_setup_post_uniforms(void) {
    float texel_x = vk.extent.width ? 1.0f / (float)vk.extent.width : 0.0f;
    float texel_y = vk.extent.height ? 1.0f / (float)vk.extent.height : 0.0f;
    float texel[2];

    texel[0] = texel_x;
    texel[1] = texel_y;
    memcpy(vk.scene[AE3D_VK_PROGRAM_SCENE].bytes + AE3D_VK_OFF_TEXELSIZE, texel, sizeof(texel));
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_BLOOMTHRESHOLD, vk.bloom_threshold);
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_BLOOMINTENSITY, vk.bloom_intensity);
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_EDGETHRESHOLD, 0.125f);
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_EDGETHRESHOLDMIN, 0.0625f);
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_SUBPIXELQUALITY, 0.75f);
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_SSRROADHEIGHT, vk.ssr_road_height);
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_SSRSTRENGTH, vk.ssr_strength);
    vk.program = AE3D_VK_PROGRAM_SCENE;
}

// The occlusion, over the opaque scene: a multiply drawn inside the scene
// pass with depth off, reading the camera depth the prepass drew. Nothing
// when the occlusion is off, the prepass did not run, or the pipeline did
// not build; the scene is then simply not darkened.
void ae3d_vk_draw_ssao(void) {
    if (!vk.recording || !vk.ssao_enabled || !vk.ssao_pipeline) return;
    if (!vk.camdepth_view || !vk.camdepth_framebuffer || vk.default_texture <= 0) return;
    ae3d_vk_open_scene_pass();
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_SSAORADIUS, vk.ssao_radius);
    ae3d_vk_set_float(&vk.scene[AE3D_VK_PROGRAM_SCENE], AE3D_VK_OFF_SSAOINTENSITY, vk.ssao_intensity);
    ae3d_vk_draw_screen(vk.ssao_pipeline, &vk.ssao_set[(int)vk.frame], vk.default_texture);
}

// The ordinary composite: bloom, else fxaa, else a straight copy, reading the
// given colour source -- the scene directly, or the reflected intermediate when
// SSR ran a stage before it.
static void ae3d_vk_draw_composite(int source_texture) {
    int index = vk.bloom && vk.post_pipelines[2] ? 2 : (vk.fxaa && vk.post_pipelines[1] ? 1 : 0);
    ae3d_vk_draw_pipeline(vk.post_pipelines[index], vk.screen_quad, source_texture, 0, 1);
}

int ae3d_vk_frame_end(void) {
    VkSubmitInfo submit;
    VkPresentInfoKHR present;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkResult result;
    int frame_slot = (int)vk.frame;

    if (!vk.recording) return 0;

    ae3d_vk_open_scene_pass();
    ae3d_vkCmdEndRenderPass(vk.command_buffers[vk.frame]);
    vk.pass_open = 0;
    ae3d_vk_stamp_through(2);

    if (vk.post_active) {
        VkRenderPassBeginInfo pass;
        VkViewport viewport;
        VkRect2D scissor;
        // The reflective stage runs when SSR is on and its off-screen targets
        // are built. It mirrors the scene into the intermediate; the ordinary
        // composite then reads that instead of the scene colour, so the bloom
        // it does picks up the mirrored lamps as well.
        int ssr_now = vk.ssr_enabled && vk.post_pipelines[3] &&
                      vk.ssr_framebuffer && vk.camdepth_framebuffer &&
                      vk.ssr_reflect_texture > 0;

        memset(&viewport, 0, sizeof(viewport));
        viewport.width = (float)vk.extent.width;
        viewport.height = (float)vk.extent.height;
        viewport.maxDepth = 1.0f;
        memset(&scissor, 0, sizeof(scissor));
        scissor.extent = vk.extent;

        ae3d_vk_setup_post_uniforms();

        if (ssr_now) {
            memset(&pass, 0, sizeof(pass));
            pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            pass.renderPass = vk.ssr_pass;
            pass.framebuffer = vk.ssr_framebuffer;
            pass.renderArea.extent = vk.extent;
            ae3d_vkCmdBeginRenderPass(vk.command_buffers[vk.frame], &pass, VK_SUBPASS_CONTENTS_INLINE);
            vk.pass_open = 1;
            ae3d_vkCmdSetViewport(vk.command_buffers[vk.frame], 0, 1, &viewport);
            ae3d_vkCmdSetScissor(vk.command_buffers[vk.frame], 0, 1, &scissor);
            ae3d_vk_draw_screen(vk.post_pipelines[3], &vk.ssr_set[frame_slot], vk.post_texture);
            ae3d_vkCmdEndRenderPass(vk.command_buffers[vk.frame]);
            vk.pass_open = 0;
        }

        memset(&pass, 0, sizeof(pass));
        pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = vk.post_pass;
        pass.framebuffer = vk.post_framebuffers[vk.image_index];
        pass.renderArea.extent = vk.extent;
        ae3d_vkCmdBeginRenderPass(vk.command_buffers[vk.frame], &pass, VK_SUBPASS_CONTENTS_INLINE);
        vk.pass_open = 1;
        ae3d_vkCmdSetViewport(vk.command_buffers[vk.frame], 0, 1, &viewport);
        ae3d_vkCmdSetScissor(vk.command_buffers[vk.frame], 0, 1, &scissor);
        ae3d_vk_draw_composite(ssr_now ? vk.ssr_reflect_texture : vk.post_texture);
        ae3d_vkCmdEndRenderPass(vk.command_buffers[vk.frame]);
        vk.pass_open = 0;
    }

    // Offscreen resolves into an image the pass leaves in transfer-source
    // layout, so the copy to host memory is recorded into the same submission
    // rather than costing a second one.
    if (vk.offscreen) {
        VkBufferImageCopy region;
        memset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = vk.extent.width;
        region.imageExtent.height = vk.extent.height;
        region.imageExtent.depth = 1;
        ae3d_vkCmdCopyImageToBuffer(vk.command_buffers[vk.frame], vk.images[0],
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    vk.readback_buffer[vk.frame], 1, &region);
    }

    // A windowed frame is copied only when a capture was asked for. The
    // swapchain image is in present layout after the render pass, so it is
    // moved to transfer-source, copied to this frame's staging buffer, and put
    // back before it is handed to the presenter.
    if (!vk.offscreen && vk.capture_request && vk.can_capture
        && vk.readback_buffer[vk.frame]) {
        VkImageMemoryBarrier to_src, to_present;
        VkBufferImageCopy region;

        memset(&to_src, 0, sizeof(to_src));
        to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_src.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.image = vk.images[vk.image_index];
        to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_src.subresourceRange.levelCount = 1;
        to_src.subresourceRange.layerCount = 1;
        to_src.srcAccessMask = 0;
        to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        ae3d_vkCmdPipelineBarrier(vk.command_buffers[vk.frame],
                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                                  1, &to_src);

        memset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = vk.extent.width;
        region.imageExtent.height = vk.extent.height;
        region.imageExtent.depth = 1;
        ae3d_vkCmdCopyImageToBuffer(vk.command_buffers[vk.frame], vk.images[vk.image_index],
                                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    vk.readback_buffer[vk.frame], 1, &region);

        memset(&to_present, 0, sizeof(to_present));
        to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = vk.images[vk.image_index];
        to_present.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_present.subresourceRange.levelCount = 1;
        to_present.subresourceRange.layerCount = 1;
        to_present.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        to_present.dstAccessMask = 0;
        ae3d_vkCmdPipelineBarrier(vk.command_buffers[vk.frame],
                                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                                  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
                                  1, &to_present);

        vk.capture_slot = (int)vk.frame;
        vk.capture_request = 0;
    }

    ae3d_vk_stamp_through(3);
    vk.stamped[vk.frame] = vk.stamp_next == AE3D_VK_STAMPS;
    ae3d_vkEndCommandBuffer(vk.command_buffers[vk.frame]);

    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (!vk.offscreen) {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &vk.image_available[vk.frame];
        submit.pWaitDstStageMask = &wait_stage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &vk.render_finished[vk.frame];
    }
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &vk.command_buffers[vk.frame];

    result = ae3d_vkQueueSubmit(vk.graphics_queue, 1, &submit, vk.in_flight[vk.frame]);
    if (result != VK_SUCCESS) {
        vk.recording = 0;
        return ae3d_vk_fail_code("vkQueueSubmit failed", result);
    }

    // Nothing waits here. The copy lands in this frame's own staging buffer, and
    // whoever asks for the pixels waits for it then; a program that renders
    // without reading never stalls at all. Reusing this frame's command buffer
    // is already gated on its fence in ae3d_vk_frame_begin.
    if (vk.offscreen) {
        vk.readback_frame = (int)vk.frame;
        vk.frame = (vk.frame + 1) % AE3D_VK_FRAMES;
        vk.recording = 0;
        return 1;
    }

    memset(&present, 0, sizeof(present));
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &vk.render_finished[vk.frame];
    present.swapchainCount = 1;
    present.pSwapchains = &vk.swapchain;
    present.pImageIndices = &vk.image_index;

    result = ae3d_vkQueuePresentKHR(vk.present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vk.needs_resize = 1;
        vk.pending_width = (int)vk.extent.width;
        vk.pending_height = (int)vk.extent.height;
    } else if (result != VK_SUCCESS) {
        vk.recording = 0;
        return ae3d_vk_fail_code("vkQueuePresentKHR failed", result);
    }

    vk.frame = (vk.frame + 1) % AE3D_VK_FRAMES;
    vk.recording = 0;
    return 1;
}

/* The finished frame, at an address that does not move.

   Readback is double buffered, so the mapped memory the frame landed in
   alternates: handing that out means the caller is given a different pointer
   every frame for what is, to it, the same image. Anything that keeps work
   against the buffer it was given -- a canvas that uploads the image it is
   drawing, say -- then redoes that work on every frame and on every buffer,
   and never hits what it cached. Copying two megabytes costs a fifth of a
   millisecond; in ae3d's own editor, not copying cost a hundred and fifty. */
static unsigned char *g_readback_copy = NULL;
static size_t g_readback_copy_size = 0;

void *ae3d_vk_offscreen_pixels(void) {
    size_t needed;

    if (!vk.offscreen) return NULL;
    if (vk.readback_frame < 0) return NULL;
    ae3d_vkWaitForFences(vk.device, 1, &vk.in_flight[vk.readback_frame], VK_TRUE, UINT64_MAX);

    needed = (size_t)vk.readback_width * (size_t)vk.readback_height * 4u;
    if (needed > g_readback_copy_size) {
        free(g_readback_copy);
        g_readback_copy = (unsigned char *)malloc(needed);
        g_readback_copy_size = g_readback_copy ? needed : 0;
    }
    if (!g_readback_copy) return vk.readback_mapped[vk.readback_frame];

    memcpy(g_readback_copy, vk.readback_mapped[vk.readback_frame], needed);
    return g_readback_copy;
}

/* The size of the frame being drawn, in pixels: the swapchain's extent. */
int ae3d_vk_frame_width(void) { return (int)vk.extent.width; }
int ae3d_vk_frame_height(void) { return (int)vk.extent.height; }

int ae3d_vk_offscreen_width(void) { return vk.readback_width; }
int ae3d_vk_offscreen_height(void) { return vk.readback_height; }

/* Windowed capture: read a presented frame back the way the offscreen path
   reads its target. The staging buffers are made on first use, sized to the
   swapchain extent, and freed with the swapchain. */
static int ae3d_vk_ensure_capture_buffers(void) {
    VkDeviceSize size;
    unsigned slot;
    if (vk.readback_buffer[0]) return 1;   /* already made for this extent */
    size = (VkDeviceSize)vk.extent.width * vk.extent.height * 4;
    for (slot = 0; slot < AE3D_VK_FRAMES; slot++) {
        void *mapped = NULL;
        if (!ae3d_vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                       | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &vk.readback_buffer[slot],
                                   &vk.readback_buffer_memory[slot])) {
            return 0;
        }
        if (ae3d_vkMapMemory(vk.device, vk.readback_buffer_memory[slot], 0, size, 0,
                             &mapped) != VK_SUCCESS) {
            return 0;
        }
        vk.readback_mapped[slot] = (unsigned char *)mapped;
    }
    vk.readback_width = (int)vk.extent.width;
    vk.readback_height = (int)vk.extent.height;
    return 1;
}

/* Arm a capture: the next frame submitted copies the presented image out.
   Returns 0 when the surface does not allow reading a frame back. */
int ae3d_vk_request_capture(void) {
    if (vk.offscreen || !vk.can_capture || !vk.ready) return 0;
    if (!ae3d_vk_ensure_capture_buffers()) return 0;
    vk.capture_slot = -1;
    vk.capture_request = 1;
    return 1;
}

int ae3d_vk_capture_ready(void) { return vk.capture_slot >= 0; }

/* The pose bank the draws after this are posed from, as the handle of a
   float texture made by ae3d_vk_texture_create_float, or zero for none.
   A crowd draw is a skinned draw with a bank bound. */
void ae3d_vk_set_pose_bank(int texture_handle) { vk.pose_bank = texture_handle; }

/* A texture of floats, four a texel, with no filtering and no mipmaps:
   what a pose bank is, fetched by exact texel. */
int ae3d_vk_texture_create_float(int width, int height, const float *rgba) {
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    VkCommandBuffer command;
    VkBufferImageCopy region;
    VkSamplerCreateInfo sampler;
    ae3d_vk_texture *texture = NULL;
    void *mapped = NULL;
    VkDeviceSize size;
    int slot, handle = 0;

    if (!vk.device || width <= 0 || height <= 0 || !rgba) return 0;
    size = (VkDeviceSize)width * height * 4 * sizeof(float);

    for (slot = 0; slot < AE3D_VK_MAX_TEXTURES; slot++) {
        if (!vk.textures[slot].in_use) { texture = &vk.textures[slot]; handle = slot + 1; break; }
    }
    if (!texture) { ae3d_vk_fail("texture table full"); return 0; }

    if (!ae3d_vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               &staging, &staging_memory)) {
        return 0;
    }
    ae3d_vkMapMemory(vk.device, staging_memory, 0, size, 0, &mapped);
    memcpy(mapped, rgba, (size_t)size);
    ae3d_vkUnmapMemory(vk.device, staging_memory);

    if (!ae3d_vk_create_image(width, height, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                              VK_SAMPLE_COUNT_1_BIT,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_TILING_OPTIMAL,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              &texture->image, &texture->memory, &texture->view)) {
        ae3d_vkDestroyBuffer(vk.device, staging, NULL);
        ae3d_vkFreeMemory(vk.device, staging_memory, NULL);
        return 0;
    }

    command = ae3d_vk_begin_once();
    ae3d_vk_transition(command, texture->image, VK_IMAGE_LAYOUT_UNDEFINED,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    memset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (unsigned)width;
    region.imageExtent.height = (unsigned)height;
    region.imageExtent.depth = 1;
    ae3d_vkCmdCopyBufferToImage(command, staging, texture->image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    ae3d_vk_transition(command, texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    ae3d_vk_end_once(command);

    ae3d_vkDestroyBuffer(vk.device, staging, NULL);
    ae3d_vkFreeMemory(vk.device, staging_memory, NULL);

    memset(&sampler, 0, sizeof(sampler));
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.maxLod = 0.0f;
    if (ae3d_vkCreateSampler(vk.device, &sampler, NULL, &texture->sampler) != VK_SUCCESS) {
        ae3d_vk_fail("vkCreateSampler failed");
        return 0;
    }

    texture->width = width;
    texture->height = height;
    texture->in_use = 1;
    return handle;
}

/* The captured frame as RGBA, top row first -- the orientation a PNG and the
   capture buffer both use, so no flip like the OpenGL readback needs. The
   swapchain is usually BGRA, so the red and blue channels are swapped on the
   way out. */
void *ae3d_vk_capture_pixels(void) {
    size_t needed, i;
    int swap;
    unsigned char *src;

    if (vk.capture_slot < 0) return NULL;
    ae3d_vkWaitForFences(vk.device, 1, &vk.in_flight[vk.capture_slot], VK_TRUE, UINT64_MAX);

    needed = (size_t)vk.readback_width * (size_t)vk.readback_height * 4u;
    if (needed > g_readback_copy_size) {
        free(g_readback_copy);
        g_readback_copy = (unsigned char *)malloc(needed);
        g_readback_copy_size = g_readback_copy ? needed : 0;
    }
    if (!g_readback_copy) return NULL;
    src = vk.readback_mapped[vk.capture_slot];
    memcpy(g_readback_copy, src, needed);

    swap = (vk.color_format == VK_FORMAT_B8G8R8A8_UNORM
            || vk.color_format == VK_FORMAT_B8G8R8A8_SRGB);
    if (swap) {
        for (i = 0; i + 3 < needed; i += 4) {
            unsigned char b = g_readback_copy[i];
            g_readback_copy[i] = g_readback_copy[i + 2];
            g_readback_copy[i + 2] = b;
        }
    }
    return g_readback_copy;
}

int ae3d_vk_capture_width(void) { return vk.readback_width; }
int ae3d_vk_capture_height(void) { return vk.readback_height; }

int ae3d_vk_upload_mesh(void *mesh) {
    const float *vertices = ae3d_mesh_vertex_data(mesh);
    const unsigned *indices = ae3d_mesh_index_data(mesh);
    int vertex_count = ae3d_mesh_vertex_count(mesh);
    int index_count = ae3d_mesh_index_count(mesh);
    unsigned *sequential = NULL;
    ae3d_vk_mesh *slot = NULL;
    int i, handle = 0;

    if (!vk.device) { ae3d_vk_fail("vulkan not initialised"); return 0; }

    for (i = 0; i < vk.mesh_capacity; i++) {
        ae3d_vk_mesh *entry = &vk.meshes[i];
        if (!entry->in_use || !entry->shared || entry->refs <= 0) continue;
        /* Two meshes alike in their vertices can differ in their skins, and the
           comparison below cannot see that, so a skinned mesh is never shared
           and never shares. */
        if (entry->skinned || ae3d_mesh_is_skinned(mesh)) continue;
        if (entry->vertex_count != vertex_count) continue;
        if (entry->index_count != (unsigned)index_count) continue;
        if (memcmp(entry->vertices, vertices, (size_t)vertex_count * AE3D_VK_STRIDE) != 0) continue;
        if (indices && index_count > 0 &&
            memcmp(entry->indices, indices, (size_t)index_count * sizeof(unsigned)) != 0) {
            continue;
        }
        entry->refs++;
        return i + 1;
    }
    if (!vertices || vertex_count <= 0) {
        ae3d_vk_fail("mesh has no geometry");
        return 0;
    }

    // A mesh built for glDrawArrays carries no index buffer. Numbering its
    // vertices costs one small upload and keeps a single indexed draw path
    // instead of a second pipeline and a second command sequence.
    if (!indices || index_count <= 0) {
        sequential = (unsigned *)malloc((size_t)vertex_count * sizeof(unsigned));
        if (!sequential) { ae3d_vk_fail("out of memory"); return 0; }
        for (i = 0; i < vertex_count; i++) sequential[i] = (unsigned)i;
        indices = sequential;
        index_count = vertex_count;
    }

    for (i = 0; i < vk.mesh_capacity; i++) {
        if (!vk.meshes[i].in_use) { slot = &vk.meshes[i]; handle = i + 1; break; }
    }
    if (!slot) {
        int grown = vk.mesh_capacity ? vk.mesh_capacity * 2 : 16;
        ae3d_vk_mesh *fresh = (ae3d_vk_mesh *)realloc(vk.meshes, (size_t)grown * sizeof(ae3d_vk_mesh));
        if (!fresh) { free(sequential); ae3d_vk_fail("out of memory"); return 0; }
        memset(fresh + vk.mesh_capacity, 0, (size_t)(grown - vk.mesh_capacity) * sizeof(ae3d_vk_mesh));
        vk.meshes = fresh;
        slot = &vk.meshes[vk.mesh_capacity];
        handle = vk.mesh_capacity + 1;
        vk.mesh_capacity = grown;
    }

    if (!ae3d_vk_upload_buffer(vertices, (VkDeviceSize)vertex_count * AE3D_VK_STRIDE,
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               &slot->vertex_buffer, &slot->vertex_memory)) {
        free(sequential);
        return 0;
    }

    if (ae3d_mesh_is_skinned(mesh)) {
        if (!ae3d_vk_upload_buffer(ae3d_mesh_skin_data(mesh),
                                   (VkDeviceSize)vertex_count * AE3D_VK_SKIN_STRIDE,
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   &slot->skin_buffer, &slot->skin_memory)) {
            free(sequential);
            return 0;
        }
        slot->skinned = 1;
    }
    if (!ae3d_vk_upload_buffer(indices, (VkDeviceSize)index_count * sizeof(unsigned),
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               &slot->index_buffer, &slot->index_memory)) {
        ae3d_vkDestroyBuffer(vk.device, slot->vertex_buffer, NULL);
        ae3d_vkFreeMemory(vk.device, slot->vertex_memory, NULL);
        memset(slot, 0, sizeof(*slot));
        free(sequential);
        return 0;
    }

    slot->index_count = (unsigned)index_count;
    slot->in_use = 1;
    slot->vertex_count = vertex_count;
    slot->refs = 1;
    slot->vertices = (float *)malloc((size_t)vertex_count * AE3D_VK_STRIDE);
    slot->indices = (unsigned *)malloc((size_t)index_count * sizeof(unsigned));
    if (slot->vertices && slot->indices) {
        memcpy(slot->vertices, vertices, (size_t)vertex_count * AE3D_VK_STRIDE);
        memcpy(slot->indices, indices, (size_t)index_count * sizeof(unsigned));
        slot->shared = 1;
    } else {
        free(slot->vertices);
        free(slot->indices);
        slot->vertices = NULL;
        slot->indices = NULL;
        slot->shared = 0;
    }

    free(sequential);
    return handle;
}

// The instance stream is interleaved into the layout the pipeline declares: a
// matrix then a colour, one vertex-input binding stepping per instance.
// One matrix and one colour per instance, in the layout the vertex shader
// declares. Both the first upload and every later refresh build it the same way.
/* The stream packed into `packed`, sixteen floats of matrix and four of
   colour an instance, as the pipeline's second vertex binding reads it. */
static void ae3d_vk_pack_instances_into(float *packed, void *instances, int count) {
    const float *matrices = ae3d_inst_matrix_data(instances);
    const float *colours = ae3d_inst_color_data(instances);
    const float *phases = ae3d_inst_has_phases(instances) ? ae3d_inst_phase_data(instances) : NULL;
    int has_colours = ae3d_inst_has_colors(instances);
    int i;

    for (i = 0; i < count; i++) {
        memcpy(packed + (size_t)i * 20, matrices + (size_t)i * 16, 16 * sizeof(float));
        if (has_colours && colours) {
            memcpy(packed + (size_t)i * 20 + 16, colours + (size_t)i * 3, 3 * sizeof(float));
        } else {
            packed[i * 20 + 16] = 1.0f;
            packed[i * 20 + 17] = 1.0f;
            packed[i * 20 + 18] = 1.0f;
        }
        /* The phase rides in the float after the colour; the crowd vertex
           shader reads it, nothing else does. */
        packed[i * 20 + 19] = phases ? phases[i] : 0.0f;
    }
}

static float *ae3d_vk_pack_instances(void *instances, int count) {
    float *packed = (float *)malloc((size_t)count * 20 * sizeof(float));
    if (!packed) return NULL;
    ae3d_vk_pack_instances_into(packed, instances, count);
    return packed;
}

// An instance stream that has been moved or recoloured since it was uploaded.
// Without this the buffer is written once, when the model is registered, and a
// particle that moves or a block that changes colour never reaches the GPU.
static void ae3d_vk_free_ring(ae3d_vk_mesh *slot) {
    unsigned i;
    for (i = 0; i < AE3D_VK_FRAMES; i++) {
        if (slot->ring_mapped[i]) ae3d_vkUnmapMemory(vk.device, slot->ring_memory[i]);
        if (slot->ring[i]) ae3d_vkDestroyBuffer(vk.device, slot->ring[i], NULL);
        if (slot->ring_memory[i]) ae3d_vkFreeMemory(vk.device, slot->ring_memory[i], NULL);
        slot->ring[i] = VK_NULL_HANDLE;
        slot->ring_memory[i] = VK_NULL_HANDLE;
        slot->ring_mapped[i] = NULL;
    }
    slot->ring_size = 0;
    slot->streaming = 0;
}

/* The ring for a stream of `bytes`, made or regrown. Growing waits for the
   device once, since every slot of the old ring may be in flight; a stream
   that keeps its size never waits again. */
static int ae3d_vk_ensure_ring(ae3d_vk_mesh *slot, VkDeviceSize bytes) {
    unsigned i;
    if (slot->streaming && slot->ring_size >= bytes) return 1;

    ae3d_vkDeviceWaitIdle(vk.device);
    if (slot->streaming) {
        /* Regrowing: the bound buffer is one of the ring's, which the ring
           frees. Destroying it here as well destroyed it twice, and the
           driver fell over on the second when a stream grew. */
        slot->vertex_buffer = VK_NULL_HANDLE;
        slot->vertex_memory = VK_NULL_HANDLE;
    }
    ae3d_vk_free_ring(slot);
    /* The device-local buffer the first upload made is not read again. */
    if (slot->vertex_buffer) ae3d_vkDestroyBuffer(vk.device, slot->vertex_buffer, NULL);
    if (slot->vertex_memory) ae3d_vkFreeMemory(vk.device, slot->vertex_memory, NULL);
    slot->vertex_buffer = VK_NULL_HANDLE;
    slot->vertex_memory = VK_NULL_HANDLE;

    for (i = 0; i < AE3D_VK_FRAMES; i++) {
        if (!ae3d_vk_create_buffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                       | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   &slot->ring[i], &slot->ring_memory[i])) {
            ae3d_vk_free_ring(slot);
            return 0;
        }
        if (ae3d_vkMapMemory(vk.device, slot->ring_memory[i], 0, bytes, 0,
                             &slot->ring_mapped[i]) != VK_SUCCESS) {
            slot->ring_mapped[i] = NULL;
            ae3d_vk_free_ring(slot);
            return ae3d_vk_fail("vkMapMemory failed for an instance ring");
        }
    }
    slot->ring_size = bytes;
    slot->streaming = 1;
    return 1;
}

int ae3d_vk_update_instances(int handle, void *instances) {
    ae3d_vk_mesh *slot;
    int count = ae3d_inst_count(instances);
    VkDeviceSize bytes;

    if (!vk.ready || handle <= 0 || handle > vk.mesh_capacity || count <= 0) return 0;
    slot = &vk.meshes[handle - 1];
    if (!slot->in_use) return 0;

    if (ae3d_inst_is_points(instances)) {
        /* A point stream is already the bytes the binding reads: one copy
           into this frame's slot of the ring, no packing. */
        const float *points = ae3d_inst_point_data(instances);
        if (!points) return 0;
        bytes = (VkDeviceSize)count * 8 * sizeof(float);
        if (!ae3d_vk_ensure_ring(slot, bytes)) return 0;
        memcpy(slot->ring_mapped[vk.frame], points, (size_t)bytes);
        slot->vertex_buffer = slot->ring[vk.frame];
        slot->points = 1;
        return 1;
    }

    /* Packed straight into this frame's slot of the ring, which this
       frame's draws then bind. The first update turns the stream over from
       the device-local buffer of the upload to the ring; from then on an
       update is one pass over the instances into mapped memory, where it
       used to be a wait for the whole device, a destroy and a staged upload
       of a fresh buffer every frame. */
    bytes = (VkDeviceSize)count * 20 * sizeof(float);
    if (!ae3d_vk_ensure_ring(slot, bytes)) return 0;
    ae3d_vk_pack_instances_into((float *)slot->ring_mapped[vk.frame], instances, count);
    slot->vertex_buffer = slot->ring[vk.frame];
    return 1;
}

int ae3d_vk_upload_instances(void *instances) {
    const float *matrices = ae3d_inst_matrix_data(instances);
    int count = ae3d_inst_count(instances);
    ae3d_vk_mesh *slot = NULL;
    float *packed;
    int i, handle = 0;
    int points = ae3d_inst_is_points(instances);

    if (points) matrices = ae3d_inst_point_data(instances);
    if (!vk.device || !matrices || count <= 0) { ae3d_vk_fail("no instances"); return 0; }

    for (i = 0; i < vk.mesh_capacity; i++) {
        if (!vk.meshes[i].in_use) { slot = &vk.meshes[i]; handle = i + 1; break; }
    }
    if (!slot) {
        int grown = vk.mesh_capacity ? vk.mesh_capacity * 2 : 16;
        ae3d_vk_mesh *fresh = (ae3d_vk_mesh *)realloc(vk.meshes, (size_t)grown * sizeof(ae3d_vk_mesh));
        if (!fresh) { ae3d_vk_fail("out of memory"); return 0; }
        memset(fresh + vk.mesh_capacity, 0, (size_t)(grown - vk.mesh_capacity) * sizeof(ae3d_vk_mesh));
        vk.meshes = fresh;
        slot = &vk.meshes[vk.mesh_capacity];
        handle = vk.mesh_capacity + 1;
        vk.mesh_capacity = grown;
    }

    if (points) {
        if (!ae3d_vk_upload_buffer((void *)matrices, (VkDeviceSize)count * 8 * sizeof(float),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   &slot->vertex_buffer, &slot->vertex_memory)) {
            return 0;
        }
        slot->points = 1;
        slot->index_count = 0;
        slot->in_use = 1;
        return handle;
    }

    packed = ae3d_vk_pack_instances(instances, count);
    if (!packed) { ae3d_vk_fail("out of memory"); return 0; }

    if (!ae3d_vk_upload_buffer(packed, (VkDeviceSize)count * 20 * sizeof(float),
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               &slot->vertex_buffer, &slot->vertex_memory)) {
        free(packed);
        return 0;
    }
    free(packed);

    slot->points = 0;
    slot->index_count = 0;
    slot->in_use = 1;
    return handle;
}

/* Whether more than one model draws this mesh. The upload cache hands the
   same handle to every model built from the same bytes, and a depth pass can
   draw all of them in one instanced call if it knows. */
int ae3d_vk_mesh_shared(int handle) {
    if (handle <= 0 || handle > vk.mesh_capacity) return 0;
    if (!vk.meshes[handle - 1].in_use) return 0;
    return vk.meshes[handle - 1].shared && vk.meshes[handle - 1].refs > 1;
}

void ae3d_vk_free_mesh(int handle) {
    ae3d_vk_mesh *mesh;

    if (!vk.ready || handle <= 0 || handle > vk.mesh_capacity) return;
    mesh = &vk.meshes[handle - 1];
    if (!mesh->in_use) return;
    if (mesh->shared && --mesh->refs > 0) return;

    ae3d_vkDeviceWaitIdle(vk.device);
    free(mesh->vertices);
    free(mesh->indices);
    if (mesh->streaming) {
        /* The bound buffer is one of the ring's; the ring frees it. */
        mesh->vertex_buffer = VK_NULL_HANDLE;
        mesh->vertex_memory = VK_NULL_HANDLE;
        ae3d_vk_free_ring(mesh);
    }
    ae3d_vkDestroyBuffer(vk.device, mesh->vertex_buffer, NULL);
    ae3d_vkFreeMemory(vk.device, mesh->vertex_memory, NULL);
    ae3d_vkDestroyBuffer(vk.device, mesh->index_buffer, NULL);
    ae3d_vkFreeMemory(vk.device, mesh->index_memory, NULL);
    if (mesh->skin_buffer) {
        ae3d_vkDestroyBuffer(vk.device, mesh->skin_buffer, NULL);
        ae3d_vkFreeMemory(vk.device, mesh->skin_memory, NULL);
    }
    memset(mesh, 0, sizeof(*mesh));
}

void ae3d_vk_shutdown(void) {
    unsigned i;

    free(g_readback_copy);
    g_readback_copy = NULL;
    g_readback_copy_size = 0;

    if (!vk.ready) return;
    ae3d_vkDeviceWaitIdle(vk.device);

    for (i = 0; i < (unsigned)vk.mesh_capacity; i++) {
        if (vk.meshes[i].in_use) {
            if (vk.meshes[i].streaming) {
                vk.meshes[i].vertex_buffer = VK_NULL_HANDLE;
                vk.meshes[i].vertex_memory = VK_NULL_HANDLE;
                ae3d_vk_free_ring(&vk.meshes[i]);
            }
            ae3d_vkDestroyBuffer(vk.device, vk.meshes[i].vertex_buffer, NULL);
            ae3d_vkFreeMemory(vk.device, vk.meshes[i].vertex_memory, NULL);
            ae3d_vkDestroyBuffer(vk.device, vk.meshes[i].index_buffer, NULL);
            ae3d_vkFreeMemory(vk.device, vk.meshes[i].index_memory, NULL);
            if (vk.meshes[i].skin_buffer) {
                ae3d_vkDestroyBuffer(vk.device, vk.meshes[i].skin_buffer, NULL);
                ae3d_vkFreeMemory(vk.device, vk.meshes[i].skin_memory, NULL);
            }
            // The copy a shared mesh keeps of the bytes it was built from.
            free(vk.meshes[i].vertices);
            free(vk.meshes[i].indices);
        }
    }
    free(vk.meshes);
    vk.meshes = NULL;
    vk.mesh_capacity = 0;

    if (vk.timestamps) {
        ae3d_vkDestroyQueryPool(vk.device, vk.timestamps, NULL);
        vk.timestamps = VK_NULL_HANDLE;
    }
    for (i = 0; i < AE3D_VK_FRAMES; i++) {
        if (vk.image_available[i]) ae3d_vkDestroySemaphore(vk.device, vk.image_available[i], NULL);
        if (vk.render_finished[i]) ae3d_vkDestroySemaphore(vk.device, vk.render_finished[i], NULL);
        if (vk.in_flight[i]) ae3d_vkDestroyFence(vk.device, vk.in_flight[i], NULL);
    }
    for (i = 0; i < AE3D_VK_MAX_TEXTURES; i++) {
        ae3d_vk_texture *texture = &vk.textures[i];
        if (!texture->in_use) continue;
        if (texture->sampler) ae3d_vkDestroySampler(vk.device, texture->sampler, NULL);
        if (texture->view) ae3d_vkDestroyImageView(vk.device, texture->view, NULL);
        if (texture->image) ae3d_vkDestroyImage(vk.device, texture->image, NULL);
        if (texture->memory) ae3d_vkFreeMemory(vk.device, texture->memory, NULL);
        memset(texture, 0, sizeof(*texture));
    }

    for (i = 0; i < AE3D_VK_FRAMES; i++) {
        ae3d_vk_uniform_ring *ring = &vk.uniforms[i];
        if (ring->mapped) ae3d_vkUnmapMemory(vk.device, ring->memory);
        if (ring->buffer) ae3d_vkDestroyBuffer(vk.device, ring->buffer, NULL);
        if (ring->memory) ae3d_vkFreeMemory(vk.device, ring->memory, NULL);
        memset(ring, 0, sizeof(*ring));
    }

    if (vk.identity_instance) ae3d_vkDestroyBuffer(vk.device, vk.identity_instance, NULL);
    if (vk.identity_instance_memory) ae3d_vkFreeMemory(vk.device, vk.identity_instance_memory, NULL);
    if (vk.empty_skin) ae3d_vkDestroyBuffer(vk.device, vk.empty_skin, NULL);
    if (vk.empty_skin_memory) ae3d_vkFreeMemory(vk.device, vk.empty_skin_memory, NULL);

    if (vk.descriptor_pool) ae3d_vkDestroyDescriptorPool(vk.device, vk.descriptor_pool, NULL);
    if (vk.set_layout) ae3d_vkDestroyDescriptorSetLayout(vk.device, vk.set_layout, NULL);

    if (vk.command_pool) ae3d_vkDestroyCommandPool(vk.device, vk.command_pool, NULL);
    for (i = 0; i < 2; i++) {
        if (vk.pipeline[i]) ae3d_vkDestroyPipeline(vk.device, vk.pipeline[i], NULL);
        if (vk.water_pipeline[i]) ae3d_vkDestroyPipeline(vk.device, vk.water_pipeline[i], NULL);
        if (vk.crowd_pipeline[i]) ae3d_vkDestroyPipeline(vk.device, vk.crowd_pipeline[i], NULL);
    }
    if (vk.pipeline_blend) ae3d_vkDestroyPipeline(vk.device, vk.pipeline_blend, NULL);
    if (vk.water_pipeline_blend) ae3d_vkDestroyPipeline(vk.device, vk.water_pipeline_blend, NULL);
    if (vk.crowd_pipeline_blend) ae3d_vkDestroyPipeline(vk.device, vk.crowd_pipeline_blend, NULL);
    if (vk.crowd_shadow_pipeline) ae3d_vkDestroyPipeline(vk.device, vk.crowd_shadow_pipeline, NULL);
    if (vk.sky_pipeline) ae3d_vkDestroyPipeline(vk.device, vk.sky_pipeline, NULL);
    if (vk.ssao_pipeline) ae3d_vkDestroyPipeline(vk.device, vk.ssao_pipeline, NULL);
    if (vk.point_pipeline_blend) ae3d_vkDestroyPipeline(vk.device, vk.point_pipeline_blend, NULL);
    if (vk.point_pipeline[0]) ae3d_vkDestroyPipeline(vk.device, vk.point_pipeline[0], NULL);
    if (vk.point_pipeline[1]) ae3d_vkDestroyPipeline(vk.device, vk.point_pipeline[1], NULL);
    if (vk.point_shadow_pipeline) ae3d_vkDestroyPipeline(vk.device, vk.point_shadow_pipeline, NULL);
    for (i = 0; i < 3; i++) {
        if (vk.post_pipelines[i]) ae3d_vkDestroyPipeline(vk.device, vk.post_pipelines[i], NULL);
    }
    if (vk.pipeline_layout) ae3d_vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, NULL);

    ae3d_vk_destroy_swapchain();
    if (vk.render_pass) ae3d_vkDestroyRenderPass(vk.device, vk.render_pass, NULL);
    if (vk.scene_pass) ae3d_vkDestroyRenderPass(vk.device, vk.scene_pass, NULL);
    if (vk.post_pass) ae3d_vkDestroyRenderPass(vk.device, vk.post_pass, NULL);
    if (vk.ssr_pass) ae3d_vkDestroyRenderPass(vk.device, vk.ssr_pass, NULL);
    if (vk.shadow_pass) ae3d_vkDestroyRenderPass(vk.device, vk.shadow_pass, NULL);
    if (vk.camdepth_pass) ae3d_vkDestroyRenderPass(vk.device, vk.camdepth_pass, NULL);
    if (vk.shadow_pipeline) ae3d_vkDestroyPipeline(vk.device, vk.shadow_pipeline, NULL);
    ae3d_vk_destroy_shadow_target();
    if (vk.device) ae3d_vkDestroyDevice(vk.device, NULL);
    if (vk.surface && ae3d_vkDestroySurfaceKHR) ae3d_vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    if (vk.instance) ae3d_vkDestroyInstance(vk.instance, NULL);

    memset(&vk, 0, sizeof(vk));
}
