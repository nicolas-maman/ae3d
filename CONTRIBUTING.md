# Contributing

## Before a pull request

```bash
./ci.sh
```

It builds the native layer with `-Wall -Wextra -Werror`, type-checks every
module, and runs every test suite and every example. It must pass with no
warnings.

This is the gate. Nothing runs it for you when a pull request opens: the
workflow is started by hand, from the Actions tab, and is there for the one
thing a local run cannot tell you, which is whether the other platform agrees.

## What the rules are

- **No warnings.** Not from the C compiler, not from `aetherc`. A warning that
  cannot be fixed in this repository gets an issue upstream and a comment naming
  it.
- **Tests assert behaviour, not shape.** A test that only checks a function
  returns something is not worth writing. The voxel suite pins exposed-face
  culling to exact counts; the noise suite checks the properties that
  characterise Perlin noise; the loader suite runs against the real model files,
  which is how three real bugs were found.
- **Comments explain why, not what.** A comment earns its place by recording a
  constraint a future edit could violate.
- **Measure before claiming.** Performance statements in commits and in the
  README come with the number and how it was obtained.
- **No leaks.** Every headless suite is checked under `leaks` and must report
  zero. A string field on a heap struct is owned by exactly one place: the setter
  that assigns it and the destructor that frees it. Never assign a string in a
  constructor directly, and never store a borrowed literal in one; see the note
  in `ae3d.core` for why.

## Where things go

Engine work belongs here. A defect or a missing capability in the Aether
compiler, its standard library, or aether-ui gets an issue on the corresponding
`aether-lang-dev` repository with a minimal reproduction, and is worked around
here only in a way that is defensible on its own merits.

## Adding a module

A module is `src/ae3d/<name>/module.ae` with an `exports (...)` list. Note that a
struct field typed by another module's struct is emitted before that struct's
definition (aether-lang-dev/aether#1856), which is why the math and scene types
share one module. Keep new modules to primitives, pointers and their own structs,
or add the type to `ae3d.core`.
