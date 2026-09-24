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

/* A pass costs what the GPU spends on it, and the CPU time around a submit is
   not that number: the driver returns from a draw call long before the work is
   done, so a pass that doubled in cost on the GPU can read as unchanged.
   GL_TIME_ELAPSED brackets the work itself.

   The result is read a frame or two later, never in the frame that issued it.
   Asking for it immediately is a stall: the CPU waits for the GPU to drain,
   which both costs the frame time it is trying to measure and changes it. */
#define AE3D_GL_TIME_ELAPSED            0x88BF
#define AE3D_GL_QUERY_RESULT            0x8866
#define AE3D_GL_QUERY_RESULT_AVAILABLE  0x8867

#define AE3D_GL_PASSES 3
/* Deep enough that a result is always read long after the GPU has finished
   with it, so the read never waits. */
#define AE3D_GL_TIMER_DEPTH 3

typedef struct {
    GLuint id[AE3D_GL_TIMER_DEPTH][AE3D_GL_PASSES];
    int    issued[AE3D_GL_TIMER_DEPTH][AE3D_GL_PASSES];
    double ms[AE3D_GL_PASSES];
    int    slot;
    int    open;
} ae3d_gl_passtimer;

static int ae3d_gl_queries_available(void) {
    return ae3d_glGenQueries && ae3d_glDeleteQueries && ae3d_glBeginQuery
        && ae3d_glEndQuery && ae3d_glGetQueryObjectuiv
        && ae3d_glGetQueryObjectui64v;
}

void *ae3d_gl_passtimer_create(void) {
    ae3d_gl_passtimer *timer;
    int slot, pass;
    if (!ae3d_gl_queries_available()) return NULL;
    timer = (ae3d_gl_passtimer *)calloc(1, sizeof(*timer));
    if (!timer) return NULL;
    for (slot = 0; slot < AE3D_GL_TIMER_DEPTH; slot++)
        for (pass = 0; pass < AE3D_GL_PASSES; pass++)
            glGenQueries(1, &timer->id[slot][pass]);
    return timer;
}

void ae3d_gl_passtimer_destroy(void *handle) {
    ae3d_gl_passtimer *timer = (ae3d_gl_passtimer *)handle;
    int slot, pass;
    if (!timer) return;
    /* Only delete the query objects if a context is current to delete them in.
       When the window is already gone -- a program that frees its renderer
       after shutting the engine down, or a test that destroys its offscreen
       context first -- the queries died with the context, and calling
       glDeleteQueries against no context segfaults on llvmpipe. */
    if (glfwGetCurrentContext() != NULL) {
        for (slot = 0; slot < AE3D_GL_TIMER_DEPTH; slot++)
            for (pass = 0; pass < AE3D_GL_PASSES; pass++)
                if (timer->id[slot][pass]) glDeleteQueries(1, &timer->id[slot][pass]);
    }
    free(timer);
}

/* Moves to the slot this frame will write, collecting what that slot measured
   the last time round. A pass that was not drawn reports nothing rather than
   the last figure it happened to have. */
void ae3d_gl_passtimer_frame(void *handle) {
    ae3d_gl_passtimer *timer = (ae3d_gl_passtimer *)handle;
    int pass;
    if (!timer) return;
    timer->slot = (timer->slot + 1) % AE3D_GL_TIMER_DEPTH;
    for (pass = 0; pass < AE3D_GL_PASSES; pass++) {
        GLuint id = timer->id[timer->slot][pass];
        GLuint ready = 0;
        if (!timer->issued[timer->slot][pass]) {
            timer->ms[pass] = 0.0;
            continue;
        }
        timer->issued[timer->slot][pass] = 0;
        if (!id) continue;
        glGetQueryObjectuiv(id, AE3D_GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            unsigned long long elapsed = 0;
            glGetQueryObjectui64v(id, AE3D_GL_QUERY_RESULT, &elapsed);
            /* Nanoseconds. A double carries every integer to 2^53, so the
               conversion to milliseconds loses nothing a timer can measure. */
            timer->ms[pass] = (double)elapsed / 1000000.0;
        }
    }
}

void ae3d_gl_passtimer_begin(void *handle, int pass) {
    ae3d_gl_passtimer *timer = (ae3d_gl_passtimer *)handle;
    GLuint id;
    if (!timer || pass < 0 || pass >= AE3D_GL_PASSES || timer->open) return;
    id = timer->id[timer->slot][pass];
    if (!id) return;
    glBeginQuery(AE3D_GL_TIME_ELAPSED, id);
    timer->issued[timer->slot][pass] = 1;
    timer->open = 1;
}

void ae3d_gl_passtimer_end(void *handle) {
    ae3d_gl_passtimer *timer = (ae3d_gl_passtimer *)handle;
    if (!timer || !timer->open) return;
    glEndQuery(AE3D_GL_TIME_ELAPSED);
    timer->open = 0;
}

double ae3d_gl_passtimer_ms(void *handle, int pass) {
    ae3d_gl_passtimer *timer = (ae3d_gl_passtimer *)handle;
    if (!timer || pass < 0 || pass >= AE3D_GL_PASSES) return 0.0;
    return timer->ms[pass];
}

int ae3d_gl_fbo_create(void) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    return (int)fbo;
}

void ae3d_gl_fbo_bind(int fbo) { glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo); }

void ae3d_gl_fbo_delete(int fbo) {
    GLuint id = (GLuint)fbo;
    if (id) glDeleteFramebuffers(1, &id);
}

int ae3d_gl_fbo_attach_color(int fbo, int width, int height, int hdr) {
    GLuint texture = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, width, height, 0,
                 GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    return (int)texture;
}

/* The motion vectors beside the colour: a two-channel half-float texture
   at the framebuffer's second colour attachment, point sampled (a vector
   is not blended between pixels), and its multisampled renderbuffer twin
   for the frame that is drawn multisampled and resolved. */
int ae3d_gl_fbo_attach_velocity(int fbo, int width, int height) {
    GLuint texture = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, texture, 0);
    return (int)texture;
}

int ae3d_gl_fbo_attach_velocity_multisample(int fbo, int width, int height, int samples) {
    GLuint rbo = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RG16F, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, rbo);
    return (int)rbo;
}

/* How many of the bound framebuffer's colour attachments the draws write,
   from the first: two while the scene is drawn with its motion vectors,
   one for the passes after, whose shaders have no second output. */
void ae3d_gl_draw_buffers(int count) {
    GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    if (count < 1) count = 1;
    if (count > 2) count = 2;
    glDrawBuffers(count, buffers);
}

/* One colour attachment of the bound framebuffer cleared to a value of
   its own: the motion vectors to zero, whatever the scene's clear colour. */
void ae3d_gl_clear_attachment(int index, double r, double g, double b, double a) {
    GLfloat value[4];
    value[0] = (GLfloat)r; value[1] = (GLfloat)g; value[2] = (GLfloat)b; value[3] = (GLfloat)a;
    glClearBufferfv(GL_COLOR, index, value);
}

/* The multisample resolve of one colour attachment other than the first:
   read and draw buffers set to it for the blit and put back after. */
int ae3d_gl_fbo_resolve_attachment(int source, int destination, int width, int height, int index) {
    GLenum status;
    GLenum attachment = GL_COLOR_ATTACHMENT0 + (GLenum)index;
    while (glGetError() != GL_NO_ERROR) { }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);
    glReadBuffer(attachment);
    glDrawBuffer(attachment);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    status = glGetError();
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return (int)status;
}

/* A colour texture already made attached as the framebuffer's colour: the
   temporal pass writes its two history textures in turn through the one
   framebuffer. */
void ae3d_gl_fbo_set_color(int fbo, int texture) {
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)texture, 0);
}

// A shadow map wants different sampling from a post-processing colour buffer:
// point sampling, because the comparison is done per texel with explicit
// offsets, and an edge clamp so a fragment projecting outside the light's view
// reads the cleared far value rather than wrapping.
//
// The DEPTH attachment, and nothing else. The pass used to write gl_FragCoord.z
// into an R32F colour buffer beside a depth buffer holding that same number, so
// every shadow texel was written twice and one of the two was read. With no
// colour attachment at all the driver takes its depth-only path, and a depth
// texture sampled with .r gives back what the colour buffer used to hold, so
// nothing that reads the map changes.
int ae3d_gl_fbo_attach_shadow_map(int fbo, int size) {
    GLuint texture = 0;
    if (size < 1) size = 1;

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    // 32-bit float depth. Eight bits is 256 steps across the whole light-space
    // box, which is coarse enough that a surface shadows itself.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size, size, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Left in the default NONE compare mode: the lit pass does its own
    // comparison with a slope-scaled bias over a 3x3 neighbourhood, so it wants
    // the stored depth rather than a hardware pass/fail.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    // A framebuffer with no colour attachment is incomplete unless it is told
    // there is no colour to draw or read.
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return (int)texture;
}

// The scene pass renders into multisampled attachments and resolves down into
// the single-sample texture the composite samples. Without it the one path that
// runs an effect is also the one path with no antialiasing.
int ae3d_gl_max_samples(void) {
    GLint most = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &most);
    if (most > 4) most = 4;
    return most > 1 ? (int)most : 0;
}

int ae3d_gl_fbo_attach_color_multisample(int fbo, int width, int height, int samples) {
    GLuint rbo = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    return (int)rbo;
}

int ae3d_gl_fbo_attach_depth_multisample(int fbo, int width, int height, int samples) {
    GLuint rbo = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
    return (int)rbo;
}

// Reports what the driver made of the blit rather than leaving a rejected one
// to show up as a black frame. A multisample resolve has more ways to be
// refused than most calls: the two framebuffers have to agree about format and
// size, and drivers differ in how much they will forgive.
int ae3d_gl_fbo_resolve(int source, int destination, int width, int height) {
    GLenum status;
    while (glGetError() != GL_NO_ERROR) { }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);

    status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return (int)status;
    }

    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    status = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return (int)status;
}

/* The scene's depth as drawn so far, copied into a texture the transparent
   pass can read: what a water surface asks to know how much water lies
   between it and the ground. One framebuffer and one depth texture, kept at
   the size of whatever is being drawn into and remade when that changes;
   the copy resolves a multisampled frame to one sample, which for depth
   takes the nearest. Returns the texture, or 0 when the copy failed. */
static GLuint g_scene_depth_fbo, g_scene_depth_texture;
static int g_scene_depth_width, g_scene_depth_height;

int ae3d_gl_scene_depth_capture(void) {
    GLint viewport[4];
    GLint source = 0;
    int width, height;

    glGetIntegerv(GL_VIEWPORT, viewport);
    width = viewport[2];
    height = viewport[3];
    if (width < 1 || height < 1) return 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &source);

    if (!g_scene_depth_fbo) glGenFramebuffers(1, &g_scene_depth_fbo);
    if (!g_scene_depth_texture || width != g_scene_depth_width || height != g_scene_depth_height) {
        if (g_scene_depth_texture) glDeleteTextures(1, &g_scene_depth_texture);
        glGenTextures(1, &g_scene_depth_texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_scene_depth_texture);
        /* The same depth-stencil format the frame is drawn with: a blit of
           depth is refused between formats that differ, and both the window
           and the post-processing target carry 24 bits of depth with 8 of
           stencil. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0,
                     GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, g_scene_depth_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                               g_scene_depth_texture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        g_scene_depth_width = width;
        g_scene_depth_height = height;
    }

    while (glGetError() != GL_NO_ERROR) { }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_scene_depth_fbo);
    glBlitFramebuffer(viewport[0], viewport[1], viewport[0] + width, viewport[1] + height,
                      0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)source);
    if (glGetError() != GL_NO_ERROR) return 0;
    return (int)g_scene_depth_texture;
}

int ae3d_gl_fbo_attach_depth(int fbo, int width, int height) {
    GLuint rbo = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
    return (int)rbo;
}

void ae3d_gl_renderbuffer_delete(int rbo) {
    GLuint id = (GLuint)rbo;
    if (id) glDeleteRenderbuffers(1, &id);
}

// Packed 0xRRGGBB of one framebuffer pixel, for tests that must prove the scene
// was drawn rather than merely cleared.
int ae3d_gl_read_pixel(int x, int y) {
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return ((int)rgba[0] << 16) | ((int)rgba[1] << 8) | (int)rgba[2];
}

int ae3d_gl_fbo_complete(void) {
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

// The frame on screen as RGBA bytes, top row first, in a buffer the caller
// frees: read from whatever framebuffer is bound, which for a windowed
// program between its last draw and its buffer swap is the finished frame.
// GL hands back rows bottom-up and every image format wants them the other
// way, so the copy out walks them backwards.
void *ae3d_gl_read_frame(int width, int height) {
    unsigned char *pixels = NULL;
    unsigned char *flipped = NULL;
    size_t row_bytes;
    int y;

    if (width <= 0 || height <= 0) return NULL;
    row_bytes = (size_t)width * 4;
    pixels = (unsigned char *)malloc(row_bytes * (size_t)height);
    if (!pixels) return NULL;
    flipped = (unsigned char *)malloc(row_bytes * (size_t)height);
    if (!flipped) { free(pixels); return NULL; }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (y = 0; y < height; y++) {
        memcpy(flipped + row_bytes * (size_t)y,
               pixels + row_bytes * (size_t)(height - 1 - y), row_bytes);
    }
    free(pixels);
    return flipped;
}

/* The frame's light, for the exposure that follows it (#378).
 *
 * What was drawn, measured: the finished frame copied at its own size into a
 * texture (a blit at the same size is also what resolves a multisampled
 * framebuffer, which a scaling blit may not read), its mip chain built, and
 * the first level no wider than 320 texels read back -- an average of every
 * pixel under each texel, fine enough that a lamp or a lit face covering a
 * few dozen pixels still fills a texel. The read goes into one of three pixel buffers and
 * the one filled two frames ago is mapped, so the CPU never waits on the
 * frame it just asked for. The texels are the tone-mapped, gamma-encoded
 * frame; `out` gets their mean linear luminance and the share of them near
 * white (past 0.8 linear), which is what an exposure that follows the frame
 * steers by. A source of -1
 * reads the framebuffer the frame was drawn into. Returns 1 when `out` holds
 * a measurement, 0 until the first one is back. */
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#define AE3D_METER_PACKS 3

static struct {
    GLuint texture, framebuffer, level_framebuffer;
    GLuint pack[AE3D_METER_PACKS];
    int width, height, level, level_width, level_height;
    int index, filled;
} g_meter;

static void ae3d_gl_meter_free(void) {
    if (g_meter.texture) glDeleteTextures(1, &g_meter.texture);
    if (g_meter.framebuffer) glDeleteFramebuffers(1, &g_meter.framebuffer);
    if (g_meter.level_framebuffer) glDeleteFramebuffers(1, &g_meter.level_framebuffer);
    if (g_meter.pack[0]) glDeleteBuffers(AE3D_METER_PACKS, g_meter.pack);
    memset(&g_meter, 0, sizeof(g_meter));
}

static int ae3d_gl_meter_make(int width, int height) {
    int i, level = 0, w = width, h = height;
    ae3d_gl_meter_free();
    while (w > 320) { w = w / 2 > 0 ? w / 2 : 1; h = h / 2 > 0 ? h / 2 : 1; level++; }
    glGenTextures(1, &g_meter.texture);
    glBindTexture(GL_TEXTURE_2D, g_meter.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &g_meter.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_meter.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_meter.texture, 0);
    glGenFramebuffers(1, &g_meter.level_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_meter.level_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_meter.texture, level);
    glGenBuffers(AE3D_METER_PACKS, g_meter.pack);
    for (i = 0; i < AE3D_METER_PACKS; i++) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, g_meter.pack[i]);
        glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)w * h * 4, NULL, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g_meter.width = width;
    g_meter.height = height;
    g_meter.level = level;
    g_meter.level_width = w;
    g_meter.level_height = h;
    return 1;
}

/* sRGB-encoded byte to linear light, from a table made once: the meter
   reads tens of thousands of texels a frame. */
static double ae3d_meter_linear(unsigned char c) {
    static double table[256];
    static int made;
    if (!made) {
        int i;
        for (i = 0; i < 256; i++) {
            double v = i / 255.0;
            table[i] = v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
        }
        made = 1;
    }
    return table[c];
}

int ae3d_gl_meter(int source_framebuffer, int width, int height, double *out) {
    GLint previous_read = 0, previous_draw = 0;
    int ready, x, count, got = 0;
    const unsigned char *mapped;
    if (width < 1 || height < 1 || !out) return 0;
    if (width != g_meter.width || height != g_meter.height || !g_meter.texture) {
        if (!ae3d_gl_meter_make(width, height)) return 0;
    }
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
    /* -1: whatever the frame was drawn into, the window or a target. */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source_framebuffer < 0 ? (GLuint)previous_draw : (GLuint)source_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_meter.framebuffer);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, g_meter.texture);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_meter.level_framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, g_meter.pack[g_meter.index]);
    glReadPixels(0, 0, g_meter.level_width, g_meter.level_height, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g_meter.filled++;
    /* The one read two frames ago, when there has been one. */
    if (g_meter.filled >= AE3D_METER_PACKS) {
        ready = (g_meter.index + 1) % AE3D_METER_PACKS;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, g_meter.pack[ready]);
        mapped = (const unsigned char *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (mapped) {
            double sum = 0.0;
            int white = 0;
            count = g_meter.level_width * g_meter.level_height;
            for (x = 0; x < count; x++) {
                const unsigned char *p = mapped + (size_t)x * 4;
                double luma = 0.2126 * ae3d_meter_linear(p[0]) + 0.7152 * ae3d_meter_linear(p[1]) +
                              0.0722 * ae3d_meter_linear(p[2]);
                sum += luma;
                if (luma > 0.8) white++;
            }
            out[0] = sum / (double)count;
            out[1] = (double)white / (double)count;
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            got = 1;
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    g_meter.index = (g_meter.index + 1) % AE3D_METER_PACKS;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous_draw);
    return got;
}

void ae3d_gl_meter_release(void) { ae3d_gl_meter_free(); }
