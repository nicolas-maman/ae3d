"""Look for surfaces that would fight over the same depth, in the geometry.

Z-fighting is two faces in one plane, close enough in depth that rounding
decides which is in front. It is usually looked for on screen, where it shows
as a shimmer that depends on where the camera happens to be; here it is looked
for in the scene, where either two faces share a plane and overlap or they do
not.

    python tools/blender/check_coplanar.py resources/blender/zombie_street

Every exported object is read back with the transform the manifest recorded,
which is what the loader will place it with. Rotated objects are reported and
skipped: their faces are not axis aligned, so this test does not apply to them
and saying so is better than passing them silently.
"""

import json
import os
import sys

TOLERANCE = 1e-4
OVERLAP = 1e-3


def load_obj(path):
    positions = []
    faces = []
    with open(path) as handle:
        for line in handle:
            if line.startswith("v "):
                positions.append(tuple(float(v) for v in line.split()[1:4]))
            elif line.startswith("f "):
                faces.append([int(c.split("/")[0]) - 1 for c in line.split()[1:]])
    return positions, faces


def is_identity(rotation):
    x, y, z, w = rotation
    return abs(x) < TOLERANCE and abs(y) < TOLERANCE and abs(z) < TOLERANCE \
        and abs(abs(w) - 1.0) < TOLERANCE


def world_faces(positions, faces, entry):
    """Each face as (axis, plane, box), for the faces that are axis aligned."""
    location = entry["transform"]["location"]
    scale = entry["transform"]["scale"]
    out = []
    for face in faces:
        corners = [tuple(positions[i][a] * scale[a] + location[a] for a in range(3))
                   for i in face]
        for axis in range(3):
            values = [c[axis] for c in corners]
            if max(values) - min(values) > TOLERANCE:
                continue
            others = [a for a in range(3) if a != axis]
            box = tuple((min(c[a] for c in corners), max(c[a] for c in corners))
                        for a in others)
            if all(hi - lo > OVERLAP for lo, hi in box):
                out.append((axis, values[0], box))
            break
    return out


def overlaps(a, b):
    for (a_lo, a_hi), (b_lo, b_hi) in zip(a, b):
        if min(a_hi, b_hi) - max(a_lo, b_lo) <= OVERLAP:
            return False
    return True


def main(argv):
    if not argv:
        print("usage: check_coplanar.py <exported directory>")
        return 2
    directory = argv[0]
    with open(os.path.join(directory, "manifest.json")) as handle:
        manifest = json.load(handle)

    surfaces = {}
    rotated = []
    for entry in manifest["objects"]:
        if not is_identity(entry["transform"]["rotation"]):
            rotated.append(entry["object"])
            continue
        if entry.get("parent"):
            rotated.append(entry["object"])
            continue
        positions, faces = load_obj(os.path.join(directory, entry["files"]["mesh"]))
        surfaces[entry["object"]] = world_faces(positions, faces, entry)

    names = sorted(surfaces)
    clashes = []
    for i, first in enumerate(names):
        for second in names[i + 1:]:
            for axis_a, plane_a, box_a in surfaces[first]:
                for axis_b, plane_b, box_b in surfaces[second]:
                    if axis_a != axis_b:
                        continue
                    if abs(plane_a - plane_b) > TOLERANCE:
                        continue
                    if overlaps(box_a, box_b):
                        clashes.append((first, second, "xyz"[axis_a], plane_a))
                        break
                else:
                    continue
                break

    print("check_coplanar: %d objects checked, %d skipped as rotated or parented"
          % (len(surfaces), len(rotated)))
    for first, second, axis, plane in clashes:
        print("  %s and %s share the plane %s = %.4f and overlap there"
              % (first, second, axis, plane))
    if clashes:
        print("check_coplanar: %d coplanar overlap(s)" % len(clashes))
        return 1
    print("check_coplanar: no two faces share a plane")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
