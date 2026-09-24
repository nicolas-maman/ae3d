# Testing and verification

How the engine is held to what it claims: a suite a subject, each a
program that prints its own verdict; benchmarks with no window; a critique
that judges a scene by number; a frame budget that fails on one extra
triangle; and a way of looking at a scene that does not trust one still.

## The suites

`tests/` is one program a suite, built and run like any example
(`./build.sh tests/test_physics.ae && ./build/test_physics`). Each prints
a line per check and ends with `<name>: all checks passed` or a list of
what failed, and exits accordingly. There are seventy-odd; the rule for a
new one is that it says what it measured and what it wanted, in numbers:

```
  ok   the crate fell onto the slab and rests on it
  ok   the sprung ragdoll is still standing after four seconds (1.593)
  ok   the car drove forward on its wheel joints (-31.2)
physics: all checks passed (240 fixed steps)
```

Most suites are headless: the math, the loaders, the ECS, the horde, the
flow field, the pose bank, the physics, the scene files, the undo history,
the input, the agent protocol. The rendering suites draw through the
offscreen target on both backends and read the pixels back
(`test_render`, `test_shadows`, `test_ray_shadows`, `test_ssr`, `test_taa`,
`test_fog`, `test_lights`, `test_impostor`, `test_skinned_render`, ...);
`test_backend_parity` draws one scene through Vulkan and OpenGL and holds
them to 0.7% of channels differing. A suite that needs a device skips
itself with a message where there is none, and says so rather than
passing.

Every suite that spreads work over the job pool runs at one thread and at
the machine's count, and the answer has to be the same.

## Benchmarks

`benchmarks/` measures per-frame cost with no window and no driver, so
the cost and the allocation behaviour are the engine's own:
`bench_frame` (the heaviest scenes' per-frame work), `bench_scene`,
`bench_shadow`, `bench_readback`, `bench_black_hole`. `AE3D_PERF=1` on any
program prints the device's timestamps around each stage of the frame and
the CPU's time in building it ([performance.md](performance.md));
`scripts/perf.sh` takes the best of three hidden runs of each scene and
prints the table that page keeps.

`scripts/validate.sh [scene ...]` rebuilds each example and runs it on
Vulkan under the Khronos validation layer, printing its error count and the
distinct VUIDs of any that has one, and exits non-zero if any had one.
Every example runs clean, so a new error is the change that made it. The
hosted runners' software Vulkan traces no rays and has none of the
DLSS, meter or crowd paths a GPU takes, so this runs on a GPU before a
renderer change is pushed -- and once more on Mesa's lavapipe
(`VK_DRIVER_FILES=<its lvp_icd json> scripts/validate.sh`, `AE3D_VALIDATE_FRAMES=8`
since it is slow), which takes the paths a device without ray queries
takes and has the smaller limits many real devices have. `AE3D_VALIDATE_SYNC=1`
adds the layer's synchronization validation, and every example is clean
under it too: a hazard is ordering a GPU forgives today and a driver that
overlaps more will not.

A number in a document is quoted with its pair from the same run on the
same machine, because two runs on a shared GPU differ by more than most
optimisations gain.

## The critique and the budget

A program that renders can be checked for not crashing and for the number
of draws it issued. Neither says what it drew. Two programs run on every
build over the agent channel ([agent.md](agent.md)) against the measuring
rig `tools/zombie_street.ae` -- one block, one zombie:

- **`tools/critique_scene.ae`** asks whether the scene is any good, with the
  standards a real art review applies and the number each of them is:
  texel density (no surface softer than a texel every four millimetres),
  a normal map on every surface big enough to stand next to, the figure's
  triangle count, the street lit in pools rather than flooded flat, no
  foot through the road, a planted foot staying planted, the strike
  reaching past the walk, the head following the body. Each constant
  carries the reason for its value beside it.
- **`tools/ae3d_bench.ae`** records what a frame costs -- draws, triangles,
  program and material binds, GPU pass times -- in
  `resources/zombie_street.<backend>.budget.json`, and a build that draws
  one more triangle than the record fails until the record is deliberately
  re-taken with `--record`.

Both run on both backends in `ci.sh`, and both exit 3 rather than 1 where
the frame cannot be read back (software Vulkan on a headless runner has no
swapchain), which is a skip and not a judgement.

## Looking at a scene

One still hides most of what goes wrong in a scene: a figure that
teleports, one that vanishes on a zoom, a seam that shows from a grazing
angle, a shadow that slides with the camera. So a scene is verified from a
sweep, and by number:

- `AE3D_VIEW=n`, `AE3D_CAMX/Y/Z` and `AE3D_AIMX/Y/Z` place the camera by
  number in the scenes that take them; `scripts/contact_sheet.sh <scene>
  out.png` renders the default view, a low grazing view and a view toward
  the sun on both backends and lays the six out as one picture
  (`tools/montage.ae`).
- A detail is judged at full size with `tools/crop.ae`, and in numbers with
  `tools/probe_image.ae`: the mean colour and greyness of each band of a
  frame, and what moved between two. A thumbnail is not evidence; a
  sampled pixel with its four channels is.
- The scenes log what they decided (`zombie_city[diag]`, `AE3D_DIAG=1` in
  the street): the tiers and their triangle counts, every light, each
  material's texture and the GPU id it resolved to, and a periodic check
  that no figure ever moved further than it can walk. A clean position
  check can still miss animation-driven "teleporting", which is why the
  audit runs every frame over the pool.
- `tools/ae3d_view.ae` prints a frame the channel answered with as
  characters, one cell a mean, so a run on a machine without a screen can
  still be looked at; `tools/measure_scene.ae` asks the engine what each
  model is made of, where it landed on screen, what colour it arrived at,
  and whether the picture is stable under a movement too small to change
  what is in it.

## Leaks and platforms

On macOS `ci.sh` runs the headless programs under `leaks --atExit` and
fails on a lost allocation whose stack is the engine's; a system retain
cycle from a GPU context is reported apart (`AE3D_SKIP_LEAKS=1` skips it). The three runners (Linux with GTK 4
and a software Vulkan, macOS with MoltenVK, Windows under MSYS2 UCRT64)
build every C file alone with warnings as errors, because every
Windows-only assumption in the tree survived exactly as long as there was
no Windows runner.

## Writing a test

A test states what it wanted and what it got, in the unit the reader
thinks in (metres, frames, milliseconds, channels), on the line it fails.
It drives the engine for a bounded number of frames or steps, never "until
it settles". It reads the engine's own numbers -- transforms, counters,
pixels through the offscreen target or the channel -- rather than a file
written by another tool. And where it measures time it prints the pair:
what it took and what the previous implementation took, in the same run.
