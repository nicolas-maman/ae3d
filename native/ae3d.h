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
                          int api, int msaa, int decorated, int depth_bits);
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
void   ae3d_mesh_destroy(void *mesh);
int    ae3d_mesh_reserve(void *mesh, int vertices, int indices);
int    ae3d_mesh_push_vertex(void *mesh, double px, double py, double pz,
                             double u, double v,
                             double nx, double ny, double nz);
int    ae3d_mesh_push_index(void *mesh, int index);
int    ae3d_mesh_vertex_count(void *mesh);
int    ae3d_mesh_index_count(void *mesh);
double ae3d_mesh_pos_x(void *mesh, int i);
double ae3d_mesh_pos_y(void *mesh, int i);
double ae3d_mesh_pos_z(void *mesh, int i);
void   ae3d_mesh_set_pos(void *mesh, int i, double x, double y, double z);
void   ae3d_mesh_set_uv(void *mesh, int i, double u, double v);
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
void   ae3d_mesh_clear_dirty(void *mesh);

void  *ae3d_inst_create(void);
void   ae3d_inst_destroy(void *inst);
int    ae3d_inst_resize(void *inst, int count);
int    ae3d_inst_count(void *inst);
void   ae3d_inst_set_trs(void *inst, int i,
                         double px, double py, double pz,
                         double sx, double sy, double sz,
                         double qx, double qy, double qz, double qw);
void   ae3d_inst_set_positions(void *inst, const double *xyz, int count,
                               double sx, double sy, double sz,
                               double qx, double qy, double qz, double qw);
void   ae3d_inst_set_colors(void *inst, const double *rgb, int count);
void   ae3d_inst_set_matrix(void *inst, int i, const double *m);
void   ae3d_inst_remove(void *inst, int i);
int    ae3d_inst_enable_colors(void *inst, int count);
int    ae3d_inst_has_colors(void *inst);
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

int    ae3d_gl_fbo_create(void);
void   ae3d_gl_fbo_bind(int fbo);
void   ae3d_gl_fbo_delete(int fbo);
int    ae3d_gl_fbo_attach_color(int fbo, int width, int height, int hdr);
int    ae3d_gl_fbo_attach_depth(int fbo, int width, int height);
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
void   ae3d_vk_free_mesh(int handle);
void   ae3d_vk_draw_mesh(int handle, const double *mvp,
                         double cr, double cg, double cb);

#endif
