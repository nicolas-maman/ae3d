# ae3d documentation

The engine's documentation, one page a subject, each written to be read
on its own and each holding to the same rule as the code: a claim is a
number, a command or a file the reader can check. The [README](../README.md)
at the root is the front page; this is the map.

## Reading order

| Page | What it answers |
|---|---|
| [Architecture](architecture.md) | How the engine is put together: the modules, the frame, the two renderers, the job pool, what is still C and why |
| [Building](building.md) | Toolchain, dependencies per platform, `build.sh`, `ci.sh`, the editor's build, the shader generator, the environment variables every program honours |
| [Rendering](rendering.md) | Every rendering feature, with the reasoning and the measurement behind each: shading, shadows, ray queries, DLSS, impostors, the crowd sorted on the device |
| [Physics](physics.md) | `ae3d.physics` over aephysics: rigidbodies, colliders, ragdolls, vehicles, hit events; the four reference scenes and the driven street |
| [Crowds and navigation](crowds.md) | Pose banks, the horde's kernels, the flow field, the ECS, the job pool the simulation runs on |
| [The asset pipeline](pipeline.md) | Blender to engine: the exporter, the manifest, the critique, glTF from anywhere |
| [The agent channel](agent.md) | The JSON channel a program, a test or an AI agent drives a running scene through |
| [The editor](editor.md) | The scene editor: panels, controls, undo, scene files, how the viewport is drawn |
| [Performance](performance.md) | How a frame is measured, and where the frames are on the machine the numbers were taken on |
| [Testing and verification](testing.md) | The suites, the benchmarks, the critique, the frame budget, and how a scene is verified by number rather than by eye |
| [Writing Aether in this engine](writing-aether.md) | The conventions and the language's edges the engine is written around |
| [The black hole](black-hole.md) | A worked example: Kerr geodesics per pixel, and what checking them taught the engine |

## Where things are

```
src/ae3d/     the engine, one module a directory (docs: architecture.md)
native/       the C that remains, by role: gpu/, geometry/, image/, platform/, agent/, dlss/ (native/README.md)
deps/         aephysics, the physics engine, as a submodule
examples/     runnable scenes; examples/lib/ what they share
tests/        one program a suite, each printing its own verdict
benchmarks/   per-frame cost measured without a window
tools/        the agent client, the viewer, the critique, the bench, the bakers
scripts/      export, critique, perf, the build's platform and native halves
editor/       the scene editor
docs/         this
docs/images/  the pictures the pages and the README show
```

## Conventions

Every page states what a thing costs where a cost was measured, names the
machine, and quotes pairs taken in the same run. A feature that is not
measured is described as such. Numbers older than the code they describe
are dated.
