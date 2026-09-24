### A box man to test figures against

- `tools/make_gltf_fixture.ae` writes `tests/fixtures/gltf/humanoid.glb`
  beside the arm fixtures (which come out byte for byte as before): a
  skinned box man 1.84 m tall, standing on y = 0 and facing -Z, on a
  twenty-bone Mixamo rig named with the `mixamorig:` prefix under an
  `Armature` node, at the proportions of the engine's test rigs (hips one
  metre up, shoulders 0.17 m out from Spine2, legs 0.1 m out from the hips).
  One box a bone, every vertex weighted wholly to it; two clips, `Idle`
  (Spine2 bowing eight degrees forward and back over two seconds, looping
  without a seam) and `Wave` (the right arm raised 150 degrees out to the
  side and lowered over one second). 27,652 bytes, for tests of the figure
  and active-ragdoll code that want a real file rather than a rig built in
  code.
- `tests/test_humanoid_fixture.ae` loads it with `ae3d.gltf`, no warnings,
  and checks the twenty names, every bone's rest position against the
  offsets added up (by its node and by its inverse bind), the mesh standing
  on the ground with every triangle facing out, and where the two clips put
  a chest vertex and a hand vertex against the rotation computed in the
  test.
