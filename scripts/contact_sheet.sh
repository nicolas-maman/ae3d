#!/usr/bin/env bash
# A scene from every camera that matters, on both backends, in one picture.
#
# One camera flatters a scene: the water read well from the island's default
# view and as stripes from a low one, the clouds well looking up and as dice
# looking toward the sun. This renders a scene's default view, a low grazing
# view and a view toward the sun, on OpenGL and on Vulkan where there is a
# driver, and lays the six frames out as one sheet with tools/montage.
#
#   scripts/contact_sheet.sh smooth_terrain out.png
#   scripts/contact_sheet.sh voxel_world out.png
#
# The views are the scene's own AE3D_CAMX/Y/Z and AE3D_AIMX/Y/Z knobs; a scene
# without them renders its default view three times, which is still the two
# backends side by side.
set -euo pipefail

scene="${1:?scene name, e.g. smooth_terrain}"
out="${2:?output png}"
frames="${AE3D_SHEET_FRAMES:-60}"
bin="./build/$scene"
[ -x "$bin" ] || bin="$bin.exe"
[ -x "$bin" ] || { echo "contact_sheet: no $bin; build it first" >&2; exit 2; }
[ -x ./build/montage ] || [ -x ./build/montage.exe ] || ./build.sh tools/montage.ae >/dev/null

# name cam(x,y,z) aim(x,y,z) per scene: default, low and grazing, toward the sun.
case "$scene" in
    smooth_terrain)
        views=("default:::" "low:1000,14,430:780,8,520" "sunward:1150,60,900:300,120,-200") ;;
    voxel_world)
        views=("default:::" "low:-40,20,300:250,40,380" "sunward:700,60,760:100,80,100") ;;
    sand)
        views=("default:::" "low:-600,40,300:0,120,0" "sunward:600,90,-700:-200,150,300") ;;
    *)
        views=("default:::" "second:::" "third:::") ;;
esac

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
list=()
for backend in opengl vulkan; do
    for view in "${views[@]}"; do
        name="${view%%:*}"
        rest="${view#*:}"
        cam="${rest%%:*}"
        aim="${rest#*:}"
        env_args=(AE3D_API="$backend" AE3D_HIDDEN=1 AE3D_FRAMES="$frames" AE3D_SNAPSHOT="$tmp/$backend-$name.png")
        if [ -n "$cam" ]; then
            IFS=, read -r cx cy cz <<<"$cam"
            IFS=, read -r ax ay az <<<"$aim"
            env_args+=(AE3D_CAMX="$cx" AE3D_CAMY="$cy" AE3D_CAMZ="$cz" AE3D_AIMX="$ax" AE3D_AIMY="$ay" AE3D_AIMZ="$az")
        fi
        if env "${env_args[@]}" "$bin" >"$tmp/$backend-$name.log" 2>&1 && [ -s "$tmp/$backend-$name.png" ]; then
            list+=("$tmp/$backend-$name.png")
            echo "  $backend $name: $(grep -iE 'fps|frames' "$tmp/$backend-$name.log" | tail -1)"
        else
            echo "  $backend $name: no frame ($(tail -1 "$tmp/$backend-$name.log"))"
        fi
    done
done
[ "${#list[@]}" -gt 0 ] || { echo "contact_sheet: nothing rendered" >&2; exit 1; }
./build/montage "$out" "${list[@]}"
