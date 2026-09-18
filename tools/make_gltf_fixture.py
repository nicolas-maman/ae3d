#!/usr/bin/env python3
"""Write the glTF fixtures tests/test_gltf reads: a two-bone arm, once as a
.glb and once as a .gltf beside its .bin, from the same numbers.

The arm is a box two units tall on the Y axis, its lower four vertices bound
to the root bone and its upper four to a child bone one unit up. "Bend"
turns the child bone ninety degrees about Z over one second, linearly, so at
the end the upper half lies along -X; "Lift" steps the root up by one unit at
half a second. The material is red, metallic 0, roughness 0.5.

    py tools/make_gltf_fixture.py tests/fixtures/gltf
"""
import json
import math
import os
import struct
import sys


def main(out_dir):
    positions = []
    normals = []
    uvs = []
    joints = []
    weights = []
    # Lower half (0..1) on bone 0, upper half (1..2) on bone 1; a quad per
    # face would be nicer, but eight vertices and twelve triangles are enough
    # for a loader to be checked against.
    for y in (0.0, 1.0, 2.0):
        for x, z in ((-0.5, -0.5), (0.5, -0.5), (0.5, 0.5), (-0.5, 0.5)):
            positions.append((x, y, z))
            normals.append((0.0, 1.0, 0.0))
            uvs.append((x + 0.5, y / 2.0))
            bone = 0 if y < 1.0 else 1
            joints.append((bone, 0, 0, 0))
            weights.append((1.0, 0.0, 0.0, 0.0))
    indices = []
    for ring in (0, 4):
        for k in range(4):
            a, b = ring + k, ring + (k + 1) % 4
            c, d = a + 4, b + 4
            indices += [a, b, c, b, d, c]

    def pack(fmt, rows):
        return b"".join(struct.pack("<" + fmt * len(r), *r) for r in rows)

    blob = b""
    views = []
    accessors = []

    def add(rows, fmt, ctype, kind, target=None, minmax=False):
        nonlocal blob
        data = pack(fmt, rows)
        while len(blob) % 4:
            blob += b"\0"
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data),
                      **({"target": target} if target else {})})
        blob += data
        acc = {"bufferView": len(views) - 1, "componentType": ctype, "count": len(rows), "type": kind}
        if minmax:
            acc["min"] = [min(r[i] for r in rows) for i in range(len(rows[0]))]
            acc["max"] = [max(r[i] for r in rows) for i in range(len(rows[0]))]
        accessors.append(acc)
        return len(accessors) - 1

    a_pos = add(positions, "f", 5126, "VEC3", 34962, True)
    a_nrm = add(normals, "f", 5126, "VEC3", 34962)
    a_uv = add(uvs, "f", 5126, "VEC2", 34962)
    a_jnt = add(joints, "B", 5121, "VEC4", 34962)
    a_wgt = add(weights, "f", 5126, "VEC4", 34962)
    a_idx = add([(i,) for i in indices], "H", 5123, "SCALAR", 34963)
    # Inverse bind matrices: bone 0 at the origin, bone 1 at (0, 1, 0).
    ident = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    up1 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]
    a_ibm = add([tuple(float(v) for v in ident), tuple(float(v) for v in up1)], "f", 5126, "MAT4")
    # Bend: the child bone's rotation, 0 -> 90 degrees about Z, linear.
    half = math.sin(math.pi / 4)
    a_bend_t = add([(0.0,), (1.0,)], "f", 5126, "SCALAR", None, True)
    a_bend_q = add([(0.0, 0.0, 0.0, 1.0), (0.0, 0.0, half, half)], "f", 5126, "VEC4")
    # Lift: the root's translation stepping up at half a second.
    a_lift_t = add([(0.0,), (0.5,), (1.0,)], "f", 5126, "SCALAR", None, True)
    a_lift_v = add([(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 1.0, 0.0)], "f", 5126, "VEC3")

    doc = {
        "asset": {"version": "2.0", "generator": "ae3d tools/make_gltf_fixture.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "Root", "children": [1, 2], "translation": [0.0, 0.0, 0.0]},
            {"name": "Arm", "mesh": 0, "skin": 0},
            {"name": "Lower", "children": [3], "translation": [0.0, 0.0, 0.0]},
            {"name": "Upper", "translation": [0.0, 1.0, 0.0]},
        ],
        "meshes": [{"name": "ArmMesh", "primitives": [{
            "attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "TEXCOORD_0": a_uv,
                           "JOINTS_0": a_jnt, "WEIGHTS_0": a_wgt},
            "indices": a_idx, "material": 0, "mode": 4}]}],
        "skins": [{"name": "ArmSkin", "joints": [2, 3], "inverseBindMatrices": a_ibm, "skeleton": 2}],
        "materials": [{"name": "Red", "pbrMetallicRoughness": {
            "baseColorFactor": [0.8, 0.1, 0.1, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.5}}],
        "animations": [
            {"name": "Bend",
             "samplers": [{"input": a_bend_t, "output": a_bend_q, "interpolation": "LINEAR"}],
             "channels": [{"sampler": 0, "target": {"node": 3, "path": "rotation"}}]},
            {"name": "Lift",
             "samplers": [{"input": a_lift_t, "output": a_lift_v, "interpolation": "STEP"}],
             "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]},
        ],
        "bufferViews": views,
        "accessors": accessors,
        "buffers": [{"byteLength": len(blob)}],
    }

    os.makedirs(out_dir, exist_ok=True)
    # .gltf + .bin
    doc_file = dict(doc)
    doc_file["buffers"] = [{"byteLength": len(blob), "uri": "arm.bin"}]
    with open(os.path.join(out_dir, "arm.gltf"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc_file, f, indent=1, sort_keys=True)
        f.write("\n")
    with open(os.path.join(out_dir, "arm.bin"), "wb") as f:
        f.write(blob)
    # .glb: header, JSON chunk (space padded to 4), BIN chunk (zero padded).
    text = json.dumps(doc, sort_keys=True).encode("utf-8")
    while len(text) % 4:
        text += b" "
    body = blob
    while len(body) % 4:
        body += b"\0"
    total = 12 + 8 + len(text) + 8 + len(body)
    with open(os.path.join(out_dir, "arm.glb"), "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(text), 0x4E4F534A))
        f.write(text)
        f.write(struct.pack("<II", len(body), 0x004E4942))
        f.write(body)
    print("wrote", out_dir, "arm.gltf arm.bin arm.glb", len(blob), "bytes of buffer")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/gltf")
