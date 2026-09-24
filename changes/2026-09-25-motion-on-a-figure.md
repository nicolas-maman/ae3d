### Natural motion on a figure

- `motion.on_figure(e, object)` gives an animated figure an active ragdoll
  (#439 slice 3a, #414). The ragdoll is made where the figure stands and
  turned to face the way its feet point, then dressed by the rig's
  humanoid naming: the engine's pipeline's, or Mixamo's with or without
  `mixamorig:` (`physics.humanoid_scheme`, `ragdoll_dress_humanoid`). The
  scene file carries it as the group's `motion` record, and
  `motion.from_record` puts it back.
- A scene file's model can carry records from more than one module's
  attachments: the figure's from `ae3d.figure`, the active ragdoll's from
  `ae3d.motion`.
- `tests/test_motion_figure.ae`, 8 checks:
  - both namings are known, and a two-bone rig is not;
  - dressed, each figure faces within 3.7° of its object's turn;
  - both stand on their muscles, the pelvis at 1.0 m, leaning 1°;
  - the records come back through the file.
