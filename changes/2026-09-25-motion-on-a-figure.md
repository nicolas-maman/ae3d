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
- The editor's MOTION section (#439 slice 3b) sits beside the FIGURE
  section. It has a Natural motion switch, Animated / Powered / Limp,
  Protective, strength, and get-up time.
  - Simulate puts every figure with natural motion on its muscles, on the
    scene's static bodies, or on a floor at its feet where there are none.
  - A click on a figure while simulating strikes it (450 N·s along the
    click), and it falls the way a person does.
  - Stop puts every bone and the figure's group back where they were.
  - The scene file carries the `motion` record.
  - The bounded run holds it to numbers with the humanoid fixture: on its
    muscles the pelvis stays at 1.00 m; struck, it is at 0.21 m a second
    and a half later; stopped, its bones are back to 0 mm. This passes on
    OpenGL, on Vulkan, and through the file (`motion_stuck 0`).
- The editor's drag check starts from an empty history. A drag that
  recorded nothing had its undo take back the last object the scene added,
  and the check passed with that object gone. That hid the arm figure from
  the figure check.
