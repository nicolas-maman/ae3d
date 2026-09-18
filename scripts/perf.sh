#!/usr/bin/env bash
# What each scene costs on the device, stage by stage, on Vulkan.
#
#   scripts/perf.sh                 # every scene, into docs/performance.md's table form
#   scripts/perf.sh sand zombie_city
#
# Each scene runs hidden for AE3D_PERF_FRAMES frames (default 240) three
# times, and the run with the highest frame rate is kept: on a machine
# where anything else is using the GPU, the other runs measured that too.
# The engine says the numbers itself (AE3D_PERF=1), so they are the device's
# own timestamps and the same on every machine; a number here that moves is
# the engine moving, not the tool.
set -euo pipefail

frames="${AE3D_PERF_FRAMES:-240}"
runs="${AE3D_PERF_RUNS:-3}"
scenes=("$@")
[ "${#scenes[@]}" -gt 0 ] || scenes=(spinning_cube models lights caustics sand smooth_terrain voxel_world zombie_city zombie_street)

printf '| scene | fps | gpu ms | sky | opaque | occlusion | transparent | shadow | post | cpu ms | draws |\n'
printf '|---|---|---|---|---|---|---|---|---|---|---|\n'
for scene in "${scenes[@]}"; do
    bin="./build/$scene"
    [ -x "$bin" ] || bin="$bin.exe"
    if [ ! -x "$bin" ]; then
        printf '| %s | not built | | | | | | | | | |\n' "$scene"
        continue
    fi
    best=""
    best_fps=0
    for run in $(seq 1 "$runs"); do
        out="$(AE3D_API=vulkan AE3D_PERF=1 AE3D_HIDDEN=1 AE3D_FRAMES="$frames" "$bin" 2>&1 | grep '^perf' || true)"
        fps="$(printf '%s\n' "$out" | sed -n 's/.* fps=\([0-9.]*\).*/\1/p' | head -1)"
        [ -n "$fps" ] || continue
        if [ -z "$best" ] || [ "$(awk -v a="$fps" -v b="$best_fps" 'BEGIN { print (a > b) ? 1 : 0 }')" = 1 ]; then
            best="$out"
            best_fps="$fps"
        fi
    done
    if [ -z "$best" ]; then
        printf '| %s | no perf line | | | | | | | | | |\n' "$scene"
        continue
    fi
    field() { printf '%s\n' "$best" | sed -n "s/.* $1=\([0-9.]*\).*/\1/p" | head -1 | awk '{printf "%.2f", $1}'; }
    printf '| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n' \
        "$scene" "$(field fps | awk '{printf "%.0f", $1}')" "$(field gpu_scene_ms)" \
        "$(field sky)" "$(field opaque)" "$(field occlusion)" "$(field transparent)" \
        "$(field gpu_shadow_ms)" "$(field gpu_post_ms)" "$(field cpu_ms)" \
        "$(printf '%s\n' "$best" | sed -n 's/.* draws=\([0-9]*\).*/\1/p' | head -1)"
done
