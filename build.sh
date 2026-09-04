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

CC="${CC:-cc}"
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
AETHER_INCLUDES="$(printf '%s\n' $AETHER_CFLAGS | grep -E '^-I' | tr '\n' ' ')"

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists glfw3; then
    GLFW_CFLAGS="$(pkg-config --cflags glfw3)"
    GLFW_LIBS="$(pkg-config --libs glfw3)"
else
    GLFW_CFLAGS=""
    GLFW_LIBS="-lglfw"
fi

VULKAN_CFLAGS=""
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
    VULKAN_CFLAGS="$(pkg-config --cflags vulkan)"
elif [ -d /opt/homebrew/include/vulkan ]; then
    VULKAN_CFLAGS="-I/opt/homebrew/include"
elif [ -n "${VULKAN_SDK:-}" ]; then
    VULKAN_CFLAGS="-I$VULKAN_SDK/include"
fi

case "$(uname -s)" in
    Darwin) PLATFORM_LIBS="-framework Cocoa -framework IOKit -framework CoreVideo -framework QuartzCore -framework Metal -framework OpenGL" ;;
    Linux)  PLATFORM_LIBS="-ldl -lm -lpthread" ;;
    *)      PLATFORM_LIBS="-lm" ;;
esac

NATIVE_SOURCES="native/ae3d_glapi.c native/ae3d_platform.c native/ae3d_mesh.c native/ae3d_meshfile.c native/ae3d_image.c native/ae3d_gl.c native/ae3d_offscreen.c native/ae3d_vk.c"
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
        "$CC" -c $CFLAGS $WARN $extra $GLFW_CFLAGS $VULKAN_CFLAGS "$src" -o "$obj"
    fi
done

"$AETHERC" "$SOURCE" "$GEN"
"$CC" $CFLAGS "$GEN" $OBJ_DIR/*.o $AETHER_INCLUDES $AETHER_LIBS $GLFW_LIBS $PLATFORM_LIBS -o "$OUT"

echo "built: $OUT"
