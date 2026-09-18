# Performance

What a frame costs, measured by the engine itself and said in numbers that
mean the same on every machine: the device's own timestamps around each
stage of the frame, and the CPU's time in building and submitting it.

## Measuring

Any program run with `AE3D_PERF=1` says, when it ends, what an average
frame cost past the first ten (which carry the uploads and the pipeline
builds, the cost of starting and not of a frame):

```
perf backend=vulkan frames=110 fps=86 gpu_shadow_ms=0.03 gpu_scene_ms=7.25 gpu_post_ms=0.01 cpu_ms=0.14 draws=6 width=1280 height=800
perf stages camdepth=0.03 sky=6.68 opaque=0.20 occlusion=0.06 transparent=0.28
```

On Vulkan the frame is split by eight timestamps -- the start, and the end
of the shadow pass, the camera-depth prepass, the sky, the opaque draws,
the occlusion, the transparent draws and the post chain -- so the second
line says which stage a frame's cost is in. The same split is in the
editor's stats bar and in the agent channel's `frame.stats`.

`scripts/perf.sh [scene ...]` runs each scene hidden on Vulkan for
`AE3D_PERF_FRAMES` frames (240) `AE3D_PERF_RUNS` times (3) and keeps the
run with the highest frame rate: on a machine where anything else is using
the GPU, the other runs measured that too. It prints the table below.

## Where the frames are (2026-09-18, RTX 4070 Ti, 1280x800, 4x MSAA, Vulkan)

| scene | fps | gpu ms | sky | opaque | occlusion | transparent | shadow | post | cpu ms | draws |
|---|---|---|---|---|---|---|---|---|---|---|
| spinning_cube | 144 | 0.02 | 0.00 | 0.00 | 0.01 | 0.00 | 0.01 | 0.00 | 0.10 | 2 |
| models | 144 | 0.12 | 0.00 | 0.00 | 0.01 | 0.00 | 0.04 | 0.00 | 0.09 | 8 |
| lights | 144 | 0.07 | 0.00 | 0.05 | 0.01 | 0.00 | 0.02 | 0.01 | 0.10 | 9 |
| caustics | 144 | 0.80 | 0.32 | 0.00 | 0.06 | 0.40 | 0.04 | 0.00 | 0.22 | 275 |
| sand | 72 | 6.11 | 0.46 | 2.79 | 0.10 | 0.01 | 0.07 | 0.00 | 3.62 | 15 |
| smooth_terrain | 130 | 2.12 | 1.52 | 0.19 | 0.08 | 0.26 | 0.03 | 0.01 | 0.10 | 6 |
| voxel_world | 131 | 1.32 | 0.76 | 0.24 | 0.08 | 0.17 | 0.12 | 0.00 | 0.12 | 6 |
| zombie_city | 141 | 4.83 | 0.02 | 2.59 | 0.08 | 0.01 | 2.22 | 0.01 | 0.69 | 1194 |
| zombie_street | 144 | 0.42 | 0.02 | 0.34 | 0.03 | 0.00 | 0.09 | 0.23 | 0.25 | 338 |

Before the clouds were baked into textures (the previous entry, the same
day), the sky stage of `smooth_terrain` was 6.68 ms and the scene ran at
86 fps; `voxel_world` 5.67 ms at 87; `sand` 5.73 ms at 63.

Hidden windows are not presented, so the rate is the engine's and not the
display's; the machine was also running a game on the same GPU, which is
why the best of the runs is kept and why the numbers are a floor for a
quiet machine rather than a ceiling.

What the table says: the scenes under a sky are bounded by the hidden
window's 144 Hz clock now, not by the clouds. The sky stage -- the sky
itself and the clouds marched over it, a fetch a sample from the baked
weather and shape textures -- is a millisecond and a half in
`smooth_terrain` and under one in `voxel_world`, where it was five to
seven. The sand's opaque stage is a million grains at eight triangles each
and its CPU time is the simulation; the city's shadow pass is its buildings
and its crowd into the shadow map. Those two are where the frame is now.

The table found a stall as well: `lights` spent 6.9 ms of CPU a frame, at
nine draws, because a batched model that moves had its instance buffer
freed and re-uploaded every frame, and freeing a buffer the device may
still be reading waits for the whole device. The batch writes its ring
now, like a model's own stream, and the scene's CPU time is a tenth of a
millisecond. A number in the CPU column that is not near zero is that
kind of thing: the engine's CPU work in a frame is small, so what shows
up there is a wait.
