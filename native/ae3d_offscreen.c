#include "ae3d.h"
#include "ae3d_internal.h"
#include "ae3d_glapi.h"

#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

typedef struct {
    GLuint framebuffer;
    GLuint color;
    GLuint depth;
    int width;
    int height;
    unsigned char *pixels;
    int pixel_bytes;
} ae3d_offscreen;

// A context with no visible window. The editor draws the scene here and hands
// the pixels to whatever is compositing them, so the 3D view can live inside a
// toolkit that owns the real window and has no GPU surface of its own.
void *ae3d_offscreen_context(int width, int height) {
    GLFWwindow *window;

    if (!ae3d_platform_init()) return NULL;

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    window = glfwCreateWindow(width > 0 ? width : 1, height > 0 ? height : 1,
                              "aether3d offscreen", NULL, NULL);
    if (!window) return NULL;

    glfwMakeContextCurrent(window);
    return window;
}

void ae3d_offscreen_context_destroy(void *context) {
    if (!context) return;
    glfwDestroyWindow((GLFWwindow *)context);
    ae3d_platform_shutdown();
}

static int ae3d_offscreen_attach(ae3d_offscreen *target, int width, int height) {
    GLenum status;

    if (width < 1) width = 1;
    if (height < 1) height = 1;

    if (target->color) glDeleteTextures(1, &target->color);
    if (target->depth) glDeleteRenderbuffers(1, &target->depth);
    target->color = 0;
    target->depth = 0;

    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);

    glGenTextures(1, &target->color);
    glBindTexture(GL_TEXTURE_2D, target->color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->color, 0);

    glGenRenderbuffers(1, &target->depth);
    glBindRenderbuffer(GL_RENDERBUFFER, target->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, target->depth);

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) return 0;

    target->width = width;
    target->height = height;

    {
        int needed = width * height * 4;
        if (needed > target->pixel_bytes) {
            unsigned char *grown = (unsigned char *)realloc(target->pixels, (size_t)needed);
            if (!grown) return 0;
            target->pixels = grown;
            target->pixel_bytes = needed;
        }
    }
    return 1;
}

void *ae3d_offscreen_create(int width, int height) {
    ae3d_offscreen *target = (ae3d_offscreen *)calloc(1, sizeof(ae3d_offscreen));
    if (!target) return NULL;

    glGenFramebuffers(1, &target->framebuffer);
    if (!ae3d_offscreen_attach(target, width, height)) {
        glDeleteFramebuffers(1, &target->framebuffer);
        free(target->pixels);
        free(target);
        return NULL;
    }
    return target;
}

int ae3d_offscreen_resize(void *handle, int width, int height) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    if (!target) return 0;
    if (target->width == width && target->height == height) return 1;
    return ae3d_offscreen_attach(target, width, height);
}

void ae3d_offscreen_bind(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    if (!target) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glViewport(0, 0, target->width, target->height);
}

void ae3d_offscreen_unbind(void) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int ae3d_offscreen_width(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    return target ? target->width : 0;
}

int ae3d_offscreen_height(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    return target ? target->height : 0;
}

int ae3d_offscreen_byte_size(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    return target ? target->width * target->height * 4 : 0;
}

// GL reports rows bottom-up while every compositor wants them top-down, so the
// readback flips as it copies rather than leaving the caller to do it per frame.
void *ae3d_offscreen_read(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    int row_bytes, y;
    unsigned char *scratch;

    if (!target || !target->pixels) return NULL;
    row_bytes = target->width * 4;

    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadPixels(0, 0, target->width, target->height, GL_RGBA, GL_UNSIGNED_BYTE, target->pixels);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    scratch = (unsigned char *)malloc((size_t)row_bytes);
    if (!scratch) return target->pixels;
    for (y = 0; y < target->height / 2; y++) {
        unsigned char *top = target->pixels + (size_t)y * row_bytes;
        unsigned char *bottom = target->pixels + (size_t)(target->height - 1 - y) * row_bytes;
        memcpy(scratch, top, (size_t)row_bytes);
        memcpy(top, bottom, (size_t)row_bytes);
        memcpy(bottom, scratch, (size_t)row_bytes);
    }
    free(scratch);
    return target->pixels;
}

void ae3d_offscreen_destroy(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    if (!target) return;
    if (target->color) glDeleteTextures(1, &target->color);
    if (target->depth) glDeleteRenderbuffers(1, &target->depth);
    if (target->framebuffer) glDeleteFramebuffers(1, &target->framebuffer);
    free(target->pixels);
    free(target);
}
