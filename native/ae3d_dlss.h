/* DLSS through NVIDIA Streamline, on Vulkan: the interposer loaded in place
   of the Vulkan loader before the instance exists, DLSS asked for, each
   frame the scene's colour, depth and motion vectors at the render size
   tagged and evaluated into the frame-size output. See docs/rendering.md,
   "DLSS".

   Built two ways: native/ae3d_dlss.cpp against the Streamline SDK's C++
   headers when AE3D_STREAMLINE_ROOT names the SDK at build time, otherwise
   native/ae3d_dlss_stub.c, where every call says the feature was not built
   in. The Vulkan backend only ever calls this C surface, so it builds the
   same either way. The Streamline runtime (sl.interposer.dll, sl.common.dll,
   sl.dlss.dll, nvngx_dlss.dll) is loaded at run time from AE3D_STREAMLINE or
   beside the program; it is NVIDIA's and not shipped here. */
#ifndef AE3D_DLSS_H
#define AE3D_DLSS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The camera a frame was drawn with, what DLSS reprojects by. Matrices are
   column-major in the Vulkan clip convention (y down, depth 0..1), as the
   scene's own block carries them, unjittered; the jitter is given apart, in
   pixels of the render size. The motion-vector scale turns the vectors'
   texture-space units into what DLSS reads. */
typedef struct {
    float view_to_clip[16];       /* the projection */
    float clip_to_view[16];       /* its inverse */
    float clip_to_prev_clip[16];  /* this frame's clip to last frame's */
    float prev_clip_to_clip[16];  /* and back */
    float jitter_x, jitter_y;
    float mvec_scale_x, mvec_scale_y;
    float position[3], up[3], right[3], forward[3];
    float near_plane, far_plane, fov, aspect;
    int depth_inverted;
    int reset;                    /* no history: a resize, a cut */
} ae3d_dlss_camera;

/* A Vulkan image DLSS is handed: its handle, memory and view, the layout it
   is in when DLSS runs (a VkImageLayout), size, VkFormat and usage flags. */
typedef struct {
    void *image;
    void *memory;
    void *view;
    unsigned layout;
    unsigned width, height;
    unsigned format;
    unsigned usage;
} ae3d_dlss_image;

/* What each tagged image is. */
enum {
    AE3D_DLSS_DEPTH = 0,
    AE3D_DLSS_MOTION = 1,
    AE3D_DLSS_COLOUR_IN = 2,
    AE3D_DLSS_COLOUR_OUT = 3
};

/* The DLSS modes, sl::DLSSMode's order. */
enum {
    AE3D_DLSS_OFF = 0,
    AE3D_DLSS_PERFORMANCE = 1,
    AE3D_DLSS_BALANCED = 2,
    AE3D_DLSS_QUALITY = 3,
    AE3D_DLSS_ULTRA_PERFORMANCE = 4,
    AE3D_DLSS_ULTRA_QUALITY = 5,
    AE3D_DLSS_DLAA = 6
};

/* 1 when this library was built against the Streamline SDK. */
int ae3d_dlss_built(void);

/* Loads the Streamline runtime from `directory` (NULL: AE3D_STREAMLINE, or
   beside the program) and initialises it for Vulkan with DLSS. Before any
   Vulkan call. 1 on success; ae3d_dlss_last_error says otherwise. */
int ae3d_dlss_load(const char *directory);

/* The interposer's vkGetInstanceProcAddr, to load Vulkan through, so its
   vkCreateInstance and vkCreateDevice add what DLSS needs; NULL when not
   loaded. */
void *ae3d_dlss_instance_proc_addr(void);

/* Whether DLSS runs on this VkPhysicalDevice: after the instance exists. */
int ae3d_dlss_supported(void *vk_physical_device);

/* The render size DLSS wants for `mode` at an output size. */
int ae3d_dlss_optimal(int mode, unsigned out_w, unsigned out_h, unsigned *render_w, unsigned *render_h);

/* The mode and the output size the evaluations that follow use. */
int ae3d_dlss_set_options(int mode, unsigned out_w, unsigned out_h);

/* A frame begins: the token the frame's constants, tags and evaluation share. */
int ae3d_dlss_begin_frame(unsigned frame_index);
int ae3d_dlss_set_constants(const ae3d_dlss_camera *camera);
/* The four images of the frame, tagged at once, recorded into `command_buffer`. */
int ae3d_dlss_tag(const ae3d_dlss_image *depth, const ae3d_dlss_image *motion,
                  const ae3d_dlss_image *colour_in, const ae3d_dlss_image *colour_out,
                  void *command_buffer);
/* The upscale, recorded into `command_buffer` (a VkCommandBuffer outside any
   render pass). The caller rebinds its own state afterwards. */
int ae3d_dlss_evaluate(void *command_buffer);

const char *ae3d_dlss_last_error(void);
void ae3d_dlss_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
