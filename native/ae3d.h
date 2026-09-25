#ifndef AE3D_H
#define AE3D_H

#define AE3D_API_GL     0
#define AE3D_API_VULKAN 1

#define AE3D_CURSOR_NORMAL   0
#define AE3D_CURSOR_HIDDEN   1
#define AE3D_CURSOR_DISABLED 2

/* The mesh and instance stores are ae3d.geometry's; the renderers read them
   through native/gpu/stores.h. These two read any field of one the way the
   renderers do, for the test that holds the two layouts together. */
int    ae3d_store_size(int which);
double ae3d_store_field(void *store, int which, int field);

/* An OpenGL entry point by name, from the process or the window system:
   what ae3d.glapi resolves every GL call it makes through (#398). */
void  *ae3d_gl_proc(const char *name);

/* Scripts: a compiled Aether source opened as a shared library, with the
 * object it is attached to passed in on every call. */

int    ae3d_vk_update_instances(int handle, void *instances);


/* GPU time per pass, read three frames late so the read never waits. */


void  *ae3d_offscreen_context(int width, int height);
void   ae3d_offscreen_context_destroy(void *context);



int    ae3d_vk_available(void);
const char *ae3d_vk_device_name(void);
const char *ae3d_vk_last_error(void);
int    ae3d_vk_init(void *win, int width, int height);
void   ae3d_vk_shutdown(void);
void   ae3d_vk_resize(int width, int height);
int    ae3d_vk_frame_begin(double r, double g, double b, double a);
int    ae3d_vk_frame_end(void);
int    ae3d_vk_upload_mesh(void *mesh);
int    ae3d_vk_upload_instances(void *instances);
void   ae3d_vk_free_mesh(int handle);
int    ae3d_vk_texture_adopt(void *image, void *memory, void *view, int width, int height, int mip_levels, int kind);
void   ae3d_vk_set_texture_maker(void *maker);
void  *ae3d_vk_once_begin(void);
void   ae3d_vk_once_end(void *commands);
void   ae3d_vk_texture_destroy(int handle);
void   ae3d_vk_scene_set_float(int offset, double value);
void   ae3d_vk_scene_set_int(int offset, int value);
void   ae3d_vk_scene_set_vec3(int offset, double x, double y, double z);
void   ae3d_vk_scene_set_mat4(int offset, const double *m);
void   ae3d_vk_scene_set_clip_mat4(int offset, const double *m);
void   ae3d_vk_scene_set_mat4v(int offset, int count, const void *values);
void   ae3d_vk_set_normal_map(int handle);
void   ae3d_vk_set_blend(int on);
void   ae3d_vk_draw(int mesh, int texture, int instances, int instance_count);
void   ae3d_vk_draw_sky(int mesh, int texture, int weather, int shape);
void   ae3d_vk_set_screen_quad(int mesh);
int    ae3d_vk_scene_offset(const char *name);
void   ae3d_vk_scene_set_float_array(int offset, void *handle);
void   ae3d_vk_scene_set_vec3_array(int offset, void *handle);
void   ae3d_vk_set_face_culling(int on);
void   ae3d_vk_set_program(int program);
int    ae3d_vk_program_count(void);
void   ae3d_vk_set_shadows(int on);
int    ae3d_vk_shadows(void);
int    ae3d_vk_live_shadow_targets(void);
int    ae3d_vk_shadow_begin(void);
void   ae3d_vk_shadow_draw(int mesh, int instances, int instance_count);
void   ae3d_vk_shadow_end(void);
void   ae3d_vk_set_post(int fxaa, int bloom, double threshold, double intensity);
int    ae3d_vk_post_active(void);
int    ae3d_vk_draw_calls(void);
int    ae3d_vk_mesh_shared(int handle);
/* GPU time per pass -- 0 shadow, 1 scene, 2 post -- from the frame whose
   fence was last waited on, and what that frame cost in changes of mind. */
double ae3d_vk_pass_ms(int pass);
int    ae3d_vk_pipeline_binds(void);
int    ae3d_vk_set_binds(void);
int    ae3d_vk_sample_count(void);
int    ae3d_vk_offscreen_width(void);
int    ae3d_vk_offscreen_height(void);
void   ae3d_vk_set_pose_bank(int texture_handle);
void   ae3d_vk_set_scene_depth(int on);
int    ae3d_vk_scene_depth_ready(void);
int    ae3d_vk_resolve_scene_depth(void);
int    ae3d_vk_velocity_texture(void);
int    ae3d_vk_render_width(void);
int    ae3d_vk_render_height(void);
double ae3d_vk_set_render_scale(double scale);
double ae3d_vk_render_scale(void);
/* DLSS through Streamline (native/ae3d_dlss.h): asked for before Vulkan
   starts, a mode set once the device is up, the camera given every frame. */
int    ae3d_vk_request_dlss(const char *directory);
void   ae3d_vk_set_samples(int samples);
int    ae3d_vk_dlss_available(void);
int    ae3d_vk_set_dlss(int mode);
int    ae3d_vk_dlss(void);
int    ae3d_vk_dlss_failed(void);
/* Rays (VK_KHR_ray_query): whether the device traces; the frame's instances
   for the scene's structure, added before the passes and built once; and
   the shadows traced through it (docs/rendering.md, "Ray-traced shadows"). */
int    ae3d_vk_ray_query(void);
void   ae3d_vk_ray_begin(void);
int    ae3d_vk_ray_reserve(int count, int statics);
int    ae3d_vk_ray_indirect(void);
void   ae3d_vk_ray_add(int mesh_handle, const float *matrices, int count);
void   ae3d_vk_ray_add_one(int mesh_handle, const double *matrix);
int    ae3d_vk_ray_build(void);
void   ae3d_vk_set_ray_shadows(int on);
void   ae3d_vk_set_ray_reach(double metres);
int    ae3d_vk_ray_empty(void);
int    ae3d_vk_max_texture_size(void);
void   ae3d_vk_flush_uploads(void);
int    ae3d_vk_add_frame_hooks(void *record, void *collect, void *release, void *context);
int    ae3d_vk_ready(void);
void   ae3d_vk_frame_wait(int slot);
int    ae3d_vk_frame_bgr(void);
void  *ae3d_vk_instance_handle(void);
void  *ae3d_vk_physical_device_handle(void);
void  *ae3d_vk_device_handle(void);
void  *ae3d_vk_frame_commands(void);
int    ae3d_vk_frame_slot(void);
int    ae3d_vk_frames_in_flight(void);
void  *ae3d_vk_frame_image(void);
int    ae3d_vk_frame_offscreen(void);
int    ae3d_vk_frame_copyable(void);
void   ae3d_vk_set_ray_budget(int figures);
int    ae3d_vk_ray_budget(void);
int    ae3d_vk_ray_figures(void);
double ae3d_vk_ray_reach_now(void);
int    ae3d_vk_ray_shadows(void);
int    ae3d_vk_ray_shadows_now(void);
void   ae3d_vk_dlss_camera(const double *view, const double *projection,
                           const double *prev_view, const double *prev_projection,
                           double jitter_x, double jitter_y,
                           double near_plane, double far_plane, double fov, double aspect,
                           const double *position, const double *up, const double *right, const double *forward);
void   ae3d_vk_set_capture_bypass(int on);
/* A crowd sorted on the device (crowd_sort_vk.comp): its figures' state
   written as eight floats each into the frame's buffer, the sort dispatched
   before the passes, and the tiers drawn by the counts it wrote. */
int    ae3d_vk_crowd_create(int capacity);
void   ae3d_vk_crowd_destroy(int handle);
int    ae3d_vk_crowd_count(int handle, int tier);
void   ae3d_vk_crowd_set_tier(int handle, int tier, int part, double sx, double sy, double sz,
                              int indices, int shadow_indices);
int    ae3d_vk_crowd_fill(int handle, const double *pos, const double *yaw, const double *phase,
                          const double *col, int start, int n);
int    ae3d_vk_crowd_sort(int handle, double cx, double cz, double near_dist, double mid_dist,
                          double cull_dist);
void   ae3d_vk_draw_crowd_tier(int mesh_handle, int texture_handle, int handle, int tier, int part);
/* A crowd in the rays: the far mesh at every frame of its pose bank as a
   bottom-level structure each, and the crowd's figures pointing at them. */
int    ae3d_vk_pose_blas_create(void *mesh, const float *bank, int frames, int bones);
void   ae3d_vk_pose_blas_destroy(int handle);
void   ae3d_vk_crowd_set_poses(int handle, int poses);
void   ae3d_vk_shadow_draw_crowd_tier(int mesh_handle, int handle, int tier, int part);
void   ae3d_vk_set_taa(int on);
int    ae3d_vk_taa(void);
int    ae3d_vk_taa_history(void);
int    ae3d_vk_frame_width(void);
int    ae3d_vk_frame_height(void);


/* The asking end of the same channel, so a tool that measures a scene can be
   written against the engine rather than against a copy of the protocol. */


/* A parallel for over the engine's job pool (native/gpu/jobs.c): the range
   [0, count) in blocks of at least `grain` elements, done when it returns.
   The pool is ae3d.jobs; the engine installs the runner that reaches it,
   and without one the calling thread does the range alone. */
typedef void (*ae3d_job_fn)(void *ctx, int start, int end);
typedef void (*ae3d_jobs_runner_fn)(int count, int grain, ae3d_job_fn fn, void *ctx);
void ae3d_jobs_set_runner(ae3d_jobs_runner_fn runner);
void ae3d_job_call(ae3d_job_fn fn, void *ctx, int start, int end);
void ae3d_jobs_for(int count, int grain, ae3d_job_fn fn, void *ctx);
/* How many numbers the fixed-size answers -- a pixel, a region, a diff -- are
   written into. A grid is as long as it has cells and says so. */

#endif
