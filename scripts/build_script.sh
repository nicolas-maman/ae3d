#!/usr/bin/env bash
# Compile one Aether script into a shared library the editor can attach.
#
#   scripts/build_script.sh resources/scripts/spin.ae [build/scripts]
#
# A script is an ordinary source file with script_start and script_update in
# it. It is compiled the same way anything else is and then linked as a shared
# library whose undefined symbols are resolved out of the host at load time:
# the runtime, and whatever native calls the engine code it imported makes.
#
# CRITICAL: build a script with the same tree that will run it. A script
# carries its own copy of what it imported, so the host and the script agree
# about what a Model is only while both were built from the same sources.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SOURCE="${1:?usage: build_script.sh <script.ae> [output directory]}"
OUT_DIR="${2:-build/scripts}"
NAME="$(basename "$SOURCE" .ae)"

mkdir -p "$OUT_DIR"

case "$(uname -s)" in
    Darwin) SUFFIX=".dylib" ;;
    MINGW*|MSYS*|CYGWIN*) SUFFIX=".dll" ;;
    *) SUFFIX=".so" ;;
esac

GEN="$OUT_DIR/$NAME.c"
LIB="$OUT_DIR/$NAME$SUFFIX"

AETHER_CFLAGS="$(ae cflags --cflags 2>/dev/null || true)"
if [ -z "$AETHER_CFLAGS" ]; then
    echo "ae3d: 'ae cflags' produced nothing; is the toolchain on PATH?" >&2
    exit 1
fi

AETHER_LIB_DIR="$ROOT/src" aetherc "$SOURCE" "$GEN"

CC="${CC:-cc}"
HOST_OBJECTS=""
HOST_LIBS=""
case "$(uname -s)" in
    Darwin)
        # The host resolves what the script leaves undefined. Without this the
        # link fails on every runtime symbol the script's own imports call.
        LINK_FLAGS="-dynamiclib -undefined dynamic_lookup"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        # A DLL has no undefined symbols: the PE loader has nowhere to look
        # them up, so the link has to resolve every one. A script here carries
        # the engine's C and the runtime rather than borrowing its host's, and
        # the objects it links are the ones the host was built from -- which is
        # the same rule as everywhere else, enforced by the linker instead of
        # by the reader.
        LINK_FLAGS="-shared"
        ./build.sh --natives >/dev/null
        HOST_OBJECTS="$ROOT/build/obj/*.o"
        . "$ROOT/scripts/platform.sh"
        HOST_LIBS="$(ae cflags --libs 2>/dev/null || true) $(ae3d_platform_libs "$(uname -s)")"
        if command -v pkg-config >/dev/null 2>&1; then
            pkg-config --exists glfw3 && HOST_LIBS="$HOST_LIBS $(pkg-config --libs glfw3)"
            pkg-config --exists zlib && HOST_LIBS="$HOST_LIBS $(pkg-config --libs zlib)"
        else
            HOST_LIBS="$HOST_LIBS -lglfw3 -lz"
        fi
        ;;
    *)
        LINK_FLAGS="-shared -fPIC"
        ;;
esac

# shellcheck disable=SC2086
$CC -O2 -fwrapv -fPIC $AETHER_CFLAGS -Inative $LINK_FLAGS "$GEN" $HOST_OBJECTS -o "$LIB" $HOST_LIBS

echo "built: $LIB"
