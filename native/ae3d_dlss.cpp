/* DLSS through NVIDIA Streamline, on Vulkan. The C surface is in
   native/ae3d_dlss.h; this is the half that speaks the SDK's C++. Built
   only when AE3D_STREAMLINE_ROOT names the SDK (build.sh); the stub in
   native/ae3d_dlss_stub.c stands in otherwise.

   The runtime is loaded by hand -- sl.interposer.dll, opened from the
   directory asked for, AE3D_STREAMLINE, or beside the program -- and every
   Streamline call goes through the function pointers it exports, so this
   library links nothing of NVIDIA's: a machine without the runtime or the
   card is told so and draws without DLSS. No standard library: plain C
   idioms in a .cpp, so the engine's shared library links as before. */
#include "ae3d_dlss.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <vulkan/vulkan.h>

#include "sl.h"
#include "sl_consts.h"
#include "sl_dlss.h"

static char g_error[512];
static void *g_module;
static void *g_gipa;
static int g_ready;
static sl::FrameToken *g_frame;
static sl::ViewportHandle g_viewport{0u};

/* The exports, by name, from the module. */
static PFun_slInit *p_slInit;
static PFun_slShutdown *p_slShutdown;
static PFun_slIsFeatureSupported *p_slIsFeatureSupported;
static PFun_slSetTagForFrame *p_slSetTagForFrame;
static PFun_slSetConstants *p_slSetConstants;
static PFun_slGetFeatureFunction *p_slGetFeatureFunction;
static PFun_slGetNewFrameToken *p_slGetNewFrameToken;
static PFun_slEvaluateFeature *p_slEvaluateFeature;
static PFun_slDLSSGetOptimalSettings *p_slDLSSGetOptimalSettings;
static PFun_slDLSSSetOptions *p_slDLSSSetOptions;

static void fail(const char *what, sl::Result result) {
    snprintf(g_error, sizeof(g_error), "%s (sl::Result %u)", what, (unsigned)result);
}

static void log_message(sl::LogType type, const char *msg) {
    if (type == sl::LogType::eError) {
        snprintf(g_error, sizeof(g_error), "%s", msg ? msg : "");
        fprintf(stderr, "ae3d dlss: %s", msg ? msg : "\n");
    } else if (type == sl::LogType::eWarn || getenv("AE3D_DLSS_LOG")) {
        fprintf(stderr, "ae3d dlss: %s", msg ? msg : "\n");
    }
}

static void *load_symbol(const char *name) {
#if defined(_WIN32)
    return (void *)GetProcAddress((HMODULE)g_module, name);
#else
    (void)name;
    return NULL;
#endif
}

extern "C" int ae3d_dlss_built(void) { return 1; }

extern "C" const char *ae3d_dlss_last_error(void) { return g_error; }

extern "C" void *ae3d_dlss_instance_proc_addr(void) { return g_ready ? g_gipa : NULL; }

extern "C" int ae3d_dlss_load(const char *directory) {
#if !defined(_WIN32)
    (void)directory;
    snprintf(g_error, sizeof(g_error), "Streamline runs on Windows");
    return 0;
#else
    char path[1024];
    wchar_t wdir[1024];
    const wchar_t *plugin_paths[1];
    sl::Preferences pref{};
    sl::Feature features[1] = { sl::kFeatureDLSS };
    sl::Result result;

    if (g_ready) return 1;
    if (!directory || !*directory) directory = getenv("AE3D_STREAMLINE");
    if (!directory || !*directory) {
        /* Beside the program. */
        DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
        char *slash;
        if (n == 0 || n >= sizeof(path)) { snprintf(g_error, sizeof(g_error), "no program path"); return 0; }
        slash = strrchr(path, '\\');
        if (slash) *slash = 0;
        directory = path;
    } else {
        snprintf(path, sizeof(path), "%s", directory);
        directory = path;
    }
    {
        char module_path[1200];
        snprintf(module_path, sizeof(module_path), "%s\\sl.interposer.dll", directory);
        g_module = (void *)LoadLibraryA(module_path);
        if (!g_module) {
            snprintf(g_error, sizeof(g_error), "sl.interposer.dll not found in %.400s", directory);
            return 0;
        }
    }
    p_slInit = (PFun_slInit *)load_symbol("slInit");
    p_slShutdown = (PFun_slShutdown *)load_symbol("slShutdown");
    p_slIsFeatureSupported = (PFun_slIsFeatureSupported *)load_symbol("slIsFeatureSupported");
    p_slSetTagForFrame = (PFun_slSetTagForFrame *)load_symbol("slSetTagForFrame");
    p_slSetConstants = (PFun_slSetConstants *)load_symbol("slSetConstants");
    p_slGetFeatureFunction = (PFun_slGetFeatureFunction *)load_symbol("slGetFeatureFunction");
    p_slGetNewFrameToken = (PFun_slGetNewFrameToken *)load_symbol("slGetNewFrameToken");
    p_slEvaluateFeature = (PFun_slEvaluateFeature *)load_symbol("slEvaluateFeature");
    g_gipa = load_symbol("vkGetInstanceProcAddr");
    if (!p_slInit || !p_slShutdown || !p_slIsFeatureSupported || !p_slSetTagForFrame || !p_slSetConstants ||
        !p_slGetFeatureFunction || !p_slGetNewFrameToken || !p_slEvaluateFeature || !g_gipa) {
        snprintf(g_error, sizeof(g_error), "sl.interposer.dll is missing an export");
        return 0;
    }

    MultiByteToWideChar(CP_UTF8, 0, directory, -1, wdir, 1024);
    plugin_paths[0] = wdir;
    pref.showConsole = false;
    pref.logLevel = getenv("AE3D_DLSS_LOG") ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
    pref.pathsToPlugins = plugin_paths;
    pref.numPathsToPlugins = 1;
    pref.logMessageCallback = log_message;
    /* Frame-based tagging, no state tracking of the command buffer (the
       renderer rebinds what it needs after an evaluation), nothing
       downloaded or loaded that did not come with the runtime. */
    pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    pref.featuresToLoad = features;
    pref.numFeaturesToLoad = 1;
    pref.applicationId = 231313132;   /* the SDK sample's: NGX wants one; a shipped game gets its own from NVIDIA */
    pref.engine = sl::EngineType::eCustom;
    pref.renderAPI = sl::RenderAPI::eVulkan;
    /* NGX writes its logs and data beside the plugins. */
    pref.pathToLogsAndData = wdir;
    result = p_slInit(pref, sl::kSDKVersion);
    if (result != sl::Result::eOk) { fail("slInit failed", result); return 0; }

    g_ready = 1;
    return 1;
#endif
}

/* The DLSS plugin's own functions, which it hands out only once the device
   exists: fetched on first use, after the device. */
static int dlss_functions(void) {
    sl::Result result;
    if (p_slDLSSGetOptimalSettings && p_slDLSSSetOptions) return 1;
    result = p_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", (void *&)p_slDLSSGetOptimalSettings);
    if (result != sl::Result::eOk) { fail("slDLSSGetOptimalSettings missing", result); return 0; }
    result = p_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions", (void *&)p_slDLSSSetOptions);
    if (result != sl::Result::eOk) { fail("slDLSSSetOptions missing", result); return 0; }
    return 1;
}

extern "C" int ae3d_dlss_supported(void *vk_physical_device) {
    sl::AdapterInfo adapter{};
    sl::Result result;
    if (!g_ready) return 0;
    adapter.vkPhysicalDevice = vk_physical_device;
    result = p_slIsFeatureSupported(sl::kFeatureDLSS, adapter);
    if (result != sl::Result::eOk) { fail("DLSS is not supported on this device", result); return 0; }
    return 1;
}

static sl::DLSSMode mode_of(int mode) {
    switch (mode) {
    case AE3D_DLSS_PERFORMANCE: return sl::DLSSMode::eMaxPerformance;
    case AE3D_DLSS_BALANCED: return sl::DLSSMode::eBalanced;
    case AE3D_DLSS_QUALITY: return sl::DLSSMode::eMaxQuality;
    case AE3D_DLSS_ULTRA_PERFORMANCE: return sl::DLSSMode::eUltraPerformance;
    case AE3D_DLSS_ULTRA_QUALITY: return sl::DLSSMode::eUltraQuality;
    case AE3D_DLSS_DLAA: return sl::DLSSMode::eDLAA;
    default: return sl::DLSSMode::eOff;
    }
}

static void fill_options(sl::DLSSOptions &options, int mode, unsigned out_w, unsigned out_h) {
    options.mode = mode_of(mode);
    options.outputWidth = out_w;
    options.outputHeight = out_h;
    /* The scene's colour is the tone-mapped, gamma-encoded picture. */
    options.colorBuffersHDR = sl::Boolean::eFalse;
    options.useAutoExposure = sl::Boolean::eTrue;
}

extern "C" int ae3d_dlss_optimal(int mode, unsigned out_w, unsigned out_h, unsigned *render_w, unsigned *render_h) {
    sl::DLSSOptions options{};
    sl::DLSSOptimalSettings settings{};
    sl::Result result;
    if (!g_ready || !dlss_functions()) return 0;
    fill_options(options, mode, out_w, out_h);
    result = p_slDLSSGetOptimalSettings(options, settings);
    if (result != sl::Result::eOk) { fail("slDLSSGetOptimalSettings failed", result); return 0; }
    if (render_w) *render_w = settings.optimalRenderWidth;
    if (render_h) *render_h = settings.optimalRenderHeight;
    if (settings.optimalRenderWidth == 0 || settings.optimalRenderHeight == 0) {
        snprintf(g_error, sizeof(g_error), "DLSS gave no render size for mode %d at %ux%u (min %ux%u, max %ux%u)",
                 mode, out_w, out_h, settings.renderWidthMin, settings.renderHeightMin,
                 settings.renderWidthMax, settings.renderHeightMax);
        return 0;
    }
    return 1;
}

extern "C" int ae3d_dlss_set_options(int mode, unsigned out_w, unsigned out_h) {
    sl::DLSSOptions options{};
    sl::Result result;
    if (!g_ready || !dlss_functions()) return 0;
    fill_options(options, mode, out_w, out_h);
    result = p_slDLSSSetOptions(g_viewport, options);
    if (result != sl::Result::eOk) { fail("slDLSSSetOptions failed", result); return 0; }
    return 1;
}

extern "C" int ae3d_dlss_begin_frame(unsigned frame_index) {
    sl::Result result;
    if (!g_ready) return 0;
    result = p_slGetNewFrameToken(g_frame, &frame_index);
    if (result != sl::Result::eOk) { fail("slGetNewFrameToken failed", result); return 0; }
    return 1;
}

static void matrix(sl::float4x4 &out, const float *m) {
    /* The scene's matrices are column-major and act on column vectors;
       Streamline's are row-major and act on row vectors. The one is the
       transpose of the other, and a column-major transpose is the same
       sixteen floats in the same order. */
    for (int r = 0; r < 4; r++) {
        out.row[r].x = m[r * 4 + 0];
        out.row[r].y = m[r * 4 + 1];
        out.row[r].z = m[r * 4 + 2];
        out.row[r].w = m[r * 4 + 3];
    }
}

extern "C" int ae3d_dlss_set_constants(const ae3d_dlss_camera *c) {
    sl::Constants k{};
    sl::Result result;
    if (!g_ready || !g_frame || !c) return 0;
    matrix(k.cameraViewToClip, c->view_to_clip);
    matrix(k.clipToCameraView, c->clip_to_view);
    matrix(k.clipToPrevClip, c->clip_to_prev_clip);
    matrix(k.prevClipToClip, c->prev_clip_to_clip);
    k.jitterOffset = { c->jitter_x, c->jitter_y };
    k.mvecScale = { c->mvec_scale_x, c->mvec_scale_y };
    k.cameraPinholeOffset = { 0.0f, 0.0f };
    k.cameraPos = { c->position[0], c->position[1], c->position[2] };
    k.cameraUp = { c->up[0], c->up[1], c->up[2] };
    k.cameraRight = { c->right[0], c->right[1], c->right[2] };
    k.cameraFwd = { c->forward[0], c->forward[1], c->forward[2] };
    k.cameraNear = c->near_plane;
    k.cameraFar = c->far_plane;
    k.cameraFOV = c->fov;
    k.cameraAspectRatio = c->aspect;
    k.depthInverted = c->depth_inverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    k.cameraMotionIncluded = sl::Boolean::eTrue;
    k.motionVectors3D = sl::Boolean::eFalse;
    k.reset = c->reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    k.orthographicProjection = sl::Boolean::eFalse;
    k.motionVectorsDilated = sl::Boolean::eFalse;
    k.motionVectorsJittered = sl::Boolean::eFalse;
    result = p_slSetConstants(k, *g_frame, g_viewport);
    if (result != sl::Result::eOk) { fail("slSetConstants failed", result); return 0; }
    return 1;
}

static void resource(sl::Resource &r, const ae3d_dlss_image *img) {
    r = sl::Resource(sl::ResourceType::eTex2d, img->image, img->memory, img->view, img->layout);
    r.width = img->width;
    r.height = img->height;
    r.nativeFormat = img->format;
    r.mipLevels = 1;
    r.arrayLayers = 1;
    r.usage = img->usage;
}

extern "C" int ae3d_dlss_tag(const ae3d_dlss_image *depth, const ae3d_dlss_image *motion,
                             const ae3d_dlss_image *colour_in, const ae3d_dlss_image *colour_out,
                             void *command_buffer) {
    sl::Resource res[4];
    sl::Extent render_extent{};
    sl::Extent out_extent{};
    sl::Result result;
    if (!g_ready || !g_frame) return 0;
    resource(res[0], depth);
    resource(res[1], motion);
    resource(res[2], colour_in);
    resource(res[3], colour_out);
    render_extent.width = colour_in->width;
    render_extent.height = colour_in->height;
    out_extent.width = colour_out->width;
    out_extent.height = colour_out->height;
    sl::ResourceTag tags[4] = {
        sl::ResourceTag(&res[0], sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &render_extent),
        sl::ResourceTag(&res[1], sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &render_extent),
        sl::ResourceTag(&res[2], sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eOnlyValidNow, &render_extent),
        sl::ResourceTag(&res[3], sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &out_extent),
    };
    result = p_slSetTagForFrame(*g_frame, g_viewport, tags, 4, (sl::CommandBuffer *)command_buffer);
    if (result != sl::Result::eOk) { fail("slSetTagForFrame failed", result); return 0; }
    return 1;
}

extern "C" int ae3d_dlss_evaluate(void *command_buffer) {
    const sl::BaseStructure *inputs[1] = { &g_viewport };
    sl::Result result;
    if (!g_ready || !g_frame) return 0;
    result = p_slEvaluateFeature(sl::kFeatureDLSS, *g_frame, inputs, 1, (sl::CommandBuffer *)command_buffer);
    if (result != sl::Result::eOk) { fail("slEvaluateFeature failed", result); return 0; }
    return 1;
}

extern "C" void ae3d_dlss_shutdown(void) {
    if (!g_ready) return;
    p_slShutdown();
    g_ready = 0;
    g_frame = NULL;
}
