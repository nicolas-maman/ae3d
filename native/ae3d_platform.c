#include "ae3d.h"
#include "ae3d_glapi.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <signal.h>
#  include <unistd.h>
#  if defined(__GLIBC__) || defined(__APPLE__)
#    include <execinfo.h>
#    define AE3D_HAVE_BACKTRACE 1
#  endif
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

static char g_error[512];
static int  g_initialized;

#if defined(AE3D_HAVE_BACKTRACE)
/* A crash on a headless CI runner leaves nothing but "died on signal 11" and
   a core file nobody can open. This prints the native stack to stderr the
   instant it happens, so the log names the frame that fell over. Async-signal
   safe: backtrace and backtrace_symbols_fd are on the allowed list, write is
   the only other call, and the handler re-raises the default so the process
   still dies and the exit status is unchanged. */
static void ae3d_say(const char *text) {
    /* write() is marked warn_unused_result by glibc and the build is -Werror;
       there is nothing useful to do if writing the crash message itself fails,
       so the result is captured and discarded. */
    ssize_t written = write(2, text, strlen(text));
    (void)written;
}

static void ae3d_crash_handler(int sig) {
    void *frames[64];
    int n = backtrace(frames, 64);
    char digit[2];
    digit[0] = (char)('0' + (sig % 10));
    digit[1] = '\n';
    ae3d_say("\nae3d: native crash, signal ");
    { ssize_t w = write(2, digit, 2); (void)w; }
    backtrace_symbols_fd(frames, n, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

/* Installed when the shared library loads, before any entry point runs, so an
   offscreen test that never opens a window is covered too. */
__attribute__((constructor))
static void ae3d_install_crash_handler(void) {
    signal(SIGSEGV, ae3d_crash_handler);
    signal(SIGABRT, ae3d_crash_handler);
    signal(SIGBUS, ae3d_crash_handler);
    signal(SIGFPE, ae3d_crash_handler);
}
#endif

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
                         int api, int msaa, int decorated, int visible,
                         int depth_bits) {
    GLFWwindow *win;
    double *scroll;

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    /* An invisible window still has a framebuffer and still renders; it just
       never reaches the screen or the taskbar, and cannot take focus away from
       whatever the machine is actually being used for. */
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    if (!visible) {
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }
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

    win = glfwCreateWindow(width, height, title ? title : "ae3d", NULL, NULL);
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
// glfwGetTime reads zero until glfwInit runs, so anything that renders without
// opening a window, an offscreen context or a test, would run on a clock that
// never moves. This one does not depend on the window system at all.
double ae3d_time(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, now;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        return 0.0;
    }
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

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
