#!/usr/bin/env bash
# Build the ae3d editor: aether-ui for the chrome, ae3d for the viewport.
#
# aether-ui lives in its own repository. Point AETHER_UI_ROOT at a checkout, or
# leave it and a sibling ../aether-ui is used.
#
#   ./editor/build_editor.sh
#   ./build/ae3d_editor

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

UI_ROOT="${AETHER_UI_ROOT:-$ROOT/../aether-ui}"
SOURCE="${1:-editor/editor.ae}"
NAME="$(basename "${2:-ae3d_editor}")"
OUT="build/$NAME"
GEN="build/$NAME.c"
OBJ_DIR="build/obj"

if [ ! -f "$UI_ROOT/ui/module.ae" ]; then
    cat >&2 <<MSG
ae3d: aether-ui not found at $UI_ROOT

The editor's chrome is built with aether-ui, which lives in its own repository:

    git clone https://github.com/aether-lang-dev/aether-ui.git
    AETHER_UI_ROOT=/path/to/aether-ui ./editor/build_editor.sh

MSG
    exit 1
fi

mkdir -p build "$OBJ_DIR"

CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O2}"
WARN="-Wall -Wextra"

AETHER_CFLAGS="$(ae cflags 2>/dev/null || true)"
AETHER_LIBS="$(ae cflags --libs 2>/dev/null || true)"
if [ -z "$AETHER_CFLAGS" ]; then
    echo "ae3d: 'ae cflags' produced nothing; is the toolchain on PATH?" >&2
    exit 1
fi
# The compile half, whole, exactly as ../build.sh takes it and for the same
# reason: $GEN is Aether's generated C, and -fwrapv is what makes its `int`
# arithmetic wrap the way the language reference says it does. Filtering the
# toolchain flags down to -I drops it.
AETHER_COMPILE_FLAGS="$(ae cflags --cflags 2>/dev/null || true)"
if [ -z "$AETHER_COMPILE_FLAGS" ]; then
    AETHER_COMPILE_FLAGS="$(printf '%s\n' $AETHER_CFLAGS | grep -E '^-I' | tr '\n' ' ')"
fi
case " $AETHER_COMPILE_FLAGS " in
    *" -fwrapv "*) ;;
    *) AETHER_COMPILE_FLAGS="$AETHER_COMPILE_FLAGS -fwrapv" ;;
esac

# Same precedence as build.sh: named flags win, then pkg-config, then a bare
# -lglfw. A Windows checkout outside MSYS2 has no pkg-config and a prefix of
# its own, and the two scripts disagreeing about how to find GLFW is how one of
# them ends up unbuildable.
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

if [ -n "${ZLIB_CFLAGS:-}" ] || [ -n "${ZLIB_LIBS:-}" ]; then
    ZLIB_CFLAGS="${ZLIB_CFLAGS:-}"
    ZLIB_LIBS="${ZLIB_LIBS:-}"
elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists zlib; then
    ZLIB_CFLAGS="$(pkg-config --cflags zlib)"
    ZLIB_LIBS="$(pkg-config --libs zlib)"
else
    ZLIB_CFLAGS=""
    ZLIB_LIBS="-lz"
fi

VULKAN_CFLAGS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
    VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
elif [ -d /opt/homebrew/include/vulkan ]; then
    VULKAN_CFLAGS="-I/opt/homebrew/include"
elif [ -n "${VULKAN_SDK:-}" ]; then
    # As build.sh does. GLFW is included with GLFW_INCLUDE_VULKAN, so
    # vulkan.h has to be found even though nothing links against the loader,
    # and the LunarG SDK on Windows spells the directory Include.
    for ae3d_vk_inc in "${VULKAN_SDK}/include" "${VULKAN_SDK}/Include"; do
        if [ -d "$ae3d_vk_inc" ]; then
            VULKAN_CFLAGS="-I$ae3d_vk_inc"
            break
        fi
    done
fi

. "$ROOT/scripts/platform.sh"
. "$ROOT/scripts/native.sh"
PIC="$(ae3d_native_pic_flag)"

OS="$(uname -s)"
case "$OS" in
    Darwin)
        UI_SOURCES="$UI_ROOT/backend/aether_ui_macos.m $UI_ROOT/backend/aether_ui_test_server.c $UI_ROOT/backend/aether_ui_system_extras.c"
        UI_FLAGS="-fobjc-arc"
        PLATFORM_LIBS="-framework AppKit -framework Foundation -framework QuartzCore -framework CoreText -framework ImageIO -framework Cocoa -framework IOKit -framework CoreVideo -framework Metal -framework OpenGL"
        NATIVE_EXTRA="native/ae3d_vk_surface.m"
        ;;
    Linux|FreeBSD)
        if ! pkg-config --exists gtk4 2>/dev/null; then
            echo "ae3d: GTK4 development libraries not found; aether-ui needs them on $OS" >&2
            exit 1
        fi
        UI_SOURCES="$UI_ROOT/backend/aether_ui_gtk4.c $UI_ROOT/backend/aether_ui_sni.c $UI_ROOT/backend/aether_ui_test_server.c $UI_ROOT/backend/aether_ui_system_extras.c"
        # The toolkit's backend calls GTK 4.10's deprecated entry points
        # (gtk_css_provider_load_from_data, the message dialog), which are
        # the toolkit's to move off, not the editor's; ci.sh fails this
        # build on any warning, and those are not warnings about the editor.
        # epoxy by name: the toolkit's GPU view calls GL from the GTK4
        # backend, and gtk4.pc names neither epoxy's headers nor its library.
        UI_FLAGS="$(pkg-config --cflags gtk4) $(pkg-config --cflags epoxy 2>/dev/null) -Wno-deprecated-declarations"
        PLATFORM_LIBS="$(pkg-config --libs gtk4) $(pkg-config --libs epoxy 2>/dev/null) -ldl -lm -lpthread"
        NATIVE_EXTRA=""
        ;;
    MINGW*|MSYS*|CYGWIN*|Windows_NT)
        # aether-ui has had a Win32 backend for some time; this script did not
        # know about it, so the editor was unbuildable on Windows for want of a
        # case in a shell script rather than for want of any code.
        #
        # The library list is aether-ui's own (its build.sh, Windows branch),
        # not a guess: MinGW does not honour #pragma comment(lib), so the win32
        # backend documents what it needs at the top of the file and every
        # caller has to name it.
        UI_SOURCES="$UI_ROOT/backend/aether_ui_win32.c $UI_ROOT/backend/aether_ui_test_server.c $UI_ROOT/backend/aether_ui_system_extras.c"
        UI_FLAGS=""
        PLATFORM_LIBS="-luser32 -lgdi32 -lgdiplus -lmsimg32 -lcomctl32 -lcomdlg32 \
-lshell32 -lole32 -loleaut32 -luuid -loleacc -ldwmapi -luxtheme \
-lopengl32 -lws2_32 -lbcrypt -lm"
        NATIVE_EXTRA=""
        ;;
    *)
        echo "ae3d: the editor has no build recipe for $OS yet" >&2
        exit 1
        ;;
esac

NATIVE_SOURCES="native/ae3d_agent.c native/ae3d_script.c native/ae3d_capture.c native/ae3d_png.c native/ae3d_glapi.c native/ae3d_platform.c native/ae3d_mesh.c native/ae3d_skin.c native/ae3d_meshfile.c native/ae3d_image.c native/ae3d_gl.c native/ae3d_offscreen.c native/ae3d_vk.c native/ae3d_cloudnoise.c native/ae3d_blob.c native/ae3d_weather.c native/ae3d_nav.c native/ae3d_jobs.c $(ae3d_dlss_source "$OBJ_DIR") $NATIVE_EXTRA"

# Every header, not a list of three: the generated ones carry the shaders and
# the uniform offsets, so leaving them out linked the previous shaders.
newest_header=""
for header in native/*.h; do
    if [ -z "$newest_header" ] || [ "$header" -nt "$newest_header" ]; then
        newest_header="$header"
    fi
done

for src in $NATIVE_SOURCES; do
    base="$(basename "$src")"
    obj="$OBJ_DIR/${base%.*}.o"
    extra="$(ae3d_native_extra_flags "$src")"
    compiler="$(ae3d_native_compiler "$CC" "$src")"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$newest_header" -nt "$obj" ]; then
        "$compiler" -c $CFLAGS $WARN $PIC $extra $GLFW_CFLAGS $ZLIB_CFLAGS $VULKAN_CFLAGS "$src" -o "$obj"
    fi
done

ae3d_native_build "$CC" "$OBJ_DIR" "$CFLAGS" "$GLFW_LIBS $ZLIB_LIBS"

# Both module trees on the search path: ae3d.* out of src/, ui and vg.* out of
# the aether-ui checkout.
export AETHER_LIB_DIR="$ROOT/src:$UI_ROOT"
aetherc "$SOURCE" "$GEN"

# GLFW and zlib belong to the engine, which is a library of its own now and
# names them on its own link line. PLATFORM_LIBS here is aether-ui's.
"$CC" $CFLAGS $UI_FLAGS "$GEN" $UI_SOURCES $(ae3d_native_link_flags) \
    $AETHER_COMPILE_FLAGS $AETHER_LIBS $PLATFORM_LIBS \
    -o "$OUT"

echo "built: $OUT"
