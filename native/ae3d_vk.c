#include "ae3d.h"
#include "ae3d_internal.h"
#include "ae3d_vk_shaders.h"

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
#define AE3D_VK_STRIDE (8 * (int)sizeof(float))

#define AE3D_VK_GLOBAL_FUNCS(X) \
    X(vkCreateInstance) \
    X(vkEnumerateInstanceExtensionProperties)

#define AE3D_VK_INSTANCE_FUNCS(X) \
    X(vkDestroyInstance) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkCreateDevice) \
    X(vkGetDeviceProcAddr) \
    X(vkDestroySurfaceKHR)

#define AE3D_VK_DEVICE_FUNCS(X) \
    X(vkGetDeviceQueue) \
    X(vkDestroyDevice) \
    X(vkDeviceWaitIdle) \
    X(vkQueueWaitIdle) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroySwapchainKHR) \
    X(vkGetSwapchainImagesKHR) \
    X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR) \
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
    X(vkCreateSemaphore) \
    X(vkDestroySemaphore) \
    X(vkCreateFence) \
    X(vkDestroyFence) \
    X(vkWaitForFences) \
    X(vkResetFences) \
    X(vkQueueSubmit)

#define AE3D_VK_DECLARE(name) static PFN_##name ae3d_##name;
AE3D_VK_GLOBAL_FUNCS(AE3D_VK_DECLARE)
AE3D_VK_INSTANCE_FUNCS(AE3D_VK_DECLARE)
AE3D_VK_DEVICE_FUNCS(AE3D_VK_DECLARE)
#undef AE3D_VK_DECLARE

typedef struct {
    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    unsigned index_count;
    int in_use;
} ae3d_vk_mesh;

typedef struct {
    float mvp[16];
    float base_color[4];
    float light[4];
} ae3d_vk_push;

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

    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkCommandPool command_pool;

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

static const char *const ae3d_vk_icd_paths[] = {
#if defined(__APPLE__)
    "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json",
    "/usr/local/etc/vulkan/icd.d/MoltenVK_icd.json",
    "/opt/homebrew/share/vulkan/icd.d/MoltenVK_icd.json",
#endif
    NULL
};

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
    return 1;
}

static int ae3d_vk_load_device(void) {
#define AE3D_VK_LOAD_DEVICE(name) \
    ae3d_##name = (PFN_##name)ae3d_vkGetDeviceProcAddr(vk.device, #name); \
    if (!ae3d_##name) return ae3d_vk_fail("missing " #name);
    AE3D_VK_DEVICE_FUNCS(AE3D_VK_LOAD_DEVICE)
#undef AE3D_VK_LOAD_DEVICE
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

    extensions[extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
    extensions[extension_count++] = AE3D_VK_PLATFORM_SURFACE_EXTENSION;

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
    app.pApplicationName = "aether3d";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "aether3d";
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
        int graphics = -1, present = -1;

        ae3d_vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, NULL);
        if (family_count == 0) continue;
        families = (VkQueueFamilyProperties *)calloc(family_count, sizeof(VkQueueFamilyProperties));
        if (!families) continue;
        ae3d_vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, families);

        for (q = 0; q < family_count; q++) {
            VkBool32 supported = VK_FALSE;
            if (graphics < 0 && (families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) graphics = (int)q;
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

    extensions[extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

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
    free(vk.images);
    vk.images = NULL;

    if (vk.depth_view) { ae3d_vkDestroyImageView(vk.device, vk.depth_view, NULL); vk.depth_view = VK_NULL_HANDLE; }
    if (vk.depth_image) { ae3d_vkDestroyImage(vk.device, vk.depth_image, NULL); vk.depth_image = VK_NULL_HANDLE; }
    if (vk.depth_memory) { ae3d_vkFreeMemory(vk.device, vk.depth_memory, NULL); vk.depth_memory = VK_NULL_HANDLE; }

    if (vk.swapchain) { ae3d_vkDestroySwapchainKHR(vk.device, vk.swapchain, NULL); vk.swapchain = VK_NULL_HANDLE; }
    vk.image_count = 0;
}

static int ae3d_vk_choose_surface_format(void) {
    VkSurfaceFormatKHR *formats;
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
    image.samples = VK_SAMPLE_COUNT_1_BIT;
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

    return ae3d_vk_create_depth();
}

static int ae3d_vk_create_render_pass(void) {
    VkAttachmentDescription attachments[2];
    VkAttachmentReference color_ref, depth_ref;
    VkSubpassDescription subpass;
    VkSubpassDependency dependency;
    VkRenderPassCreateInfo info;
    VkResult result;

    memset(attachments, 0, sizeof(attachments));
    attachments[0].format = vk.color_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = vk.depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&color_ref, 0, sizeof(color_ref));
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    memset(&depth_ref, 0, sizeof(depth_ref));
    depth_ref.attachment = 1;
    depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    memset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

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
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 1;
    info.pDependencies = &dependency;

    result = ae3d_vkCreateRenderPass(vk.device, &info, NULL, &vk.render_pass);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateRenderPass failed", result);
    return 1;
}

static int ae3d_vk_create_framebuffers(void) {
    unsigned i;

    vk.framebuffers = (VkFramebuffer *)calloc(vk.image_count, sizeof(VkFramebuffer));
    if (!vk.framebuffers) return ae3d_vk_fail("out of memory");

    for (i = 0; i < vk.image_count; i++) {
        VkImageView attachments[2];
        VkFramebufferCreateInfo info;
        VkResult result;

        attachments[0] = vk.image_views[i];
        attachments[1] = vk.depth_view;

        memset(&info, 0, sizeof(info));
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = vk.render_pass;
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.width = vk.extent.width;
        info.height = vk.extent.height;
        info.layers = 1;

        result = ae3d_vkCreateFramebuffer(vk.device, &info, NULL, &vk.framebuffers[i]);
        if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateFramebuffer failed", result);
    }
    return 1;
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

static int ae3d_vk_create_pipeline(void) {
    VkShaderModule vertex_module, fragment_module;
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription binding;
    VkVertexInputAttributeDescription attributes[3];
    VkPipelineVertexInputStateCreateInfo vertex_input;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo viewport_state;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineDepthStencilStateCreateInfo depth;
    VkPipelineColorBlendAttachmentState blend_attachment;
    VkPipelineColorBlendStateCreateInfo blend;
    VkPipelineDynamicStateCreateInfo dynamic;
    VkDynamicState dynamic_states[2];
    VkPushConstantRange push;
    VkPipelineLayoutCreateInfo layout;
    VkGraphicsPipelineCreateInfo info;
    VkResult result;

    vertex_module = ae3d_vk_shader(ae3d_vk_scene_vert_spv, (unsigned)sizeof(ae3d_vk_scene_vert_spv));
    fragment_module = ae3d_vk_shader(ae3d_vk_scene_frag_spv, (unsigned)sizeof(ae3d_vk_scene_frag_spv));
    if (!vertex_module || !fragment_module) return ae3d_vk_fail("vkCreateShaderModule failed");

    memset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_module;
    stages[1].pName = "main";

    memset(&binding, 0, sizeof(binding));
    binding.binding = 0;
    binding.stride = AE3D_VK_STRIDE;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    memset(attributes, 0, sizeof(attributes));
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = 3 * sizeof(float);
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = 5 * sizeof(float);

    memset(&vertex_input, 0, sizeof(vertex_input));
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 3;
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
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    memset(&multisample, 0, sizeof(multisample));
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    memset(&depth, 0, sizeof(depth));
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depth.maxDepthBounds = 1.0f;

    memset(&blend_attachment, 0, sizeof(blend_attachment));
    blend_attachment.blendEnable = VK_TRUE;
    blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    memset(&blend, 0, sizeof(blend));
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attachment;

    dynamic_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
    dynamic_states[1] = VK_DYNAMIC_STATE_SCISSOR;
    memset(&dynamic, 0, sizeof(dynamic));
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;

    memset(&push, 0, sizeof(push));
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(ae3d_vk_push);

    memset(&layout, 0, sizeof(layout));
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;

    result = ae3d_vkCreatePipelineLayout(vk.device, &layout, NULL, &vk.pipeline_layout);
    if (result != VK_SUCCESS) {
        ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);
        ae3d_vkDestroyShaderModule(vk.device, fragment_module, NULL);
        return ae3d_vk_fail_code("vkCreatePipelineLayout failed", result);
    }

    memset(&info, 0, sizeof(info));
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport_state;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = vk.pipeline_layout;
    info.renderPass = vk.render_pass;
    info.subpass = 0;

    result = ae3d_vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &info, NULL, &vk.pipeline);
    ae3d_vkDestroyShaderModule(vk.device, vertex_module, NULL);
    ae3d_vkDestroyShaderModule(vk.device, fragment_module, NULL);
    if (result != VK_SUCCESS) return ae3d_vk_fail_code("vkCreateGraphicsPipelines failed", result);
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
    }
    return 1;
}

int ae3d_vk_init(void *win, int width, int height) {
    if (vk.ready) return 1;
    if (!ae3d_vk_available()) return 0;
    if (!win) return ae3d_vk_fail("no window");

    memset(&vk, 0, sizeof(vk));

    if (!ae3d_vk_create_instance()) return 0;

    if (!ae3d_vk_create_surface(win)) return 0;

    if (!ae3d_vk_pick_device()) return 0;
    if (!ae3d_vk_create_device()) return 0;
    if (!ae3d_vk_choose_surface_format()) return 0;
    if (!ae3d_vk_create_swapchain(width, height)) return 0;
    if (!ae3d_vk_create_render_pass()) return 0;
    if (!ae3d_vk_create_framebuffers()) return 0;
    if (!ae3d_vk_create_pipeline()) return 0;
    if (!ae3d_vk_create_commands()) return 0;

    snprintf(g_vk_error, sizeof(g_vk_error), "%s", "");
    vk.ready = 1;
    return 1;
}

static int ae3d_vk_rebuild_swapchain(int width, int height) {
    ae3d_vkDeviceWaitIdle(vk.device);
    ae3d_vk_destroy_swapchain();
    if (!ae3d_vk_create_swapchain(width, height)) return 0;
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

int ae3d_vk_frame_begin(double r, double g, double b, double a) {
    VkCommandBufferBeginInfo begin;
    VkRenderPassBeginInfo pass;
    VkClearValue clears[2];
    VkViewport viewport;
    VkRect2D scissor;
    VkResult result;

    if (!vk.ready || vk.recording) return 0;

    if (vk.needs_resize) {
        if (!ae3d_vk_rebuild_swapchain(vk.pending_width, vk.pending_height)) return 0;
    }

    ae3d_vkWaitForFences(vk.device, 1, &vk.in_flight[vk.frame], VK_TRUE, UINT64_MAX);

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

    ae3d_vkResetFences(vk.device, 1, &vk.in_flight[vk.frame]);
    ae3d_vkResetCommandBuffer(vk.command_buffers[vk.frame], 0);

    memset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    ae3d_vkBeginCommandBuffer(vk.command_buffers[vk.frame], &begin);

    memset(clears, 0, sizeof(clears));
    clears[0].color.float32[0] = (float)r;
    clears[0].color.float32[1] = (float)g;
    clears[0].color.float32[2] = (float)b;
    clears[0].color.float32[3] = (float)a;
    clears[1].depthStencil.depth = 1.0f;

    memset(&pass, 0, sizeof(pass));
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = vk.render_pass;
    pass.framebuffer = vk.framebuffers[vk.image_index];
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

    ae3d_vkCmdBindPipeline(vk.command_buffers[vk.frame], VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline);

    vk.recording = 1;
    return 1;
}

// Vulkan clip space puts y downward and z in [0, 1]; the engine's matrices are
// OpenGL-shaped, so the correction is folded into the MVP here rather than
// forcing every caller to keep two projection matrices.
static void ae3d_vk_clip_correct(const double *source, float *out) {
    int column;
    for (column = 0; column < 4; column++) {
        out[column * 4 + 0] = (float)source[column * 4 + 0];
        out[column * 4 + 1] = (float)(-source[column * 4 + 1]);
        out[column * 4 + 2] = (float)(0.5 * (source[column * 4 + 2] + source[column * 4 + 3]));
        out[column * 4 + 3] = (float)source[column * 4 + 3];
    }
}

void ae3d_vk_draw_mesh(int handle, const double *mvp, double cr, double cg, double cb) {
    ae3d_vk_push push;
    VkDeviceSize offset = 0;
    ae3d_vk_mesh *mesh;

    if (!vk.recording || handle <= 0 || handle > vk.mesh_capacity) return;
    mesh = &vk.meshes[handle - 1];
    if (!mesh->in_use || mesh->index_count == 0) return;

    memset(&push, 0, sizeof(push));
    ae3d_vk_clip_correct(mvp, push.mvp);
    push.base_color[0] = (float)cr;
    push.base_color[1] = (float)cg;
    push.base_color[2] = (float)cb;
    push.base_color[3] = 1.0f;
    push.light[0] = 0.4f;
    push.light[1] = 0.8f;
    push.light[2] = 0.5f;
    push.light[3] = 0.2f;

    ae3d_vkCmdPushConstants(vk.command_buffers[vk.frame], vk.pipeline_layout,
                            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(push), &push);
    ae3d_vkCmdBindVertexBuffers(vk.command_buffers[vk.frame], 0, 1, &mesh->vertex_buffer, &offset);
    ae3d_vkCmdBindIndexBuffer(vk.command_buffers[vk.frame], mesh->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    ae3d_vkCmdDrawIndexed(vk.command_buffers[vk.frame], mesh->index_count, 1, 0, 0, 0);
}

int ae3d_vk_frame_end(void) {
    VkSubmitInfo submit;
    VkPresentInfoKHR present;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkResult result;

    if (!vk.recording) return 0;

    ae3d_vkCmdEndRenderPass(vk.command_buffers[vk.frame]);
    ae3d_vkEndCommandBuffer(vk.command_buffers[vk.frame]);

    memset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &vk.image_available[vk.frame];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &vk.command_buffers[vk.frame];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &vk.render_finished[vk.frame];

    result = ae3d_vkQueueSubmit(vk.graphics_queue, 1, &submit, vk.in_flight[vk.frame]);
    if (result != VK_SUCCESS) {
        vk.recording = 0;
        return ae3d_vk_fail_code("vkQueueSubmit failed", result);
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

int ae3d_vk_upload_mesh(void *mesh) {
    const float *vertices = ae3d_mesh_vertex_data(mesh);
    const unsigned *indices = ae3d_mesh_index_data(mesh);
    int vertex_count = ae3d_mesh_vertex_count(mesh);
    int index_count = ae3d_mesh_index_count(mesh);
    ae3d_vk_mesh *slot = NULL;
    int i, handle = 0;

    if (!vk.ready) { ae3d_vk_fail("vulkan not initialised"); return 0; }
    if (!vertices || !indices || vertex_count <= 0 || index_count <= 0) {
        ae3d_vk_fail("mesh has no geometry");
        return 0;
    }

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

    if (!ae3d_vk_upload_buffer(vertices, (VkDeviceSize)vertex_count * AE3D_VK_STRIDE,
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                               &slot->vertex_buffer, &slot->vertex_memory)) {
        return 0;
    }
    if (!ae3d_vk_upload_buffer(indices, (VkDeviceSize)index_count * sizeof(unsigned),
                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               &slot->index_buffer, &slot->index_memory)) {
        ae3d_vkDestroyBuffer(vk.device, slot->vertex_buffer, NULL);
        ae3d_vkFreeMemory(vk.device, slot->vertex_memory, NULL);
        memset(slot, 0, sizeof(*slot));
        return 0;
    }

    slot->index_count = (unsigned)index_count;
    slot->in_use = 1;
    return handle;
}

void ae3d_vk_free_mesh(int handle) {
    ae3d_vk_mesh *mesh;

    if (!vk.ready || handle <= 0 || handle > vk.mesh_capacity) return;
    mesh = &vk.meshes[handle - 1];
    if (!mesh->in_use) return;

    ae3d_vkDeviceWaitIdle(vk.device);
    ae3d_vkDestroyBuffer(vk.device, mesh->vertex_buffer, NULL);
    ae3d_vkFreeMemory(vk.device, mesh->vertex_memory, NULL);
    ae3d_vkDestroyBuffer(vk.device, mesh->index_buffer, NULL);
    ae3d_vkFreeMemory(vk.device, mesh->index_memory, NULL);
    memset(mesh, 0, sizeof(*mesh));
}

void ae3d_vk_shutdown(void) {
    unsigned i;

    if (!vk.ready) return;
    ae3d_vkDeviceWaitIdle(vk.device);

    for (i = 0; i < (unsigned)vk.mesh_capacity; i++) {
        if (vk.meshes[i].in_use) {
            ae3d_vkDestroyBuffer(vk.device, vk.meshes[i].vertex_buffer, NULL);
            ae3d_vkFreeMemory(vk.device, vk.meshes[i].vertex_memory, NULL);
            ae3d_vkDestroyBuffer(vk.device, vk.meshes[i].index_buffer, NULL);
            ae3d_vkFreeMemory(vk.device, vk.meshes[i].index_memory, NULL);
        }
    }
    free(vk.meshes);
    vk.meshes = NULL;
    vk.mesh_capacity = 0;

    for (i = 0; i < AE3D_VK_FRAMES; i++) {
        if (vk.image_available[i]) ae3d_vkDestroySemaphore(vk.device, vk.image_available[i], NULL);
        if (vk.render_finished[i]) ae3d_vkDestroySemaphore(vk.device, vk.render_finished[i], NULL);
        if (vk.in_flight[i]) ae3d_vkDestroyFence(vk.device, vk.in_flight[i], NULL);
    }
    if (vk.command_pool) ae3d_vkDestroyCommandPool(vk.device, vk.command_pool, NULL);
    if (vk.pipeline) ae3d_vkDestroyPipeline(vk.device, vk.pipeline, NULL);
    if (vk.pipeline_layout) ae3d_vkDestroyPipelineLayout(vk.device, vk.pipeline_layout, NULL);

    ae3d_vk_destroy_swapchain();
    if (vk.render_pass) ae3d_vkDestroyRenderPass(vk.device, vk.render_pass, NULL);
    if (vk.device) ae3d_vkDestroyDevice(vk.device, NULL);
    if (vk.surface) ae3d_vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    if (vk.instance) ae3d_vkDestroyInstance(vk.instance, NULL);

    memset(&vk, 0, sizeof(vk));
}
