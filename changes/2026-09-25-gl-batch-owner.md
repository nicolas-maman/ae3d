### OpenGL shadows under a roof

- A roof over a cube cast no shadow on OpenGL once other cubes of the same
  mesh stood nearby (#453). The shadow pass's batch and the lit pass's
  batches share one instance buffer per vertex array, and a batch that had
  not moved drew whatever matrices another batch had uploaded last. The
  geometry now remembers which batch filled its buffer and how big it is,
  and a batch uploads again whenever another's matrices are there.
  `tests/test_shadow_batches.ae` reads the cube 0.44 in the roof's shadow
  against 0.64 without shadows; before the fix the two were equal.
