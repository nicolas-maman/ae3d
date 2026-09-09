#!/usr/bin/env bash
# Compile one Aether script into a shared library the editor can attach.
#
#   scripts/build_script.sh resources/scripts/spin.ae [build/scripts]
#
# A script is an ordinary source file with script_start and script_update in
# it. It is compiled the same way anything else is and then linked as a shared
# library against the same engine library the host links, so the native calls
# the engine code it imported makes reach the one copy the host is using. That
# is not a nicety: on Windows a DLL cannot leave a symbol for its host to
# resolve, and the link fails on every one of them.
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
    Darwin)
        # The Aether runtime is linked in from the toolchain's own library, and
        # what remains undefined is resolved out of the host: a script and its
        # host share one heap because they share libc, not because they share a
        # copy of the runtime.
        LINK_FLAGS="-dynamiclib -undefined dynamic_lookup"
        ;;
    *)
        LINK_FLAGS="-shared"
        ;;
esac

if [ ! -f "$(ae3d_native_library)" ]; then
    echo "ae3d: $(ae3d_native_library) is missing; run ./build.sh on anything first" >&2
    exit 1
fi

# A script sits in build/scripts, one directory below the library.
# shellcheck disable=SC2086
$CC -O2 -fwrapv $(ae3d_native_pic_flag) $AETHER_CFLAGS -Inative $LINK_FLAGS \
    "$GEN" $(ae3d_native_link_flags ..) -o "$LIB"

echo "built: $LIB"
