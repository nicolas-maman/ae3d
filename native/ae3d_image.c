#include "ae3d.h"
#include "ae3d_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_NO_STDIO_WRITE
#include "stb_image.h"

typedef struct {
    unsigned char *pixels;
    int width;
    int height;
    int owns_stb;
} ae3d_image;

static char g_image_error[512];

const char *ae3d_image_error(void) { return g_image_error; }

static void *ae3d_image_wrap(unsigned char *pixels, int width, int height, int owns_stb) {
    ae3d_image *img;
    if (!pixels) return NULL;
    img = (ae3d_image *)calloc(1, sizeof(ae3d_image));
    if (!img) {
        if (owns_stb) stbi_image_free(pixels); else free(pixels);
        snprintf(g_image_error, sizeof(g_image_error), "out of memory");
        return NULL;
    }
    img->pixels = pixels;
    img->width = width;
    img->height = height;
    img->owns_stb = owns_stb;
    return img;
}

void *ae3d_image_load(const char *path) {
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels;

    if (!path || !*path) {
        snprintf(g_image_error, sizeof(g_image_error), "empty path");
        return NULL;
    }
    stbi_set_flip_vertically_on_load(1);
    pixels = stbi_load(path, &width, &height, &channels, 4);
    if (!pixels) {
        snprintf(g_image_error, sizeof(g_image_error), "%s: %s", path, stbi_failure_reason());
        return NULL;
    }
    return ae3d_image_wrap(pixels, width, height, 1);
}

void *ae3d_image_from_memory(const void *data, int len) {
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels;

    if (!data || len <= 0) {
        snprintf(g_image_error, sizeof(g_image_error), "empty buffer");
        return NULL;
    }
    stbi_set_flip_vertically_on_load(1);
    pixels = stbi_load_from_memory((const stbi_uc *)data, len, &width, &height, &channels, 4);
    if (!pixels) {
        snprintf(g_image_error, sizeof(g_image_error), "%s", stbi_failure_reason());
        return NULL;
    }
    return ae3d_image_wrap(pixels, width, height, 1);
}

void *ae3d_image_solid(int width, int height, int r, int g, int b, int a) {
    unsigned char *pixels;
    long i, count;

    if (width <= 0 || height <= 0) {
        snprintf(g_image_error, sizeof(g_image_error), "bad dimensions");
        return NULL;
    }
    count = (long)width * height;
    pixels = (unsigned char *)malloc((size_t)count * 4);
    if (!pixels) {
        snprintf(g_image_error, sizeof(g_image_error), "out of memory");
        return NULL;
    }
    for (i = 0; i < count; i++) {
        pixels[i * 4 + 0] = (unsigned char)r;
        pixels[i * 4 + 1] = (unsigned char)g;
        pixels[i * 4 + 2] = (unsigned char)b;
        pixels[i * 4 + 3] = (unsigned char)a;
    }
    return ae3d_image_wrap(pixels, width, height, 0);
}

void ae3d_image_free(void *handle) {
    ae3d_image *img = (ae3d_image *)handle;
    if (!img) return;
    if (img->owns_stb) stbi_image_free(img->pixels); else free(img->pixels);
    free(img);
}

int ae3d_image_width(void *handle) {
    ae3d_image *img = (ae3d_image *)handle;
    return img ? img->width : 0;
}

int ae3d_image_height(void *handle) {
    ae3d_image *img = (ae3d_image *)handle;
    return img ? img->height : 0;
}

const unsigned char *ae3d_image_pixels(void *handle) {
    ae3d_image *img = (ae3d_image *)handle;
    return img ? img->pixels : NULL;
}
