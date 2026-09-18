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
editor's stats bar and in the agent channel's `frame.stats`. `update_ms`
is the behaviours' own time -- the fixed updates, the updates, the clips
advancing: the simulation -- which is where a frame goes once the device
is done with it: half a million zombies are 8 ms of device and 25 of
simulation.

`scripts/perf.sh [scene ...]` runs each scene hidden on Vulkan for
`AE3D_PERF_FRAMES` frames (240) `AE3D_PERF_RUNS` times (3) and keeps the
run with the highest frame rate: on a machine where anything else is using
the GPU, the other runs measured that too. It prints the table below.

## Where the frames are (2026-09-18, RTX 4070 Ti, 1280x800, 4x MSAA, Vulkan)

| scene | fps | gpu ms | sky | opaque | occlusion | transparent | shadow | post | cpu ms | draws |
|---|---|---|---|---|---|---|---|---|---|---|
| spinning_cube | 144 | 0.02 | 0.00 | 0.01 | 0.01 | 0.00 | 0.01 | 0.00 | 0.11 | 2 |
| models | 144 | 0.11 | 0.00 | 0.09 | 0.01 | 0.00 | 0.03 | 0.00 | 0.10 | 8 |
| lights | 144 | 0.07 | 0.00 | 0.04 | 0.01 | 0.00 | 0.02 | 0.01 | 0.11 | 9 |
| caustics | 144 | 0.86 | 0.01 | 0.31 | 0.06 | 0.47 | 0.04 | 0.00 | 0.16 | 275 |
| sand | 74 | 6.26 | 0.63 | 2.79 | 0.09 | 0.02 | 0.07 | 0.00 | 3.43 | 15 |
| smooth_terrain | 139 | 2.98 | 2.22 | 0.34 | 0.12 | 0.23 | 0.05 | 0.02 | 0.09 | 6 |
| voxel_world | 144 | 1.44 | 0.89 | 0.24 | 0.08 | 0.16 | 0.12 | 0.00 | 0.09 | 6 |
| zombie_city | 137 | 3.23 | 0.02 | 2.75 | 0.08 | 0.02 | 0.08 | 0.01 | 0.75 | 1192 |
| zombie_street | 144 | 0.33 | 0.01 | 0.28 | 0.01 | 0.00 | 0.07 | 0.23 | 0.20 | 338 |

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
weather and shape textures -- is a millisecond and a half to two in
`smooth_terrain` and under one in `voxel_world`, where it was five to
seven. The sand's opaque stage is a million grains at eight triangles each
and its CPU time is the simulation; the city's shadow pass is its buildings
and its crowd into the shadow map. Those two are where the frame is now.

The crowd's depth-only passes were its cost. At `AE3D_CROWD=2000` the
shadow pass and the camera-depth prepass were 10 ms each and the opaque
draws 13, because the near tier's 26,636-triangle figure went through all
three; a shadow reads a figure's silhouette, not its face, so the near
tier now casts from the far tier's 168-triangle stand-in
(`model_set_depth_proxy`): 2,000 figures 30 to 68 fps, 20,000 at
`AE3D_NEAR=28` 54 to 91, and the 400 of the default scene lose their 2.2
ms shadow pass. On OpenGL, where a vertex array binds a mesh to its
instance stream, the model keeps a second array over the proxy's mesh and
its own stream, made the first time the shadow pass needs it.

The camera-depth prepass itself is gone on Vulkan. It drew every visible
triangle a second time so the occlusion, the reflection and the water had
a depth to read, and it could only be given the proxy -- whose silhouette,
a few pixels out from the mesh's, put the figure behind into the occlusion
of the figure in front. The depth those read is the frame's own now,
resolved after the opaque draws (the nearest of the samples a pixel, in a
full-screen pass between the two halves the scene pass is drawn in), the
way OpenGL has blitted its depth all along: 20,000 at `AE3D_NEAR=28` 100
to 120 fps, 1,193 draws to 419, the `camdepth` stage 4.8 ms to nothing. A
change to the engine that adds a depth-only draw is a change the stage
table shows.

Past the far tier the horde is pictures. `model_set_impostor` draws a
crowd model's instances as upright quads showing, from an atlas
`tools/bake_impostor` made of the figure, the cell for the angle the
camera sees the figure from and the frame of its walk -- two triangles a
figure, lit by the scene's lights through the figure's baked albedo and
normals, so the mesh and the picture beside it match under the same lamp.
`zombie_city` swaps at `AE3D_IMPOSTOR` metres (80). With the near band
small (`AE3D_NEAR=3`, props off, separation every fourth frame), so the
figures are the cost and not the road's density:

| crowd | pictures | fps | scene ms | shadow ms |
|---|---|---|---|---|
| 100,000 | off | 95 | 6.2 | 3.9 |
| 100,000 | past 80 m | 138 | 2.1 | 0.6 |
| 500,000 | off | 21 | 28.8 | 19.1 |
| 500,000 | past 80 m | 35 | 8.0 | 2.7 |

(Measured before the job pool below; with it, half a million are 79 fps.)

At half a million the device draws its frame in 11 ms. What was left
was the simulation: 25 ms of one thread stepping half a million figures
(`update_ms`, and `zombie_city[perf] sim` names the passes -- the
heading 2.1, the shove 2.3, the step 8.6, the sort 4.3, the matrices and
the upload 10.0). Every one of those passes is the same few lines on every
figure with nothing shared but the arrays, so they run over the engine's
job pool now (`native/ae3d_jobs.c`: a worker for every hardware thread
but the main one, `ae3d_jobs_for` handing runs of a few thousand figures
to the workers and the caller alike; `AE3D_JOBS=n` sets the thread count,
1 is the main thread alone) and the sort goes into its three tiers in one
pass instead of a pass and a pass over the far tier's output. On a
24-thread machine:

| crowd | threads | fps | update ms | heading | shove | step | sort | upload |
|---|---|---|---|---|---|---|---|---|
| 500,000 | 1 | 36 | 24.4 | 2.2 | 2.3 | 8.8 | 4.5 | 6.9 |
| 500,000 | 4 | 69 | 12.0 | 0.6 | 1.7 | 2.2 | 3.2 | 4.4 |
| 500,000 | 24 | 79 | 8.0 | 0.4 | 1.7 | 1.0 | 2.8 | 2.3 |

What did not scale was what is bound by memory rather than arithmetic:
the sort and the upload wrote each figure's eight doubles, its sixty-four
bytes of matrix and its eighty bytes of instance stream every frame, a
hundred-odd megabytes. (The "sort" column above also counted the
teleport audit, a scan of every figure against its last position; on its
own the sort was 1.5 ms, and the audit is native over the pool now, 0.4.)

So on Vulkan the sort is the device's (`crowd.device_crowd_new`; see
docs/rendering.md, "The crowd sorted on the device"): the CPU converts the
figures' state to thirty-two bytes each into a buffer in the device's
memory, and a compute pass picks the tiers, builds the matrices, packs the
streams and counts the draws, which are indirect. Same machine, same
scene (`AE3D_CROWD=500000 AE3D_NEAR=3 AE3D_NOPROPS=1 AE3D_SEPN=4`):

| crowd | sort | fps | update ms | cpu ms | sort + upload ms | gpu shadow ms | gpu scene ms |
|---|---|---|---|---|---|---|---|
| 500,000 | CPU, 24 threads | 75 | 7.4 | 3.3 | 1.6 + 2.5 | 3.4 | 9.2 |
| 500,000 | device | 90 | 3.1 | 2.4 | 0 + 0 | 3.0 | 8.1 |

The state buffer's memory matters: read by the sort from system memory
over the bus, sixteen megabytes a frame cost the device 1.7 ms -- more
than the sort -- and the frame rate fell below the CPU sort's. In the
device's own host-visible memory (the BAR; the engine falls back to
system memory where there is none) the sort is under 0.2 ms and the
frame is the draw's. What is left on the CPU at half a million is the
simulation itself -- the shove 1.7 ms, the step 0.8, the heading 0.3 --
and the conversion, about a millisecond; the shove's grid is still built
on one thread.

The table found a stall as well: `lights` spent 6.9 ms of CPU a frame, at
nine draws, because a batched model that moves had its instance buffer
freed and re-uploaded every frame, and freeing a buffer the device may
still be reading waits for the whole device. The batch writes its ring
now, like a model's own stream, and the scene's CPU time is a tenth of a
millisecond. A number in the CPU column that is not near zero is that
kind of thing: the engine's CPU work in a frame is small, so what shows
up there is a wait.
