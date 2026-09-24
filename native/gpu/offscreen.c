#include "ae3d.h"
#include "internal.h"

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

    /* glfwInit answers true when it has already run, and the window's
       platform layer (ae3d.platform) calls it again after this terminates. */
    if (!glfwInit()) return NULL;

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
    glfwTerminate();
}

#endif

// The targets -- the framebuffers drawn into, their multisampled twins and
// the pixel buffers read back through -- are ae3d.offscreen's, in Aether
// (#398). What is left here is the context, which is the platform's.
