/* dladdr, below, is a GNU extension on glibc. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

#include "ae3d.h"

#include <stddef.h>

/* dlfcn.h is POSIX; Windows has no such header, so an unconditional include
 * ended the build before anything else could be tried:
 *
 *   native/ae3d_glapi.c:3:10: fatal error: dlfcn.h: No such file or directory
 *
 * Nothing is lost by dropping it there. The process-first lookup below exists
 * for macOS, where the framework exports every entry point; on Windows the
 * driver hands out 3.x entry points through the windowing system's resolver
 * only, which is exactly the glfwGetProcAddress path already taken when dlsym
 * finds nothing. */
#if !defined(_WIN32)
#  include <dlfcn.h>
#endif

#if !defined(__APPLE__)
#  define GLFW_INCLUDE_NONE
#  include <GLFW/glfw3.h>
#endif

// An OpenGL entry point by name, for ae3d.glapi, which every GL call ae3d
// makes goes through (#398). Resolved from the process first. That covers
// every context on macOS, where the framework exports them all, and it means
// the offscreen path needs no window toolkit to load GL. Elsewhere the driver
// hands out 3.x and later entry points only through the windowing system's
// own resolver.
//
// ae3d.glapi keeps each pointer for the life of the process, and on Linux
// every one of them is in the library GLFW opened, which glfwTerminate
// closes: an offscreen context destroyed, or an engine shut down, and the
// next context called through them into unmapped memory. The library a
// pointer lives in is held open for as long as the pointer is kept.
#if !defined(_WIN32) && !defined(__APPLE__)
static void ae3d_gl_hold(void *symbol) {
    static void *held;
    Dl_info info;
    if (!dladdr(symbol, &info) || !info.dli_fname || info.dli_fbase == held) return;
    if (dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD)) held = info.dli_fbase;
}
#endif

void *ae3d_gl_proc(const char *name) {
    void *symbol = NULL;
#if !defined(_WIN32)
    symbol = dlsym(RTLD_DEFAULT, name);
#endif
#if !defined(__APPLE__)
    if (!symbol) {
        symbol = (void *)glfwGetProcAddress(name);
#  if !defined(_WIN32)
        if (symbol) ae3d_gl_hold(symbol);
#  endif
    }
#endif
    return symbol;
}
