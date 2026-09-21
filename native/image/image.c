#include "ae3d.h"
#include "internal.h"

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

/* Images the program made rather than read: a palette a mesher wrote, a map
   a tool painted. They are kept under a name, and a texture asks for them by
   that name through the same load as a file, so a model textured from memory
   is set up like any other and both backends bind it the same way. Rows are
   stored bottom first, as a loaded file's are after the flip, so a caller
   building one writes its top row last. */
typedef struct {
    char *name;
    unsigned char *pixels;
    int width;
    int height;
} ae3d_image_entry;

static ae3d_image_entry *g_registered;
static int g_registered_count;
static int g_registered_capacity;

static ae3d_image_entry *ae3d_image_find(const char *name) {
    int i;
    for (i = 0; i < g_registered_count; i++) {
        if (strcmp(g_registered[i].name, name) == 0) return &g_registered[i];
    }
    return NULL;
}

int ae3d_image_register(const char *name, const void *rgba, int width, int height) {
    ae3d_image_entry *entry;
    unsigned char *copy;
    size_t bytes;

    if (!name || !*name || !rgba || width <= 0 || height <= 0) {
        snprintf(g_image_error, sizeof(g_image_error), "bad registered image");
        return 0;
    }
    bytes = (size_t)width * (size_t)height * 4;
    copy = (unsigned char *)malloc(bytes);
    if (!copy) {
        snprintf(g_image_error, sizeof(g_image_error), "out of memory");
        return 0;
    }
    memcpy(copy, rgba, bytes);

    entry = ae3d_image_find(name);
    if (entry) {
        free(entry->pixels);
    } else {
        if (g_registered_count == g_registered_capacity) {
            int grown = g_registered_capacity ? g_registered_capacity * 2 : 8;
            ae3d_image_entry *wider = (ae3d_image_entry *)realloc(
                g_registered, (size_t)grown * sizeof(ae3d_image_entry));
            if (!wider) {
                free(copy);
                snprintf(g_image_error, sizeof(g_image_error), "out of memory");
                return 0;
            }
            g_registered = wider;
            g_registered_capacity = grown;
        }
        entry = &g_registered[g_registered_count++];
        entry->name = (char *)malloc(strlen(name) + 1);
        if (!entry->name) {
            free(copy);
            g_registered_count--;
            snprintf(g_image_error, sizeof(g_image_error), "out of memory");
            return 0;
        }
        strcpy(entry->name, name);
    }
    entry->pixels = copy;
    entry->width = width;
    entry->height = height;
    return 1;
}

void ae3d_image_unregister(const char *name) {
    ae3d_image_entry *entry = name ? ae3d_image_find(name) : NULL;
    if (!entry) return;
    free(entry->name);
    free(entry->pixels);
    *entry = g_registered[--g_registered_count];
}

void *ae3d_image_load(const char *path) {
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels;
    ae3d_image_entry *entry;

    if (!path || !*path) {
        snprintf(g_image_error, sizeof(g_image_error), "empty path");
        return NULL;
    }
    entry = ae3d_image_find(path);
    if (entry) {
        size_t bytes = (size_t)entry->width * (size_t)entry->height * 4;
        pixels = (unsigned char *)malloc(bytes);
        if (!pixels) {
            snprintf(g_image_error, sizeof(g_image_error), "out of memory");
            return NULL;
        }
        memcpy(pixels, entry->pixels, bytes);
        return ae3d_image_wrap(pixels, entry->width, entry->height, 0);
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
