#!/usr/bin/env bash
# Re-export the Blender fixtures, wherever Blender happens to be installed.
#
#   ./scripts/export_assets.sh                     regenerate tests/fixtures/exported/
#   ./scripts/export_assets.sh path/to/file.blend out/dir
#
# Skips rather than fails when Blender is absent. The exporter needs it; the
# loading half does not, which is why what it produces is committed and
# tests/test_assets runs everywhere.
#
# BLENDER overrides the search.

set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

find_blender() {
    if [ -n "${BLENDER:-}" ]; then printf '%s' "$BLENDER"; return 0; fi
    if command -v blender >/dev/null 2>&1; then command -v blender; return 0; fi
    # Windows installs land outside PATH, and there is a version in the path.
    for base in "/c/Program Files/Blender Foundation" "/d/Program Files/Blender Foundation" \
                "/Applications/Blender.app/Contents/MacOS"; do
        [ -d "$base" ] || continue
        # The name tests are grouped: -maxdepth is an option rather than a
        # test, so writing it on both sides of -o applies it to neither and
        # GNU find says so. Depth 3 because a Windows install is
        # "Blender Foundation/Blender 5.2/blender.exe" and a macOS one is
        # "Blender.app/Contents/MacOS/Blender".
        found="$(find "$base" -maxdepth 3 -type f \
                     \( -name 'blender.exe' -o -name 'Blender' -o -name 'blender' \) \
                     2>/dev/null | sort | tail -1)"
        if [ -n "$found" ]; then printf '%s' "$found"; return 0; fi
    done
    return 1
}

BLENDER_BIN="$(find_blender)" || {
    echo "export_assets: skip, no Blender found (set BLENDER=/path/to/blender)"
    exit 0
}
echo "export_assets: using $BLENDER_BIN"

# With no arguments, regenerate everything the repository ships: the test
# fixture and the showcase examples/blender_pipeline loads.
if [ $# -eq 0 ]; then
    "$0" tests/fixtures/spin.blend tests/fixtures/exported || exit 1
    "$0" resources/blender/showcase.blend resources/blender/showcase || exit 1
    exit 0
fi

SOURCE="$1"
OUT="${2:?usage: export_assets.sh [<file.blend> <out-directory>]}"

# The .blend files are generated, not committed: a .blend is a file nobody can
# review and nobody can regenerate when Blender changes it.
if [ ! -f "$SOURCE" ]; then
    case "$SOURCE" in
        tests/fixtures/spin.blend)        builder=tools/blender/make_fixture.py ;;
        resources/blender/showcase.blend) builder=tools/blender/make_showcase.py ;;
        *)                                builder="" ;;
    esac
    if [ -n "$builder" ]; then
        echo "export_assets: building $SOURCE first"
        "$BLENDER_BIN" --background --factory-startup \
            --python "$builder" -- --out "$SOURCE" \
            2>&1 | grep -E "make_|Error" || true
    fi
fi

"$BLENDER_BIN" --background "$SOURCE" \
    --python tools/blender/ae3d_export.py -- --out "$OUT" \
    2>&1 | grep -E "^ae3d_export" || true
