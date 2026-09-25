### Animated figures in the editor

- The editor places, plays and saves animated figures (#439, slice 2).
  - The asset browser lists `resources/figures/*.glb` after the meshes.
  - The inspector's FIGURE section has a button per clip, Loop, speed,
    and a time row that scrubs.
  - A figure plays while the scene is simulated.
  - Its meshes come and go with its group (delete, undo, redo).
  - The scene file carries its `figure` record, and a scene read back
    makes the figure again.
- The editor's bounded run checks it: the components scene holds the arm
  fixture, the report says `figures 1` and `figure_stuck 0` (it played
  while simulated), and the roundtrip brings it back through the file,
  on OpenGL and Vulkan. No inspector row is stuck.
- The undo slots moved up two, past the two new rows.
