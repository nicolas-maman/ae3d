#!/usr/bin/env bash
# Compile one Aether script into a shared library the editor can attach.
#
#   scripts/build_script.sh resources/scripts/spin.ae [build/scripts]
#
# A script is an ordinary source file with start and update in it, Unity's
# phases by Unity's names. It is compiled the same way anything else is and then linked as a shared
# library whose undefined symbols are resolved out of the host at load time:
# the runtime, and whatever native calls the engine code it imported makes.
#
# CRITICAL: build a script with the same tree that will run it. A script
# carries its own copy of what it imported, so the host and the script agree
# about what a Model is only while both were built from the same sources.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

. "$ROOT/scripts/platform.sh"
. "$ROOT/scripts/native.sh"

SOURCE="${1:?usage: build_script.sh <script.ae> [output directory]}"
OUT_DIR="${2:-build/scripts}"
NAME="$(basename "$SOURCE" .ae)"

mkdir -p "$OUT_DIR"

SUFFIX="$(ae3d_native_suffix)"

GEN="$OUT_DIR/$NAME.c"
LIB="$OUT_DIR/$NAME$SUFFIX"

AETHER_CFLAGS="$(ae cflags --cflags 2>/dev/null || true)"
if [ -z "$AETHER_CFLAGS" ]; then
    echo "ae3d: 'ae cflags' produced nothing; is the toolchain on PATH?" >&2
    exit 1
fi

AETHER_LIB_DIR="$ROOT/src" aetherc "$SOURCE" "$GEN"

CC="${CC:-cc}"
case "$(uname -s)" in
    # The Aether runtime comes from the toolchain's own library; what stays
    # undefined is the engine, and that is resolved by linking the same engine
    # library the host links.
    Darwin) LINK_FLAGS="-dynamiclib" ;;
    *)      LINK_FLAGS="-shared" ;;
esac

if [ ! -f "$(ae3d_native_library)" ]; then
    ./build.sh --natives >/dev/null
fi

# A script sits in build/scripts, one directory below the library. It names
# GLFW as a program does: the engine's Aether it imports calls GLFW
# (ae3d.platform), and on Windows a DLL resolves everything at its link.
ae3d_glfw_flags
# shellcheck disable=SC2086
$CC -O2 -fwrapv $(ae3d_native_pic_flag) $AETHER_CFLAGS -Inative $LINK_FLAGS \
    "$GEN" $(ae3d_native_link_flags ..) $GLFW_LIBS -o "$LIB"

echo "built: $LIB"
