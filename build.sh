#!/usr/bin/env bash
# Build an ae3d program.
#
#   ./build.sh examples/triangle.ae            -> build/triangle
#   ./build.sh examples/triangle.ae demo       -> build/demo
#   ./build.sh --natives                       -> the engine library only
#
# --natives builds the C half and stops. A script links the same engine library
# its host links, and it cannot be the first thing built if that library is not
# there yet.
#
# Module resolution is CWD-relative (src/ae3d/<module>/module.ae), so the
# compiler always runs from the repository root regardless of where the caller
# invoked this script from.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

NATIVES_ONLY=0
if [ "${1:-}" = "--natives" ]; then
    NATIVES_ONLY=1
    shift
    set -- "build.sh" ""
fi

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
# The natives include the shared headers at native/ by name from their folders.
NATIVE_INCLUDE="-Inative"

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

# GLFW_CFLAGS and GLFW_LIBS in the environment win, the convention every
# autotools build follows. pkg-config is the right answer where there is one,
# but a Windows checkout outside MSYS2 has no pkg-config and a hand-built GLFW
# in a prefix of its own, and the fallback below -- a bare -lglfw with no
# include path -- cannot find it. Naming the flags is then the only way in, and
# not having one meant the build could not be done at all rather than done
# awkwardly.
. "$ROOT/scripts/native.sh"
ae3d_glfw_flags
ae3d_vulkan_flags

. "$ROOT/scripts/platform.sh"
PLATFORM_LIBS="$(ae3d_platform_libs "$(uname -s)")"
PIC="$(ae3d_native_pic_flag)"

# The physics engine, aephysics, is a git submodule under deps/: Aether
# modules the compiler finds through AETHER_LIB_DIR below, plus its one C
# file (the threads' helpers Aether has not, and the contact solver's
# vector lanes), which is part of the engine's native library like any of
# ours. An empty deps/aephysics means the submodule was not fetched; say
# so rather than fail on a missing import.
AEPHYSICS="$ROOT/deps/aephysics"
if [ ! -f "$AEPHYSICS/aephysics/native/aephysics_native.c" ]; then
    echo "ae3d: deps/aephysics is empty; run: git submodule update --init" >&2
    exit 1
fi
NATIVE_SOURCES="$(ae3d_native_sources "$OBJ_DIR" "$AEPHYSICS")"

# The Vulkan shaders are generated from the GLSL in src/ae3d/shaders and
# compiled into the native library; an edit to the GLSL without the generator
# run after it leaves Vulkan on the previous shaders, which then fail parity
# in ways that look like real bugs. Said here, once, at every build, since
# CI's --check only says so after the push.
if [ -f native/gpu/vulkan_shaders.h ] && [ src/ae3d/shaders/module.ae -nt native/gpu/vulkan_shaders.h ]; then
    echo "build: src/ae3d/shaders/module.ae is newer than the generated Vulkan shaders; run ./build.sh tools/generate_shaders.ae && ./build/generate_shaders" >&2
fi

# Every header, not a list of three. The generated ones carry the shaders and
# the uniform offsets, so leaving them out meant regenerating the shaders and
# linking the previous ones, with nothing to say so.
newest_header=""
for header in native/*.h native/*/*.h; do
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
        "$compiler" -c $CFLAGS $WARN $PIC $NATIVE_INCLUDE $extra $GLFW_CFLAGS $VULKAN_CFLAGS "$src" -o "$obj"
    fi
done

# The library links every object in the directory, so one left by a source
# that has since been renamed or removed would be linked twice or as a
# ghost: objects without a source go.
for obj in "$OBJ_DIR"/*.o; do
    [ -e "$obj" ] || continue
    keep=0
    for src in $NATIVE_SOURCES; do
        base="$(basename "$src")"
        [ "$obj" = "$OBJ_DIR/${base%.*}.o" ] && keep=1
    done
    [ "$keep" = 1 ] || rm -f "$obj"
done

ae3d_native_build "$CC" "$OBJ_DIR" "$CFLAGS" "$GLFW_LIBS"

if [ "$NATIVES_ONLY" = 1 ]; then
    echo "built: $(ae3d_native_library)"
    exit 0
fi

# Two module trees. ae3d.* is the engine, under src/. examples/lib/ is shared
# code belonging to the examples themselves -- a black hole renderer is a tech
# demo, not an engine feature, and putting it under src/ would have told everyone
# who looked otherwise. It is factored out of the example rather than left inside
# it because three callers want the same renderer: the example draws it, the
# benchmark times it, and the test checks it against general relativity.
# Three module trees: ae3d.* under src/, the examples' shared code under
# examples/lib/, and aephysics.* in its submodule.
export AETHER_LIB_DIR="$ROOT/src:$ROOT/examples/lib:$AEPHYSICS"
"$AETHERC" "$SOURCE" "$GEN"
# zlib is the engine's, and the engine is a library of its own that names it
# on its own link line. Naming it again here is not harmless: where the Aether
# toolchain is built against zlib its --libs already carries -lz, Apple's ld
# warns about a duplicate library, and ci.sh reads a warning in a build log as
# a failure. GLFW is named: the program's own Aether calls it (ae3d.platform).
"$CC" $CFLAGS "$GEN" $(ae3d_native_link_flags) $GLFW_LIBS $AETHER_COMPILE_FLAGS $AETHER_LIBS $PLATFORM_LIBS -o "$OUT"

# MinGW gcc appends .exe to an output name that has no extension, so the file
# is not at the path this asked for. Name the one that exists.
if [ ! -f "$OUT" ] && [ -f "$OUT.exe" ]; then
    OUT="$OUT.exe"
fi

echo "built: $OUT"
