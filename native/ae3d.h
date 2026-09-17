#ifndef AE3D_H
#define AE3D_H

#define AE3D_API_GL     0
#define AE3D_API_VULKAN 1

#define AE3D_CURSOR_NORMAL   0
#define AE3D_CURSOR_HIDDEN   1
#define AE3D_CURSOR_DISABLED 2

int    ae3d_platform_init(void);
void   ae3d_platform_shutdown(void);
const char *ae3d_platform_error(void);

void  *ae3d_window_create(int width, int height, const char *title,
                          int api, int msaa, int decorated, int visible,
                          int depth_bits);
void   ae3d_window_destroy(void *win);
int    ae3d_window_should_close(void *win);
void   ae3d_window_close(void *win);
void   ae3d_window_swap(void *win);
void   ae3d_window_make_current(void *win);
void   ae3d_window_set_vsync(int interval);
int    ae3d_window_width(void *win);
int    ae3d_window_height(void *win);
int    ae3d_window_fb_width(void *win);
int    ae3d_window_fb_height(void *win);
void   ae3d_window_set_pos(void *win, int x, int y);
void   ae3d_window_center(void *win);
void   ae3d_window_set_title(void *win, const char *title);
void   ae3d_window_set_cursor_mode(void *win, int mode);
int    ae3d_monitor_width(void);
int    ae3d_monitor_height(void);
void   ae3d_poll_events(void);
double ae3d_time(void);
int    ae3d_key_down(void *win, int key);
int    ae3d_mouse_down(void *win, int button);
double ae3d_cursor_x(void *win);
double ae3d_cursor_y(void *win);
double ae3d_scroll_delta(void *win);

void  *ae3d_mesh_create(void);
void  *ae3d_mesh_clone(void *mesh);
void  *ae3d_mesh_decimate(void *mesh, double cell_size);
void   ae3d_mesh_destroy(void *mesh);
int    ae3d_mesh_reserve(void *mesh, int vertices, int indices);
int    ae3d_mesh_push_vertex(void *mesh, double px, double py, double pz,
                             double u, double v,
                             double nx, double ny, double nz);
int    ae3d_mesh_push_index(void *mesh, int index);
void   ae3d_mesh_set_occlusion(void *mesh, int i, double value);
double ae3d_mesh_occlusion(void *mesh, int i);
int    ae3d_mesh_set_skin(void *mesh, int i, int j0, int j1, int j2, int j3,
                          double w0, double w1, double w2, double w3);
int    ae3d_mesh_is_skinned(void *mesh);
void  *ae3d_palette_create(int bones);
void   ae3d_palette_destroy(void *palette);
int    ae3d_palette_bones(void *palette);
void   ae3d_palette_set(void *palette, int bone, const double *m);
double ae3d_palette_get(void *palette, int bone, int i);
const float *ae3d_palette_data(void *palette);
void  *ae3d_posebank_create(int frames, int bones);
void   ae3d_posebank_destroy(void *bank);
int    ae3d_posebank_frames(void *bank);
int    ae3d_posebank_bones(void *bank);
void   ae3d_posebank_capture(void *bank, int frame, void *palette);
double ae3d_posebank_get(void *bank, int frame, int bone, int i);
const float *ae3d_posebank_data(void *bank);
void   ae3d_posebank_set_travel(void *bank, int frame, double distance);
double ae3d_posebank_travel(void *bank, int frame);
double ae3d_posebank_speed(void *bank, double phase, double duration);
int    ae3d_gl_posebank_texture(void *bank);
void   ae3d_gl_setup_instance_phase(void *inst, int phase_vbo);
void   ae3d_gl_update_instance_phases(void *inst, int phase_vbo);
void  *ae3d_horde_grid_create(int cols, int per_cell);
void   ae3d_horde_grid_destroy(void *grid);
void   ae3d_horde_separate(void *grid, double *pos, double *vel, int n,
                           double ox, double oz, double cell_size,
                           double radius, double strength);
void   ae3d_crowd_wander(double *vel, const double *yaw, int n, double speed);
void   ae3d_crowd_step(double *pos, const double *vel, double *yaw, double *phase,
                       int n, double dt, double max_speed,
                       double x0, double x1, double z0, double z1,
                       double road_y, double walk, void *bank);
int    ae3d_crowd_bucket(const double *pos, const double *yaw, const double *phase,
                         const double *col, int n,
                         double cx, double cz, double near_dist, double cull_dist,
                         double *np, double *ny, double *nph, double *ncol,
                         double *fp, double *fy, double *fph, double *fcol,
                         double *far_out);
void   ae3d_gl_upload_skin(void *mesh, int vbo);
void  *ae3d_skinrows_create(int count);
void   ae3d_skinrows_destroy(void *rows);
int    ae3d_skinrows_count(void *rows);
void   ae3d_skinrows_set(void *rows, int i, int j0, int j1, int j2, int j3,
                         double w0, double w1, double w2, double w3);
void   ae3d_skinrows_apply(void *rows, void *mesh, int vertex, int position);
void   ae3d_objbuild_set_skin(void *build, void *rows);
void   ae3d_objbuild_set_occlusion(void *build, void *values);
void   ae3d_gl_uniform_mat4v(int loc, int count, const void *values);
const float *ae3d_mesh_skin_data(void *mesh);
double ae3d_mesh_skin_joint(void *mesh, int i, int slot);
double ae3d_mesh_skin_weight(void *mesh, int i, int slot);
double ae3d_mesh_surface_area(void *mesh, double sx, double sy, double sz);
double ae3d_mesh_uv_area(void *mesh);
double ae3d_mesh_extent(void *mesh, int axis);
int    ae3d_mesh_distinct_planes(void *mesh, double tolerance);
int    ae3d_mesh_vertex_count(void *mesh);
int    ae3d_mesh_index_count(void *mesh);
double ae3d_mesh_pos_x(void *mesh, int i);
double ae3d_mesh_pos_y(void *mesh, int i);
double ae3d_mesh_pos_z(void *mesh, int i);
void   ae3d_mesh_set_pos(void *mesh, int i, double x, double y, double z);
void   ae3d_mesh_set_uv(void *mesh, int i, double u, double v);
double ae3d_mesh_norm_x(void *mesh, int i);
double ae3d_mesh_norm_y(void *mesh, int i);
double ae3d_mesh_norm_z(void *mesh, int i);
void   ae3d_mesh_set_normal(void *mesh, int i, double x, double y, double z);
int    ae3d_mesh_index_at(void *mesh, int i);
void   ae3d_mesh_recalc_normals(void *mesh);
void   ae3d_mesh_compute_bounds(void *mesh,
                                double px, double py, double pz,
                                double sx, double sy, double sz,
                                double qx, double qy, double qz, double qw);
double ae3d_mesh_bound_x(void *mesh);
double ae3d_mesh_bound_y(void *mesh);
double ae3d_mesh_bound_z(void *mesh);
double ae3d_mesh_bound_radius(void *mesh);
int    ae3d_mesh_dirty(void *mesh);

/* Scripts: a compiled Aether source opened as a shared library, with the
 * object it is attached to passed in on every call. */
void  *ae3d_script_open(const char *path);
int    ae3d_script_has_start(void *script);
void   ae3d_script_start(void *script, void *model);
void   ae3d_script_update(void *script, void *model, double delta);
void   ae3d_script_close(void *script);
const char *ae3d_script_error(void);
const char *ae3d_script_suffix(void);
void   ae3d_mesh_clear_dirty(void *mesh);

void  *ae3d_inst_create(void);
void   ae3d_inst_destroy(void *inst);
int    ae3d_inst_resize(void *inst, int count);
int    ae3d_inst_count(void *inst);
void   ae3d_inst_set_count(void *inst, int count);
void   ae3d_inst_set_trs(void *inst, int i,
                         double px, double py, double pz,
                         double sx, double sy, double sz,
                         double qx, double qy, double qz, double qw);
void   ae3d_inst_set_positions(void *inst, const double *xyz, int count,
                               double sx, double sy, double sz,
                               double qx, double qy, double qz, double qw);
void   ae3d_inst_set_positions_yaw(void *inst, const double *xyz, const double *yaw,
                                   int count, double sx, double sy, double sz);
void   ae3d_inst_set_colors(void *inst, const double *rgb, int count);
void   ae3d_inst_set_matrix(void *inst, int i, const double *m);
void   ae3d_inst_remove(void *inst, int i);
int    ae3d_inst_enable_colors(void *inst, int count);
int    ae3d_inst_has_colors(void *inst);
int    ae3d_inst_enable_phases(void *inst, int count);
int    ae3d_inst_has_phases(void *inst);
const float *ae3d_inst_phase_data(void *inst);
void   ae3d_inst_set_phases(void *inst, const double *phases, int count);
void   ae3d_inst_set_phase(void *inst, int i, double phase);
void   ae3d_gl_update_instance_colors(void *inst, int color_vbo);
int    ae3d_vk_update_instances(int handle, void *instances);
void   ae3d_inst_set_color(void *inst, int i, double r, double g, double b);
void   ae3d_inst_compute_bounds(void *inst);
double ae3d_inst_bound_x(void *inst);
double ae3d_inst_bound_y(void *inst);
double ae3d_inst_bound_z(void *inst);
double ae3d_inst_bound_radius(void *inst);

void  *ae3d_objbuild_create(void);
void   ae3d_objbuild_destroy(void *build);
int    ae3d_objbuild_add_position(void *build, double x, double y, double z);
int    ae3d_objbuild_add_uv(void *build, double u, double v);
int    ae3d_objbuild_add_normal(void *build, double x, double y, double z);
int    ae3d_objbuild_position_count(void *build);
int    ae3d_objbuild_uv_count(void *build);
int    ae3d_objbuild_normal_count(void *build);
int    ae3d_objbuild_emit(void *build, void *mesh, int v, int vt, int vn);

void  *ae3d_image_load(const char *path);
void  *ae3d_image_from_memory(const void *data, int len);
void  *ae3d_image_solid(int width, int height, int r, int g, int b, int a);
void   ae3d_image_free(void *img);
int    ae3d_image_width(void *img);
int    ae3d_image_height(void *img);
const char *ae3d_image_error(void);

int    ae3d_gl_load(void);
/* Whether a GL context is current on this thread; cleanup that can outlive the
   window checks this before deleting GL objects. */
int    ae3d_gl_context_current(void);
const char *ae3d_gl_version(void);
const char *ae3d_gl_renderer(void);
int    ae3d_gl_error(void);

void   ae3d_gl_viewport(int x, int y, int w, int h);
void   ae3d_gl_clear_color(double r, double g, double b, double a);
void   ae3d_gl_clear(int color, int depth);
void   ae3d_gl_clear_depth(double d);
void   ae3d_gl_set_depth_test(int on);
void   ae3d_gl_set_depth_mask(int on);
void   ae3d_gl_set_face_culling(int on);
void   ae3d_gl_set_blend(int on);
void   ae3d_gl_set_multisample(int on);
void   ae3d_gl_set_wireframe(int on);
int    ae3d_gl_viewport_width(void);
int    ae3d_gl_viewport_height(void);

int    ae3d_gl_vao_create(void);
void   ae3d_gl_vao_bind(int vao);
void   ae3d_gl_vao_delete(int vao);
int    ae3d_gl_buffer_create(void);
void   ae3d_gl_buffer_delete(int buf);
int    ae3d_gl_geometry_acquire(void *mesh);
int    ae3d_gl_geometry_instance_vbo(int vao);
void   ae3d_gl_geometry_release(int vao);
void   ae3d_gl_geometry_shutdown(void);
int    ae3d_gl_batch_upload(int vao, int instance_vbo, void *inst, int capacity_bytes);
void   ae3d_gl_set_default_instance_color(void);
int    ae3d_gl_geometry_instance_capacity(int vao);
void   ae3d_gl_geometry_set_instance_capacity(int vao, int capacity);
void   ae3d_gl_upload_mesh(void *mesh, int vbo, int ebo);
void   ae3d_gl_setup_vertex_attribs(void);
void   ae3d_gl_setup_instance_attribs(void *inst, int matrix_vbo, int color_vbo);
int    ae3d_gl_update_instances(void *inst, int matrix_vbo, int capacity_bytes);
void   ae3d_gl_update_mesh_vertices(void *mesh, int vbo);
void   ae3d_gl_draw_elements(int count, int byte_offset);
void   ae3d_gl_draw_elements_instanced(int count, int byte_offset, int instances);
void   ae3d_gl_draw_arrays(int first, int count);

int    ae3d_gl_program_create(const char *vertex_src, const char *fragment_src);
void   ae3d_gl_program_delete(int program);
void   ae3d_gl_program_use(int program);
const char *ae3d_gl_program_log(void);
int    ae3d_gl_uniform_location(int program, const char *name);
void   ae3d_gl_uniform_int(int loc, int v);
void   ae3d_gl_uniform_float(int loc, double v);
void   ae3d_gl_uniform_vec2(int loc, double x, double y);
void   ae3d_gl_uniform_vec3(int loc, double x, double y, double z);
void   ae3d_gl_uniform_vec4(int loc, double x, double y, double z, double w);
void   ae3d_gl_uniform_mat4(int loc, const double *m);
void   ae3d_gl_uniform_float_array(int loc, void *arr);
void   ae3d_gl_uniform_vec3_array(int loc, void *arr);

void  *ae3d_farr_create(int count);
void   ae3d_farr_destroy(void *arr);
void   ae3d_farr_set(void *arr, int i, double v);
double ae3d_farr_get(void *arr, int i);
int    ae3d_farr_count(void *arr);

int    ae3d_gl_texture_from_image(void *img, int srgb, int mipmap);
int    ae3d_gl_texture_cubemap_from_image(void *img);
void   ae3d_gl_texture_delete(int texture);
void   ae3d_gl_texture_bind(int unit, int texture);
void   ae3d_gl_texture_bind_cubemap(int unit, int texture);

/* GPU time per pass, read three frames late so the read never waits. */
void  *ae3d_gl_passtimer_create(void);
void   ae3d_gl_passtimer_destroy(void *handle);
void   ae3d_gl_passtimer_frame(void *handle);
void   ae3d_gl_passtimer_begin(void *handle, int pass);
void   ae3d_gl_passtimer_end(void *handle);
double ae3d_gl_passtimer_ms(void *handle, int pass);

int    ae3d_gl_fbo_create(void);
void   ae3d_gl_fbo_bind(int fbo);
void   ae3d_gl_fbo_delete(int fbo);
int    ae3d_gl_fbo_attach_color(int fbo, int width, int height, int hdr);
int    ae3d_gl_fbo_attach_depth(int fbo, int width, int height);
int    ae3d_gl_max_samples(void);
int    ae3d_gl_fbo_attach_color_multisample(int fbo, int width, int height, int samples);
int    ae3d_gl_fbo_attach_depth_multisample(int fbo, int width, int height, int samples);
int    ae3d_gl_fbo_resolve(int source, int destination, int width, int height);
void   ae3d_gl_renderbuffer_delete(int rbo);
int    ae3d_gl_fbo_complete(void);
int    ae3d_gl_read_pixel(int x, int y);

void  *ae3d_offscreen_context(int width, int height);
void   ae3d_offscreen_context_destroy(void *context);
void  *ae3d_offscreen_create(int width, int height);
int    ae3d_offscreen_resize(void *target, int width, int height);
void   ae3d_offscreen_bind(void *target);
void   ae3d_offscreen_unbind(void);
int    ae3d_offscreen_width(void *target);
int    ae3d_offscreen_height(void *target);
int    ae3d_offscreen_byte_size(void *target);
void  *ae3d_offscreen_read(void *target);
void  *ae3d_offscreen_read_pipelined(void *target);

int    ae3d_png_write(const char *path, const void *rgba, int width, int height);
void   ae3d_rgba_set(void *rgba, int index, double r, double g, double b, double a);
int    ae3d_gl_snapshot(const char *path, int width, int height);
void   ae3d_offscreen_destroy(void *target);

int    ae3d_meshfile_save(const char *path, void *mesh, void *instances);
void  *ae3d_meshfile_load(const char *path);
void  *ae3d_meshfile_mesh(void *handle);
void  *ae3d_meshfile_instances(void *handle);
void   ae3d_meshfile_release(void *handle);
const char *ae3d_meshfile_error(void);

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
int    ae3d_vk_texture_create(int width, int height, const void *rgba);
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
void   ae3d_vk_draw_sky(int mesh, int texture);
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
void  *ae3d_vk_offscreen_pixels(void);
int    ae3d_vk_offscreen_width(void);
int    ae3d_vk_offscreen_height(void);
/* Windowed frame capture: arm it, then after a frame is submitted read the
   presented image back as RGBA (top row first). request returns 0 when the
   surface cannot be a transfer source. */
int    ae3d_vk_request_capture(void);
int    ae3d_vk_capture_ready(void);
void  *ae3d_vk_capture_pixels(void);
int    ae3d_vk_capture_width(void);
int    ae3d_vk_capture_height(void);

// The agent channel: a localhost NDJSON socket an agent drives the engine
// through. ae3d_agent_active() is what every hot path tests, and it is zero
// until AE3D_AGENT asks for the channel. See native/ae3d_agent.c.
int         ae3d_agent_start(void);
int         ae3d_agent_active(void);
int         ae3d_agent_port(void);
const char *ae3d_agent_next_request(void);
void        ae3d_agent_respond(const char *line);
void        ae3d_agent_stop(void);
const char *ae3d_agent_error(void);

/* The asking end of the same channel, so a tool that measures a scene can be
   written against the engine rather than against a copy of the protocol. */
void        ae3d_client_init(void);
int         ae3d_client_connect(const char *host, int port);
int         ae3d_client_send(int handle, const char *line);
const char *ae3d_client_read(int handle);
void        ae3d_client_close(int handle);
const char *ae3d_client_error(void);

int  ae3d_capture_frame(int width, int height);
int  ae3d_capture_width(void);
int  ae3d_capture_height(void);
int  ae3d_capture_pixel(int x, int y, double *out);
int  ae3d_capture_region(int x, int y, int width, int height,
                         int background, int tolerance, double *out);
int  ae3d_capture_grid(int left, int top, int width, int height,
                       int columns, int rows, int background, int tolerance,
                       double *out);
int  ae3d_capture_hold_reference(void);
int  ae3d_capture_diff(int tolerance, double *out);
void ae3d_capture_release(void);
int  ae3d_capture_adopt(const unsigned char *pixels, int width, int height);
/* How many numbers the fixed-size answers -- a pixel, a region, a diff -- are
   written into. A grid is as long as it has cells and says so. */
#define AE3D_CAPTURE_SLOTS 8
double ae3d_capture_slot(const double *block, int index);
double ae3d_capture_slot_of(const double *block, int index, int count);

#endif
