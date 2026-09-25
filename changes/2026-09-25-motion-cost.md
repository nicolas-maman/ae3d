### What natural motion costs, and physics freed with its engine

- `tests/test_motion_cost.ae` measures the fixed step of 0, 4, 16 and 32
  `POWERED` figures, kept awake by a bowing chest, with no window. A
  figure costs about 20 µs a step at 16 and 32, so about 50 fit in a
  millisecond and the dozen #414 is meant for cost a quarter of one. The
  table is in docs/motion.md.
- Measuring that found an engine hazard. Physics attached to an engine
  freed without `physics_free` stayed in `physics.of`'s list, and the next
  engine made at the same address was handed the dead world: its steps
  stopped counting. An engine now lets go of the systems attached to it
  when it is freed (`engine_on_free`), while its scene still stands.
  Physics registers itself there and takes the hook back when freed
  first. `physics_free` also frees the step it takes out of the engine,
  which it used to leak.
- `ci.sh` fails a build whose log has a line starting `error`, as it does
  for warnings. The Aether compiler printed an error for a local named
  `release` and built the program anyway, which then did something else
  (aether-lang-dev/aether#2211).
