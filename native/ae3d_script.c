// Loading a compiled script and calling into it.
//
// A script is an Aether source file that the editor compiles into a shared
// library and opens here. Assigning one to an object is then a matter of
// remembering which library answers for that object, which is how Unity and
// Godot both work and what gopher3D was reaching for with its script manager.
//
// The same dlopen shim the Vulkan loader uses, for the same reason: the three
// platforms spell it differently and nothing else about this changes.
//
// CRITICAL: a script carries its own copy of whatever it imported, compiled
// from the same sources as the host, and it reaches back into the host for the
// runtime and the engine's native calls. So the struct layouts either match or
// the script writes through a pointer into the wrong fields. That is what the
// stamp is for: the host records what it was built from, the loader refuses a
// library that does not agree, and a stale script is a message rather than
// memory corruption.

#include "ae3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
static void *ae3d_script_dlopen(const char *path) { return (void *)LoadLibraryA(path); }
static void *ae3d_script_dlsym(void *handle, const char *name) {
    return (void *)GetProcAddress((HMODULE)handle, name);
}
static void ae3d_script_dlclose(void *handle) { FreeLibrary((HMODULE)handle); }
static const char *ae3d_script_dlerror(void) { return "the library would not load"; }
#else
#include <dlfcn.h>
static void *ae3d_script_dlopen(const char *path) {
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
}
static void *ae3d_script_dlsym(void *handle, const char *name) {
    return dlsym(handle, name);
}
static void ae3d_script_dlclose(void *handle) { dlclose(handle); }
static const char *ae3d_script_dlerror(void) {
    const char *e = dlerror();
    return e ? e : "the library would not load";
}
#endif

// What a loadable library is called here. The loader knows, and every caller
// that wants to open one would otherwise have to be told.
const char *ae3d_script_suffix(void) {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

typedef void (*ae3d_script_start_fn)(void *model);
typedef void (*ae3d_script_update_fn)(void *model, double delta);

typedef struct {
    void                  *handle;
    ae3d_script_start_fn   start;
    ae3d_script_update_fn  update;
} ae3d_script;

static char g_script_error[512];

const char *ae3d_script_error(void) { return g_script_error; }

static void *ae3d_script_fail(const char *what, const char *detail) {
    snprintf(g_script_error, sizeof(g_script_error), "%s: %s", what, detail);
    return NULL;
}

void *ae3d_script_open(const char *path) {
    g_script_error[0] = '\0';
    if (!path || !*path) return ae3d_script_fail("no script path", "");

    void *handle = ae3d_script_dlopen(path);
    if (!handle) return ae3d_script_fail(path, ae3d_script_dlerror());

    // update is what makes a script a script; start is optional, because a
    // script that only moves something has nothing to set up.
    ae3d_script_update_fn update =
        (ae3d_script_update_fn)ae3d_script_dlsym(handle, "update");
    if (!update) {
        ae3d_script_dlclose(handle);
        return ae3d_script_fail(path, "no update in it");
    }

    ae3d_script *s = (ae3d_script *)calloc(1, sizeof(*s));
    if (!s) {
        ae3d_script_dlclose(handle);
        return ae3d_script_fail(path, "out of memory");
    }
    s->handle = handle;
    s->update = update;
    s->start = (ae3d_script_start_fn)ae3d_script_dlsym(handle, "start");
    return s;
}

int ae3d_script_has_start(void *script) {
    ae3d_script *s = (ae3d_script *)script;
    return (s && s->start) ? 1 : 0;
}

void ae3d_script_start(void *script, void *model) {
    ae3d_script *s = (ae3d_script *)script;
    if (s && s->start && model) s->start(model);
}

void ae3d_script_update(void *script, void *model, double delta) {
    ae3d_script *s = (ae3d_script *)script;
    if (s && s->update && model) s->update(model, delta);
}

void ae3d_script_close(void *script) {
    ae3d_script *s = (ae3d_script *)script;
    if (!s) return;
    if (s->handle) ae3d_script_dlclose(s->handle);
    free(s);
}
