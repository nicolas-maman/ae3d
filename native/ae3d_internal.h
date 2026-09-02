#ifndef AE3D_INTERNAL_H
#define AE3D_INTERNAL_H

const float    *ae3d_mesh_vertex_data(void *mesh);
const unsigned *ae3d_mesh_index_data(void *mesh);
const float    *ae3d_inst_matrix_data(void *inst);
const float    *ae3d_inst_color_data(void *inst);
const unsigned char *ae3d_image_pixels(void *img);

#ifdef __APPLE__
void *ae3d_vk_native_layer(void *window);
#endif

#endif
