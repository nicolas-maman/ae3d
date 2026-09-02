#include "ae3d.h"
#include "ae3d_glapi.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static char g_error[512];
static int  g_initialized;

static void ae3d_error_callback(int code, const char *description) {
    snprintf(g_error, sizeof(g_error), "glfw error %d: %s", code, description ? description : "");
}

static void ae3d_scroll_callback(GLFWwindow *win, double xoffset, double yoffset) {
    double *acc = (double *)glfwGetWindowUserPointer(win);
    (void)xoffset;
    if (acc) *acc += yoffset;
}

int ae3d_platform_init(void) {
    if (g_initialized) return 1;
    glfwSetErrorCallback(ae3d_error_callback);
    if (!glfwInit()) return 0;
    g_initialized = 1;
    return 1;
}

void ae3d_platform_shutdown(void) {
    if (!g_initialized) return;
    glfwTerminate();
    g_initialized = 0;
}

const char *ae3d_platform_error(void) { return g_error; }

void *ae3d_window_create(int width, int height, const char *title,
                         int api, int msaa, int decorated, int depth_bits) {
    GLFWwindow *win;
    double *scroll;

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (depth_bits > 0) glfwWindowHint(GLFW_DEPTH_BITS, depth_bits);
    if (msaa > 0) glfwWindowHint(GLFW_SAMPLES, msaa);

    if (api == AE3D_API_VULKAN) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    }

    win = glfwCreateWindow(width, height, title ? title : "aether3d", NULL, NULL);
    if (!win) return NULL;

    scroll = (double *)calloc(1, sizeof(double));
    glfwSetWindowUserPointer(win, scroll);
    glfwSetScrollCallback(win, ae3d_scroll_callback);

    if (api != AE3D_API_VULKAN) glfwMakeContextCurrent(win);
    return win;
}

void ae3d_window_destroy(void *win) {
    double *scroll;
    if (!win) return;
    scroll = (double *)glfwGetWindowUserPointer((GLFWwindow *)win);
    free(scroll);
    glfwDestroyWindow((GLFWwindow *)win);
}

int  ae3d_window_should_close(void *win) {
    return win ? glfwWindowShouldClose((GLFWwindow *)win) : 1;
}

void ae3d_window_close(void *win) {
    if (win) glfwSetWindowShouldClose((GLFWwindow *)win, GLFW_TRUE);
}

void ae3d_window_swap(void *win) {
    if (win) glfwSwapBuffers((GLFWwindow *)win);
}

void ae3d_window_make_current(void *win) {
    if (win) glfwMakeContextCurrent((GLFWwindow *)win);
}

void ae3d_window_set_vsync(int interval) { glfwSwapInterval(interval); }

int ae3d_window_width(void *win) {
    int w = 0, h = 0;
    if (win) glfwGetWindowSize((GLFWwindow *)win, &w, &h);
    return w;
}

int ae3d_window_height(void *win) {
    int w = 0, h = 0;
    if (win) glfwGetWindowSize((GLFWwindow *)win, &w, &h);
    return h;
}

int ae3d_window_fb_width(void *win) {
    int w = 0, h = 0;
    if (win) glfwGetFramebufferSize((GLFWwindow *)win, &w, &h);
    return w;
}

int ae3d_window_fb_height(void *win) {
    int w = 0, h = 0;
    if (win) glfwGetFramebufferSize((GLFWwindow *)win, &w, &h);
    return h;
}

void ae3d_window_set_pos(void *win, int x, int y) {
    if (win) glfwSetWindowPos((GLFWwindow *)win, x, y);
}

void ae3d_window_center(void *win) {
    GLFWmonitor *monitor;
    const GLFWvidmode *mode;
    int w = 0, h = 0;

    if (!win) return;
    monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    mode = glfwGetVideoMode(monitor);
    if (!mode) return;
    glfwGetWindowSize((GLFWwindow *)win, &w, &h);
    glfwSetWindowPos((GLFWwindow *)win, (mode->width - w) / 2, (mode->height - h) / 2);
}

void ae3d_window_set_title(void *win, const char *title) {
    if (win && title) glfwSetWindowTitle((GLFWwindow *)win, title);
}

void ae3d_window_set_cursor_mode(void *win, int mode) {
    int value = GLFW_CURSOR_NORMAL;
    if (!win) return;
    if (mode == AE3D_CURSOR_HIDDEN) value = GLFW_CURSOR_HIDDEN;
    else if (mode == AE3D_CURSOR_DISABLED) value = GLFW_CURSOR_DISABLED;
    glfwSetInputMode((GLFWwindow *)win, GLFW_CURSOR, value);
}

int ae3d_monitor_width(void) {
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : NULL;
    return mode ? mode->width : 0;
}

int ae3d_monitor_height(void) {
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : NULL;
    return mode ? mode->height : 0;
}

void   ae3d_poll_events(void) { glfwPollEvents(); }
double ae3d_time(void) { return glfwGetTime(); }

int ae3d_key_down(void *win, int key) {
    return win && glfwGetKey((GLFWwindow *)win, key) == GLFW_PRESS;
}

int ae3d_mouse_down(void *win, int button) {
    return win && glfwGetMouseButton((GLFWwindow *)win, button) == GLFW_PRESS;
}

double ae3d_cursor_x(void *win) {
    double x = 0.0, y = 0.0;
    if (win) glfwGetCursorPos((GLFWwindow *)win, &x, &y);
    return x;
}

double ae3d_cursor_y(void *win) {
    double x = 0.0, y = 0.0;
    if (win) glfwGetCursorPos((GLFWwindow *)win, &x, &y);
    return y;
}

double ae3d_scroll_delta(void *win) {
    double *acc, value;
    if (!win) return 0.0;
    acc = (double *)glfwGetWindowUserPointer((GLFWwindow *)win);
    if (!acc) return 0.0;
    value = *acc;
    *acc = 0.0;
    return value;
}
