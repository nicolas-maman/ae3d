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
| smooth_terrain | 86 | 7.25 | 6.68 | 0.20 | 0.06 | 0.28 | 0.03 | 0.01 | 0.14 | 6 |
| voxel_world | 87 | 6.44 | 5.67 | 0.29 | 0.07 | 0.33 | 0.12 | 9.31 | 0.15 | 6 |
| sand | 63 | 11.67 | 5.73 | 2.90 | 0.21 | 0.01 | 0.07 | 0.00 | 4.74 | 15 |
| zombie_city | 141 | 4.85 | 0.02 | 2.59 | 0.08 | 0.01 | 2.22 | 0.01 | 0.71 | 1194 |
| zombie_street | 145 | 0.32 | 0.01 | 0.27 | 0.01 | 0.00 | 0.07 | 0.26 | 0.20 | 338 |

Hidden windows are not presented, so the rate is the engine's and not the
display's; the machine was also running a game on the same GPU, which is
why the best of the runs is kept and why the numbers are a floor for a
quiet machine rather than a ceiling.

What the table says: **the clouds are the frame.** In every scene under a
sky, the sky stage -- the sky itself and the clouds marched over it -- is
five to seven milliseconds of a seven-millisecond frame, and the geometry,
the water, the occlusion and the shadows are a millisecond between them.
The sand's opaque stage is a million grains at eight triangles each; the
city's shadow pass is its buildings and its crowd into the shadow map. The
clouds are the next thing to be made both better and cheaper.
