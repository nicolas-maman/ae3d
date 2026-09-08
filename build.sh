#!/usr/bin/env bash
# Build an ae3d program.
#
#   ./build.sh examples/triangle.ae            -> build/triangle
#   ./build.sh examples/triangle.ae demo       -> build/demo
#
# Module resolution is CWD-relative (src/ae3d/<module>/module.ae), so the
# compiler always runs from the repository root regardless of where the caller
# invoked this script from.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

SOURCE="${1:?usage: build.sh <source.ae> [output-name]}"
NAME="$(basename "${2:-$(basename "$SOURCE" .ae)}")"
OUT="build/$NAME"
GEN="build/$NAME.c"
OBJ_DIR="build/obj"

mkdir -p build "$OBJ_DIR"

# `cc` first, then the names a Windows toolchain actually ships. MSYS2's
# mingw64 has a `cc`; WinLibs, which the Aether installer downloads onto a
# Windows box with no compiler, does not, so defaulting to `cc` alone failed
# before reaching the link with a message about the wrong thing.
if [ -n "${CC:-}" ]; then
    :
else
    for candidate in cc gcc clang; do
        if command -v "$candidate" >/dev/null 2>&1; then CC="$candidate"; break; fi
    done
fi
CC="${CC:-cc}"
if ! command -v "$CC" >/dev/null 2>&1; then
    echo "ae3d: no C compiler found (tried \$CC, cc, gcc, clang)" >&2
    exit 1
fi
AETHERC="${AETHERC:-aetherc}"
CFLAGS="${CFLAGS:--O2}"
WARN="-Wall -Wextra"

if ! command -v "$AETHERC" >/dev/null 2>&1; then
    echo "ae3d: '$AETHERC' not found; install the Aether toolchain first" >&2
    exit 1
fi

AETHER_CFLAGS="$(ae cflags 2>/dev/null || true)"
AETHER_LIBS="$(ae cflags --libs 2>/dev/null || true)"
if [ -z "$AETHER_CFLAGS" ]; then
    echo "ae3d: 'ae cflags' produced nothing; is the toolchain on PATH?" >&2
    exit 1
fi
# The compile half, whole. This used to keep only the flags starting with -I,
# and drop the rest on the floor. $GEN is Aether's generated C, and compiling
# it correctly is not only a question of finding headers -- the language's
# semantics are on that command line too. Aether's `int` wraps; C leaves signed
# overflow undefined; `-fwrapv` is what makes the two agree, and without it any
# wrapping generator is miscompiled from -O2 upward.
#
# That is what turned examples/black_hole.ae into a black window on Windows:
# GCC 15.2 folded the particle seeder's LCG down to a three-value cycle, so all
# 200,000 particles were seeded to the same point and the swarm rendered as one
# 1.3-pixel dot. macOS was fine only because Apple Clang does not make the same
# deduction -- the program was wrong on every platform.
#
# `ae cflags --cflags` carries -fwrapv once the toolchain fix (aether#1957) is
# released. Toolchains older than that do not, and ae3d still supports them, so
# name the flag here when the toolchain has not already supplied it.
AETHER_COMPILE_FLAGS="$(ae cflags --cflags 2>/dev/null || true)"
if [ -z "$AETHER_COMPILE_FLAGS" ]; then
    AETHER_COMPILE_FLAGS="$(printf '%s\n' $AETHER_CFLAGS | grep -E '^-I' | tr '\n' ' ')"
fi
case " $AETHER_COMPILE_FLAGS " in
    *" -fwrapv "*) ;;
    *) AETHER_COMPILE_FLAGS="$AETHER_COMPILE_FLAGS -fwrapv" ;;
esac

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists glfw3; then
    GLFW_CFLAGS="$(pkg-config --cflags glfw3)"
    GLFW_LIBS="$(pkg-config --libs glfw3)"
else
    GLFW_CFLAGS=""
    GLFW_LIBS="-lglfw"
fi

# native/ae3d_meshfile.c includes <zlib.h> and calls gzopen/gzread/gzclose, so
# zlib is ours to link and always has been. It was never named here: on Linux
# `ae cflags --libs` happens to carry -lz, because the Aether toolchain there is
# built against zlib, and that transitive flag covered for us. A Windows Aether
# built without zlib emits no -lz, and the link fails on every gz* call:
#
#   ae3d_meshfile.o: undefined reference to `gzclose'
#
# Depending on another project's link line for a library we use directly is the
# actual bug; the platform only decided when it surfaced.
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists zlib; then
    ZLIB_CFLAGS="$(pkg-config --cflags zlib)"
    ZLIB_LIBS="$(pkg-config --libs zlib)"
else
    ZLIB_CFLAGS=""
    ZLIB_LIBS="-lz"
fi
# ...but only once. Where the Aether toolchain is built against zlib its own
# --libs already carries -lz, and naming it twice is not harmless: Apple's ld
# warns `ignoring duplicate libraries: '-lz'`, and ci.sh counts a build warning
# as a failure. Keep the include flags either way -- a duplicate -I is silent,
# and the header still has to be found on the platforms where Aether does not
# supply it.
case " $AETHER_LIBS " in
    *" -lz "*) ZLIB_LIBS="" ;;
esac

VULKAN_CFLAGS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
    VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
elif [ -d /opt/homebrew/include/vulkan ]; then
    VULKAN_CFLAGS="-I/opt/homebrew/include"
elif [ -n "${VULKAN_SDK:-}" ]; then
    VULKAN_CFLAGS="-I$VULKAN_SDK/include"
fi

. "$ROOT/scripts/platform.sh"
PLATFORM_LIBS="$(ae3d_platform_libs "$(uname -s)")"

NATIVE_SOURCES="native/ae3d_glapi.c native/ae3d_platform.c native/ae3d_mesh.c native/ae3d_meshfile.c native/ae3d_image.c native/ae3d_png.c native/ae3d_gl.c native/ae3d_offscreen.c native/ae3d_vk.c"
if [ "$(uname -s)" = "Darwin" ]; then
    NATIVE_SOURCES="$NATIVE_SOURCES native/ae3d_vk_surface.m"
fi

# Every header, not a list of three. The generated ones carry the shaders and
# the uniform offsets, so leaving them out meant regenerating the shaders and
# linking the previous ones, with nothing to say so.
newest_header=""
for header in native/*.h; do
    if [ -z "$newest_header" ] || [ "$header" -nt "$newest_header" ]; then
        newest_header="$header"
    fi
done

for src in $NATIVE_SOURCES; do
    base="$(basename "$src")"
    obj="$OBJ_DIR/${base%.*}.o"
    extra=""
    case "$src" in *.m) extra="-fobjc-arc" ;; esac
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$newest_header" -nt "$obj" ]; then
        "$CC" -c $CFLAGS $WARN $extra $GLFW_CFLAGS $ZLIB_CFLAGS $VULKAN_CFLAGS "$src" -o "$obj"
    fi
done

"$AETHERC" "$SOURCE" "$GEN"
"$CC" $CFLAGS "$GEN" $OBJ_DIR/*.o $AETHER_COMPILE_FLAGS $AETHER_LIBS $GLFW_LIBS $ZLIB_LIBS $PLATFORM_LIBS -o "$OUT"

# MinGW gcc appends .exe to an output name that has no extension, so the file
# is not at the path this asked for. Name the one that exists.
if [ ! -f "$OUT" ] && [ -f "$OUT.exe" ]; then
    OUT="$OUT.exe"
fi

echo "built: $OUT"
