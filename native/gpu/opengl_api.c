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
void *ae3d_gl_proc(const char *name) {
    void *symbol = NULL;
#if !defined(_WIN32)
    symbol = dlsym(RTLD_DEFAULT, name);
#endif
#if !defined(__APPLE__)
    if (!symbol) symbol = (void *)glfwGetProcAddress(name);
#endif
    return symbol;
}
