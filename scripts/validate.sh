#!/usr/bin/env bash
# The Vulkan renderer under the Khronos validation layer, scene by scene.
#
#   scripts/validate.sh                  # every example
#   scripts/validate.sh zombie_city sand
#
# Each example is rebuilt first (an old binary would validate old code) and
# runs hidden for AE3D_VALIDATE_FRAMES frames (default 40) on Vulkan with the
# layer on. Prints each scene's error count, the first few distinct VUIDs of
# any that has errors, and exits non-zero if any scene had one. It needs the
# Vulkan SDK's layer: VK_LAYER_PATH pointing at it where the loader does not
# find it itself (on Windows, the SDK's Bin directory).
set -uo pipefail

frames="${AE3D_VALIDATE_FRAMES:-40}"
scenes=("$@")
if [ "${#scenes[@]}" -eq 0 ]; then
    for f in examples/*.ae; do scenes+=("$(basename "$f" .ae)"); done
fi

failed=0
for scene in "${scenes[@]}"; do
    if ! ./build.sh "examples/$scene.ae" > /dev/null 2>&1; then
        printf '%-24s does not build\n' "$scene"
        failed=1
        continue
    fi
    bin="./build/$scene"
    [ -x "$bin" ] || bin="$bin.exe"
    out="$(VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation AE3D_API=vulkan AE3D_HIDDEN=1 \
           AE3D_FRAMES="$frames" timeout 180 "$bin" 2>&1)"
    if ! printf '%s\n' "$out" | grep -q '^ae3d: Vulkan on'; then
        printf '%-24s did not start on Vulkan\n' "$scene"
        continue
    fi
    errors="$(printf '%s\n' "$out" | grep -c 'Validation Error' || true)"
    printf '%-24s %s\n' "$scene" "$errors"
    if [ "$errors" != 0 ]; then
        failed=1
        printf '%s\n' "$out" | grep -o 'VUID-[A-Za-z0-9_-]*' | sort | uniq -c | sort -rn | head -5 | sed 's/^/    /'
    fi
done
exit "$failed"
