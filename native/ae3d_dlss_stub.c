/* DLSS without the Streamline SDK: every call says so. Built in place of
   native/ae3d_dlss.cpp when AE3D_STREAMLINE_ROOT is not set (build.sh), so
   the engine builds the same on a machine without the SDK, a Linux runner
   or a Mac, and a program asking for DLSS is told it was not built in. */
#include "ae3d_dlss.h"

int ae3d_dlss_built(void) { return 0; }
int ae3d_dlss_load(const char *directory) { (void)directory; return 0; }
void *ae3d_dlss_instance_proc_addr(void) { return 0; }
int ae3d_dlss_supported(void *vk_physical_device) { (void)vk_physical_device; return 0; }
int ae3d_dlss_optimal(int mode, unsigned out_w, unsigned out_h, unsigned *render_w, unsigned *render_h) {
    (void)mode; (void)out_w; (void)out_h; (void)render_w; (void)render_h;
    return 0;
}
int ae3d_dlss_set_options(int mode, unsigned out_w, unsigned out_h) { (void)mode; (void)out_w; (void)out_h; return 0; }
int ae3d_dlss_begin_frame(unsigned frame_index) { (void)frame_index; return 0; }
int ae3d_dlss_set_constants(const ae3d_dlss_camera *camera) { (void)camera; return 0; }
int ae3d_dlss_tag(const ae3d_dlss_image *depth, const ae3d_dlss_image *motion,
                  const ae3d_dlss_image *colour_in, const ae3d_dlss_image *colour_out,
                  void *command_buffer) {
    (void)depth; (void)motion; (void)colour_in; (void)colour_out; (void)command_buffer;
    return 0;
}
int ae3d_dlss_evaluate(void *command_buffer) { (void)command_buffer; return 0; }
const char *ae3d_dlss_last_error(void) { return "DLSS was not built in: build with AE3D_STREAMLINE_ROOT set to the Streamline SDK"; }
void ae3d_dlss_shutdown(void) {}
