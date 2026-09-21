# Where a build finds its dependencies, and what the system wants on the link
# line, for each platform ae3d supports.
#
# Sourced by build.sh, ci.sh and editor/build_editor.sh. The three used to carry
# a copy each of the same GLFW and Vulkan probes, which had already drifted:
# ci.sh compiled without the GLFW include path pkg-config would have given it,
# and only build.sh knew about VULKAN_SDK. Windows needs all three to agree, so
# they now ask one place.
#
# Sets:
#   AE3D_OS         macos | linux | windows | whatever uname said
#   AE3D_EXE        the suffix an executable takes here, ".exe" on Windows
#   GLFW_CFLAGS     GLFW_LIBS
#   VULKAN_CFLAGS   headers only; the loader is opened at run time, never linked
#   ZLIB_CFLAGS     ZLIB_LIBS
#   PLATFORM_LIBS   what the platform itself wants linked
#   NATIVE_EXTRA    platform-only sources built alongside the C under native/
#
# and defines ae3d_have_display.

case "$(uname -s)" in
    Darwin)                   AE3D_OS=macos ;;
    Linux)                    AE3D_OS=linux ;;
    MINGW*|MSYS*|CYGWIN*)     AE3D_OS=windows ;;
    *)                        AE3D_OS="$(uname -s)" ;;
esac

AE3D_EXE=""
[ "$AE3D_OS" = windows ] && AE3D_EXE=".exe"

ae3d_pkg_exists() {
    command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$1"
}

# VULKAN_SDK and the like arrive as native Windows paths, which a shell test
# reads as a string with stray backslashes in it. Everything else is already
# what it needs to be.
ae3d_shell_path() {
    if [ "$AE3D_OS" = windows ] && command -v cygpath >/dev/null 2>&1; then
        cygpath -u "$1"
    else
        printf '%s' "$1"
    fi
}

# --- GLFW ---------------------------------------------------------------
#
# pkg-config knows where it is on every platform that has one, which on Windows
# means MSYS2. GLFW_ROOT covers a prefix built by hand, the usual answer for a
# Windows checkout that is not inside MSYS2.
if ae3d_pkg_exists glfw3; then
    GLFW_CFLAGS="$(pkg-config --cflags glfw3)"
    GLFW_LIBS="$(pkg-config --libs glfw3)"
elif [ -n "${GLFW_ROOT:-}" ]; then
    GLFW_ROOT_DIR="$(ae3d_shell_path "$GLFW_ROOT")"
    GLFW_CFLAGS="-I$GLFW_ROOT_DIR/include"
    if [ "$AE3D_OS" = windows ]; then
        GLFW_LIBS="-L$GLFW_ROOT_DIR/lib -lglfw3"
    else
        GLFW_LIBS="-L$GLFW_ROOT_DIR/lib -lglfw"
    fi
elif [ "$AE3D_OS" = windows ]; then
    # libglfw3.a is what every Windows distribution of it is called.
    GLFW_CFLAGS=""
    GLFW_LIBS="-lglfw3"
else
    GLFW_CFLAGS=""
    GLFW_LIBS="-lglfw"
fi

# --- Vulkan headers -----------------------------------------------------
VULKAN_CFLAGS=""
if ae3d_pkg_exists vulkan; then
    VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
elif [ -d /opt/homebrew/include/vulkan ]; then
    VULKAN_CFLAGS="-I/opt/homebrew/include"
elif [ -n "${VULKAN_SDK:-}" ]; then
    # The LunarG SDK spells it Include on Windows and include everywhere else.
    for ae3d_vk_inc in "$(ae3d_shell_path "$VULKAN_SDK")/include" \
                       "$(ae3d_shell_path "$VULKAN_SDK")/Include"; do
        if [ -d "$ae3d_vk_inc/vulkan" ]; then
            VULKAN_CFLAGS="-I$ae3d_vk_inc"
            break
        fi
    done
    unset ae3d_vk_inc
fi

# --- zlib ---------------------------------------------------------------
#
# The compressed mesh format is zlib's. macOS and Linux ship it as part of the
# system and the Aether toolchain's own link flags already carry it, so asking
# again there would only put a second -lz on a line that works; Windows has
# neither, and has to be told.
ZLIB_CFLAGS=""
ZLIB_LIBS=""
if [ "$AE3D_OS" = windows ]; then
    if ae3d_pkg_exists zlib; then
        ZLIB_CFLAGS="$(pkg-config --cflags zlib)"
        ZLIB_LIBS="$(pkg-config --libs zlib)"
    elif [ -n "${ZLIB_ROOT:-}" ]; then
        ZLIB_ROOT_DIR="$(ae3d_shell_path "$ZLIB_ROOT")"
        ZLIB_CFLAGS="-I$ZLIB_ROOT_DIR/include"
        ZLIB_LIBS="-L$ZLIB_ROOT_DIR/lib -lz"
    else
        ZLIB_LIBS="-lz"
    fi
fi

# --- the platform's own libraries ---------------------------------------
NATIVE_EXTRA=""
case "$AE3D_OS" in
    macos)
        PLATFORM_LIBS="-framework Cocoa -framework IOKit -framework CoreVideo -framework QuartzCore -framework Metal -framework OpenGL"
        NATIVE_EXTRA="native/platform/metal_surface.m"
        ;;
    linux)
        PLATFORM_LIBS="-ldl -lm -lpthread"
        ;;
    windows)
        # gdi32 and shell32 are GLFW's, linked statically here; opengl32 is
        # asked for by name at run time rather than imported, but a driver that
        # brings its own is happier finding the system one already resolved.
        PLATFORM_LIBS="-lopengl32 -lgdi32 -lshell32 -luser32 -lkernel32"
        ;;
    *)
        PLATFORM_LIBS="-lm"
        ;;
esac

# A Windows session always has a desktop to open a window on, and macOS has the
# window server whether or not anyone is looking at it. X11 and Wayland are the
# only ones that can be absent.
ae3d_have_display() {
    case "$AE3D_OS" in
        macos|windows) return 0 ;;
        *) [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ] ;;
    esac
}
