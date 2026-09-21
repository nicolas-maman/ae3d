#include "opengl_api.h"

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

// Entry points are resolved from the process first. That covers every context on
// macOS, where the framework exports them all, and it means the offscreen path
// needs no window toolkit to load GL. Elsewhere the driver hands out 3.x and
// later entry points only through the windowing system's own resolver.
static void *ae3d_gl_symbol(const char *name) {
    void *symbol = NULL;
#if !defined(_WIN32)
    symbol = dlsym(RTLD_DEFAULT, name);
#endif
#if !defined(__APPLE__)
    if (!symbol) symbol = (void *)glfwGetProcAddress(name);
#endif
    return symbol;
}

#define AE3D_GL_DEF(ret, name, args) ae3d_pfn_##name ae3d_##name;
AE3D_GL_FUNCS(AE3D_GL_DEF)
#undef AE3D_GL_DEF

int ae3d_glapi_load(void) {
    int missing = 0;
#define AE3D_GL_LOAD(ret, name, args) \
    ae3d_##name = (ae3d_pfn_##name)ae3d_gl_symbol(#name); \
    if (!ae3d_##name) missing++;
    AE3D_GL_FUNCS(AE3D_GL_LOAD)
#undef AE3D_GL_LOAD
    return missing;
}
