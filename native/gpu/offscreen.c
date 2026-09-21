#include "ae3d.h"
#include "internal.h"
#include "opengl_api.h"

#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
// Apple's sanctioned macro for using an API the platform has deprecated but
// still ships and still accelerates.
#  define GL_SILENCE_DEPRECATION
#  include <OpenGL/OpenGL.h>
#else
#  define GLFW_INCLUDE_NONE
#  include <GLFW/glfw3.h>
#endif

typedef struct {
    GLuint framebuffer;
    GLuint color;
    GLuint depth;
    GLuint ms_framebuffer;
    GLuint ms_color;
    GLuint ms_depth;
    int    samples;
    int    resolved;
    int width;
    int height;
    // CRITICAL: two buffers, alternated. A caller may still be reading the
    // frame we handed it last time while we fill the next one, and the
    // compositor does exactly that: it borrows the pointer and paints from it
    // on its own thread. Writing back into the buffer it is reading tears the
    // image and puts the two threads on the same cache lines.
    unsigned char *pixels[2];
    int pixel_index;
    int pixel_bytes;
    unsigned char *scratch;
    GLuint pack[2];
    int pack_index;
    int pack_primed;
} ae3d_offscreen;

static void *ae3d_offscreen_read_sync(ae3d_offscreen *target);

// A context with no window at all, so the scene can be drawn on the GPU and
// handed to whatever is compositing it.
//
// On macOS this must NOT go through a window toolkit. Creating a GLFW window,
// even a hidden one, makes GLFW take ownership of NSApplication: it installs its
// own delegate, activation policy and menu bar. A UI toolkit in the same process
// then cannot present its window, and the app shows up in the Dock with nothing
// on screen. CGL creates a context with no surface and no NSApp involvement,
// which is what an offscreen context should need.
#if defined(__APPLE__)

void *ae3d_offscreen_context(int width, int height) {
    CGLPixelFormatAttribute attributes[] = {
        kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
        kCGLPFAAccelerated,
        kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
        kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
        kCGLPFADepthSize, (CGLPixelFormatAttribute)24,
        (CGLPixelFormatAttribute)0
    };
    CGLPixelFormatObj format = NULL;
    CGLContextObj context = NULL;
    GLint formats = 0;

    (void)width;
    (void)height;

    if (CGLChoosePixelFormat(attributes, &format, &formats) != kCGLNoError || !format) return NULL;
    if (CGLCreateContext(format, NULL, &context) != kCGLNoError || !context) {
        CGLDestroyPixelFormat(format);
        return NULL;
    }
    CGLDestroyPixelFormat(format);

    if (CGLSetCurrentContext(context) != kCGLNoError) {
        CGLDestroyContext(context);
        return NULL;
    }
    return context;
}

void ae3d_offscreen_context_destroy(void *context) {
    if (!context) return;
    CGLSetCurrentContext(NULL);
    CGLDestroyContext((CGLContextObj)context);
}

#else

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
                              "ae3d offscreen", NULL, NULL);
    if (!window) return NULL;

    glfwMakeContextCurrent(window);
    return window;
}

void ae3d_offscreen_context_destroy(void *context) {
    if (!context) return;
    glfwDestroyWindow((GLFWwindow *)context);
    ae3d_platform_shutdown();
}

#endif

// How many samples the driver will give a renderbuffer, capped at what the
// Vulkan backend picks, so a scene drawn offscreen through either renderer is
// antialiased the same way.
static int ae3d_offscreen_samples(void) {
    GLint most = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &most);
    if (most > 4) most = 4;
    return most > 1 ? (int)most : 0;
}

static int ae3d_offscreen_attach(ae3d_offscreen *target, int width, int height) {
    GLenum status;

    if (width < 1) width = 1;
    if (height < 1) height = 1;

    if (target->color) glDeleteTextures(1, &target->color);
    if (target->depth) glDeleteRenderbuffers(1, &target->depth);
    if (target->ms_color) glDeleteRenderbuffers(1, &target->ms_color);
    if (target->ms_depth) glDeleteRenderbuffers(1, &target->ms_depth);
    target->color = 0;
    target->depth = 0;
    target->ms_color = 0;
    target->ms_depth = 0;

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

    // The scene is drawn into a multisampled pair and resolved into the texture
    // above before anything reads it. Without this the offscreen viewport is
    // the one surface in the engine with no antialiasing, while the same scene
    // through Vulkan is resolved from 4 samples.
    target->samples = ae3d_offscreen_samples();
    if (target->samples) {
        if (!target->ms_framebuffer) glGenFramebuffers(1, &target->ms_framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, target->ms_framebuffer);

        glGenRenderbuffers(1, &target->ms_color);
        glBindRenderbuffer(GL_RENDERBUFFER, target->ms_color);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, target->samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  target->ms_color);

        glGenRenderbuffers(1, &target->ms_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, target->ms_depth);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, target->samples, GL_DEPTH24_STENCIL8,
                                         width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                  target->ms_depth);

        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteRenderbuffers(1, &target->ms_color);
            glDeleteRenderbuffers(1, &target->ms_depth);
            target->ms_color = 0;
            target->ms_depth = 0;
            target->samples = 0;
        }
    }

    target->resolved = 0;
    target->width = width;
    target->height = height;

    {
        int needed = width * height * 4;
        if (needed > target->pixel_bytes) {
            int i;
            for (i = 0; i < 2; i++) {
                unsigned char *grown =
                    (unsigned char *)realloc(target->pixels[i], (size_t)needed);
                if (!grown) return 0;
                target->pixels[i] = grown;
            }
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
        free(target->pixels[0]);
        free(target->pixels[1]);
        free(target);
        return NULL;
    }
    return target;
}

int ae3d_offscreen_resize(void *handle, int width, int height) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    if (!target) return 0;
    if (target->width == width && target->height == height) return 1;

    // The scratch row and the pixel buffers are sized for the old dimensions,
    // so they are released here rather than read at the wrong width next frame.
    free(target->scratch);
    target->scratch = NULL;
    if (target->pack[0]) {
        glDeleteBuffers(2, target->pack);
        target->pack[0] = 0;
        target->pack[1] = 0;
        target->pack_primed = 0;
    }
    return ae3d_offscreen_attach(target, width, height);
}

void ae3d_offscreen_bind(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    if (!target) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, target->samples ? target->ms_framebuffer
                                                      : target->framebuffer);
    glViewport(0, 0, target->width, target->height);
    target->resolved = 0;
}

// Multisampled attachments cannot be read directly, so the frame is blitted
// down into the single-sample texture the readback and the picking path use.
static void ae3d_offscreen_resolve(ae3d_offscreen *target) {
    if (!target->samples || target->resolved) return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target->ms_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target->framebuffer);
    glBlitFramebuffer(0, 0, target->width, target->height,
                      0, 0, target->width, target->height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    target->resolved = 1;
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

// Reads the frame just drawn, stalling until the GPU has finished it. Callers
// that compare what they rendered need this; a viewport does not.
void *ae3d_offscreen_read(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    if (!target || !target->pixels[0]) return NULL;
    return ae3d_offscreen_read_sync(target);
}

static void *ae3d_offscreen_read_sync(ae3d_offscreen *target) {
    int row_bytes = target->width * 4;
    unsigned char *dst = target->pixels[target->pixel_index];
    int y;

    ae3d_offscreen_resolve(target);
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, target->width, target->height, GL_RGBA, GL_UNSIGNED_BYTE, dst);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    target->pixel_index ^= 1;

    if (!target->scratch) target->scratch = (unsigned char *)malloc((size_t)row_bytes);
    if (!target->scratch) return dst;
    for (y = 0; y < target->height / 2; y++) {
        unsigned char *top = dst + (size_t)y * row_bytes;
        unsigned char *bottom = dst + (size_t)(target->height - 1 - y) * row_bytes;
        memcpy(target->scratch, top, (size_t)row_bytes);
        memcpy(top, bottom, (size_t)row_bytes);
        memcpy(bottom, target->scratch, (size_t)row_bytes);
    }
    return dst;
}

int ae3d_offscreen_byte_size(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    return target ? target->width * target->height * 4 : 0;
}

// Reading straight into client memory stalls until the GPU has finished the
// frame. Two pixel buffers avoid that: the read is issued into one and the one
// filled last frame is mapped, so the CPU never waits on work still in flight.
// The viewport is one frame behind, which is what every editor preview does.
//
// GL reports rows bottom-up while every compositor wants them top-down, so the
// copy out of the mapped buffer walks the rows backwards. That folds the flip
// into a copy that had to happen anyway, instead of a second pass over the
// image with a scratch row allocated per frame.
void *ae3d_offscreen_read_pipelined(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    int row_bytes, y, ready;
    const unsigned char *mapped;
    unsigned char *destination;

    if (!target || !target->pixels[0]) return NULL;
    row_bytes = target->width * 4;

    if (!target->pack[0]) {
        glGenBuffers(2, target->pack);
        if (!target->pack[0] || !target->pack[1]) return ae3d_offscreen_read_sync(target);
        for (y = 0; y < 2; y++) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, target->pack[y]);
            glBufferData(GL_PIXEL_PACK_BUFFER, target->pixel_bytes, NULL, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        target->pack_index = 0;
        target->pack_primed = 0;
    }

    destination = target->pixels[target->pixel_index];

    ae3d_offscreen_resolve(target);
    glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, target->pack[target->pack_index]);
    glReadPixels(0, 0, target->width, target->height, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    ready = target->pack_primed ? (target->pack_index ^ 1) : target->pack_index;
    if (!target->pack_primed) glFinish();

    glBindBuffer(GL_PIXEL_PACK_BUFFER, target->pack[ready]);
    mapped = (const unsigned char *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
    if (mapped) {
        for (y = 0; y < target->height; y++) {
            memcpy(destination + (size_t)y * row_bytes,
                   mapped + (size_t)(target->height - 1 - y) * row_bytes,
                   (size_t)row_bytes);
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    target->pack_index ^= 1;
    target->pack_primed = 1;
    target->pixel_index ^= 1;
    return destination;
}

void ae3d_offscreen_destroy(void *handle) {
    ae3d_offscreen *target = (ae3d_offscreen *)handle;
    if (!target) return;
    if (target->color) glDeleteTextures(1, &target->color);
    if (target->depth) glDeleteRenderbuffers(1, &target->depth);
    if (target->ms_color) glDeleteRenderbuffers(1, &target->ms_color);
    if (target->ms_depth) glDeleteRenderbuffers(1, &target->ms_depth);
    if (target->framebuffer) glDeleteFramebuffers(1, &target->framebuffer);
    if (target->ms_framebuffer) glDeleteFramebuffers(1, &target->ms_framebuffer);
    if (target->pack[0]) glDeleteBuffers(2, target->pack);
    free(target->scratch);
    free(target->pixels[0]);
    free(target->pixels[1]);
    free(target);
}
