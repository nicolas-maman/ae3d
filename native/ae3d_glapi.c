#include "ae3d_glapi.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define AE3D_GL_DEF(ret, name, args) ae3d_pfn_##name ae3d_##name;
AE3D_GL_FUNCS(AE3D_GL_DEF)
#undef AE3D_GL_DEF

int ae3d_glapi_load(void) {
    int missing = 0;
#define AE3D_GL_LOAD(ret, name, args) \
    ae3d_##name = (ae3d_pfn_##name)glfwGetProcAddress(#name); \
    if (!ae3d_##name) missing++;
    AE3D_GL_FUNCS(AE3D_GL_LOAD)
#undef AE3D_GL_LOAD
    return missing;
}
