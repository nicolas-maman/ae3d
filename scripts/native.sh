# The engine's C, built once into a library that every program and every
# script shares.
#
# It is shared rather than a pile of objects linked into each program because a
# script is a library the host opens while it is running. On macOS and Linux a
# script can leave the engine's calls undefined and have the host resolve them;
# on Windows a DLL cannot, and the link fails on every native call the engine
# code it imported makes. Linking the same library from both sides is what the
# platform offers, and doing it on all three means one copy of the GL loader's
# state at run time rather than one per script.
#
# Sourced by build.sh, editor/build_editor.sh and scripts/build_script.sh.

ae3d_native_suffix() {
    case "$(uname -s)" in
        Darwin) echo ".dylib" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT) echo ".dll" ;;
        *) echo ".so" ;;
    esac
}

# Position-independent code is what a shared library is made of, and it is the
# default on macOS and on Windows. Naming it there is not harmless: MinGW warns
# that the flag is ignored, and ci.sh reads a warning in a build log as a
# failure.
ae3d_native_pic_flag() {
    case "$(uname -s)" in
        Darwin|MINGW*|MSYS*|CYGWIN*|Windows_NT) echo "" ;;
        *) echo "-fPIC" ;;
    esac
}

# Where a program or a script says the library will be, relative to itself. The
# programs sit beside it in build/; a script sits one directory down, in
# build/scripts/. Windows has no equivalent and needs none: the loader finds a
# DLL beside the executable that asked for it, and a script opened by a running
# host binds to the copy that host already has open.
ae3d_native_rpath() {
    case "$(uname -s)" in
        Darwin) echo "-Wl,-rpath,@loader_path/$1" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT) echo "" ;;
        *) echo "-Wl,-rpath,\$ORIGIN/$1" ;;
    esac
}

# What to put on a link line to use it. $1 is the path from the linked file back
# to build/, empty for a program.
ae3d_native_link_flags() {
    printf '%s' "-Lbuild -lae3d_native $(ae3d_native_rpath "${1:-.}")"
}

# Where GLFW is: the named flags first, then pkg-config, then a bare -lglfw.
# A Windows checkout outside MSYS2 has no pkg-config and a hand-built GLFW in
# a prefix of its own, and the fallback -- a bare -lglfw with no include path
# -- cannot find it; naming the flags is then the only way in. Sets
# GLFW_CFLAGS and GLFW_LIBS. Every program links GLFW, not only the engine's
# library: the window and input layer (ae3d.platform) calls GLFW from Aether,
# and a Windows DLL cannot lend its imports to the program that loads it.
ae3d_glfw_flags() {
    if [ -n "${GLFW_CFLAGS:-}" ] || [ -n "${GLFW_LIBS:-}" ]; then
        GLFW_CFLAGS="${GLFW_CFLAGS:-}"
        GLFW_LIBS="${GLFW_LIBS:-}"
    elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists glfw3; then
        GLFW_CFLAGS="$(pkg-config --cflags glfw3)"
        GLFW_LIBS="$(pkg-config --libs glfw3)"
    else
        GLFW_CFLAGS=""
        GLFW_LIBS="-lglfw"
    fi
}
# The Vulkan headers. GLFW is included with GLFW_INCLUDE_VULKAN, so vulkan.h
# has to be found even though nothing links against the loader (it is opened
# at run time). VULKAN_CFLAGS in the environment wins, as GLFW's flags do;
# then pkg-config, Homebrew's prefix, and the LunarG SDK, which spells the
# directory Include on Windows and include everywhere else and arrives there
# as a Windows path.
ae3d_vulkan_flags() {
    if [ -n "${VULKAN_CFLAGS:-}" ]; then
        return
    fi
    VULKAN_CFLAGS=""
    if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
        VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
    elif [ -d /opt/homebrew/include/vulkan ]; then
        VULKAN_CFLAGS="-I/opt/homebrew/include"
    elif [ -n "${VULKAN_SDK:-}" ]; then
        ae3d_vk_sdk="$VULKAN_SDK"
        if command -v cygpath >/dev/null 2>&1; then ae3d_vk_sdk="$(cygpath -u "$VULKAN_SDK")"; fi
        for ae3d_vk_inc in "$ae3d_vk_sdk/include" "$ae3d_vk_sdk/Include"; do
            if [ -d "$ae3d_vk_inc/vulkan" ]; then
                VULKAN_CFLAGS="-I$ae3d_vk_inc"
                break
            fi
        done
        unset ae3d_vk_sdk ae3d_vk_inc
    fi
}

ae3d_native_library() {
    printf '%s' "build/libae3d_native$(ae3d_native_suffix)"
}

# Link the objects the caller has just compiled into that library. The engine's
# own dependencies are named here rather than taken from the caller: the
# editor's link line carries aether-ui's libraries, which are nothing to do
# with this, and on Windows a DLL has to resolve everything it calls.
#
#   ae3d_native_build <cc> <object directory> <cflags> <extra link libraries>
ae3d_native_build() {
    ae3d_cc="$1"
    ae3d_obj_dir="$2"
    ae3d_cflags="$3"
    ae3d_extra_libs="${4:-}"
    ae3d_lib="$(ae3d_native_library)"

    ae3d_stale=0
    [ -f "$ae3d_lib" ] || ae3d_stale=1
    for ae3d_obj in "$ae3d_obj_dir"/*.o; do
        [ -e "$ae3d_obj" ] || continue
        [ "$ae3d_obj" -nt "$ae3d_lib" ] && ae3d_stale=1
    done
    [ "$ae3d_stale" = 0 ] && return 0

    ae3d_soname=""
    # Names in a crash backtrace: -rdynamic puts the library's symbols in the
    # dynamic table so backtrace_symbols_fd can name the frame that fell over,
    # rather than printing a bare address. ELF only; the linker on Windows and
    # macOS does not take it.
    ae3d_backtrace=""
    case "$(uname -s)" in
        Darwin) ae3d_soname="-Wl,-install_name,@rpath/$(basename "$ae3d_lib")" ;;
        MINGW*|MSYS*|CYGWIN*|Windows_NT)
            # The import library is what a program links against; the DLL
            # itself is only ever loaded.
            ae3d_soname="-Wl,--out-implib,$ae3d_lib.a" ;;
        *) ae3d_soname="-Wl,-soname,$(basename "$ae3d_lib")"
           ae3d_backtrace="-rdynamic" ;;
    esac

    # shellcheck disable=SC2086
    "$ae3d_cc" -shared $ae3d_cflags $ae3d_backtrace "$ae3d_obj_dir"/*.o $ae3d_soname \
        $ae3d_extra_libs $(ae3d_platform_libs "$(uname -s)") -o "$ae3d_lib"
}

# DLSS through NVIDIA Streamline: the C++ shim (native/dlss/streamline.cpp) when
# AE3D_STREAMLINE_ROOT names the SDK, the stub (native/dlss/stub.c),
# which says DLSS was not built in, otherwise. Whichever is built, the
# other's object is dropped from the object directory so the library links
# one of them.
#
#   ae3d_dlss_source <object directory>
ae3d_dlss_source() {
    if [ -n "${AE3D_STREAMLINE_ROOT:-}" ] && [ -f "$AE3D_STREAMLINE_ROOT/include/sl.h" ]; then
        rm -f "$1/stub.o"
        printf '%s' "native/dlss/streamline.cpp"
    else
        rm -f "$1/streamline.o"
        printf '%s' "native/dlss/stub.c"
    fi
}

# The engine's native sources: every file the native library is built from,
# the DLSS shim or its stub by what the machine has, and aephysics's one C
# file (the threads' helpers and the contact solver's vector lanes). One list
# for build.sh and editor/build_editor.sh alike: two copies of it drifted, and
# a file removed from one was still compiled by the other.
#
#   ae3d_native_sources <object directory> <aephysics root>
ae3d_native_sources() {
    list="native/gpu/capture.c native/gpu/opengl_api.c native/gpu/opengl.c native/gpu/offscreen.c native/gpu/vulkan.c native/gpu/jobs.c native/gpu/stores.c"
    list="$list native/platform/crash.c native/image/image.c"
    list="$list $(ae3d_dlss_source "$1") $2/aephysics/native/aephysics_native.c"
    if [ "$(uname -s)" = "Darwin" ]; then
        list="$list native/platform/metal_surface.m"
    fi
    printf '%s' "$list"
}

# The compiler and the flags a native source takes: C++ for the shim, with
# the SDK's headers and without the runtime the engine's library does not
# link (no exceptions, no RTTI, nothing from the standard library).
#
#   ae3d_native_compiler <cc> <source>
ae3d_native_compiler() {
    case "$2" in
        *.cpp) printf '%s' "${CXX:-g++}" ;;
        *) printf '%s' "$1" ;;
    esac
}
ae3d_native_extra_flags() {
    case "$1" in
        *.cpp) printf '%s' "-std=c++17 -fno-exceptions -fno-rtti -Wno-deprecated-declarations -I$AE3D_STREAMLINE_ROOT/include" ;;
        *.m) printf '%s' "-fobjc-arc" ;;
        *) printf '%s' "" ;;
    esac
}
