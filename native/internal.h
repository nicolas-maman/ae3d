#ifndef AE3D_INTERNAL_H
#define AE3D_INTERNAL_H

#include "gpu/stores.h"

const unsigned char *ae3d_image_pixels(void *img);

#ifdef __APPLE__
void *ae3d_vk_native_layer(void *window);
#endif

#endif
