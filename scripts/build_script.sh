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
case "$(uname -s)" in
    Darwin)
        # The host resolves what the script leaves undefined. Without this the
        # link fails on every runtime symbol the script's own imports call.
        LINK_FLAGS="-dynamiclib -undefined dynamic_lookup"
        ;;
    *)
        LINK_FLAGS="-shared -fPIC"
        ;;
esac

# shellcheck disable=SC2086
$CC -O2 -fwrapv -fPIC $AETHER_CFLAGS -Inative $LINK_FLAGS "$GEN" -o "$LIB"

echo "built: $LIB"
