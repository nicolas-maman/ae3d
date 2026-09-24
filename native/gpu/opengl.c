#include "ae3d.h"
#include "internal.h"
#include "opengl_api.h"

/* For glfwGetCurrentContext alone: whether a GL context is current on this
   thread, which cleanup that can outlive the window asks before deleting GL
   objects. Deleting with no context is undefined: a desktop driver returns
   early, llvmpipe dereferences the missing context and crashes. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>


typedef struct {
    float *values;
    int    count;
} ae3d_farr;


static char g_gl_version[128];
static char g_gl_renderer[128];
static int  g_loaded;

int ae3d_gl_load(void) {
    int missing;
    if (g_loaded) return 0;
    missing = ae3d_glapi_load();
    if (missing == 0) {
        const GLubyte *version = glGetString(GL_VERSION);
        const GLubyte *renderer = glGetString(GL_RENDERER);
        snprintf(g_gl_version, sizeof(g_gl_version), "%s", version ? (const char *)version : "");
        snprintf(g_gl_renderer, sizeof(g_gl_renderer), "%s", renderer ? (const char *)renderer : "");
        g_loaded = 1;
    }
    return missing;
}

const char *ae3d_gl_version(void) { return g_gl_version; }
const char *ae3d_gl_renderer(void) { return g_gl_renderer; }
/* The state calls -- viewport, clears, depth, culling, blending, the
   vertex arrays and buffers -- are ae3d.gl's, in Aether (#398). */

/* The geometry cache, the uploads, the vertex and instance attributes and
   the draws are ae3d.gl's, in Aether (#398). */

/* Programs and uniforms are ae3d.gl's, in Aether (#398). The float arrays
   below stay C while vulkan.c reads them. */

void *ae3d_farr_create(int count) {
    ae3d_farr *arr;
    if (count <= 0) return NULL;
    arr = (ae3d_farr *)calloc(1, sizeof(ae3d_farr));
    if (!arr) return NULL;
    arr->values = (float *)calloc((size_t)count, sizeof(float));
    if (!arr->values) { free(arr); return NULL; }
    arr->count = count;
    return arr;
}

void ae3d_farr_destroy(void *handle) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (!arr) return;
    free(arr->values);
    free(arr);
}

void ae3d_farr_set(void *handle, int i, double v) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (!arr || i < 0 || i >= arr->count) return;
    arr->values[i] = (float)v;
}

double ae3d_farr_get(void *handle, int i) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (!arr || i < 0 || i >= arr->count) return 0.0;
    return arr->values[i];
}

int ae3d_farr_count(void *handle) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    return arr ? arr->count : 0;
}

/* The array uniforms and the textures are ae3d.gl's, in Aether (#398). */

/* The pass timers, the framebuffers and their attachments, the resolves,
   the scene's depth copy, the frame's readback and the meter are ae3d.gl's,
   in Aether (#398). */
