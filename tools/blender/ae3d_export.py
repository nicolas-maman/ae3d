"""Export a .blend to ae3d assets, from the command line, reproducibly.

    blender --background <file.blend> --python tools/blender/ae3d_export.py -- --out build/assets

Writes, per exported object:

    <name>.obj        geometry, triangulated, with normals and UVs
    <name>.mtl        the material, when the object has one
    <name>.anim.json  clips, channels and keyframes, when the object is animated

and one manifest.json for the lot. The manifest is the first link in the chain
#181 has to be able to follow: it records what came from where, with a hash of
the source and a stable id per object, so a mesh that arrives wrong in the
engine can be traced back to the object it was exported from.

Determinism is a requirement, not a nicety. Exporting the same .blend twice
gives byte-identical output, so a diff means something changed rather than
that the exporter ran again: objects are sorted by name, floats are formatted
to a fixed precision, JSON keys are sorted, and nothing records a timestamp.

## The Blender 5 action API

`Action.fcurves` does not exist in Blender 5.2. It was removed, not deprecated,
and the layered replacement is the only way in:

    action.layers[] -> strips[] -> channelbags[] -> fcurves[]

Anything written against `action.fcurves` -- which is most of what exists --
raises AttributeError here. `iter_fcurves` below handles both, because the
legacy path still matters for older Blenders and costs three lines.
"""

import bpy
import argparse
import hashlib
import json
import math
import os
import sys

EXPORTER_VERSION = 1

# Blender is Z-up, ae3d is Y-up. Every position, normal and translation key
# goes through this, and it is the one conversion that has to be applied
# consistently or a model arrives lying on its side.
def to_y_up(x, y, z):
    return (x, z, -y)


def rounded(value, digits=6):
    """A float that formats the same way on every machine.

    -0.0 is folded to 0.0: it compares equal to 0.0 but writes differently,
    which would make a byte-identical export depend on which side of zero a
    rounding landed.
    """
    out = round(float(value), digits)
    if out == 0.0:
        return 0.0
    return out


def iter_fcurves(action):
    """Every fcurve in an action, on Blender 5 or earlier.

    Blender 5.2 removed Action.fcurves; the layered form is the only one it
    has. Older Blenders have only the flat form. Trying the flat one first
    would raise on 5.x, so the layered path is tried first.
    """
    layers = getattr(action, "layers", None)
    if layers:
        for layer in layers:
            for strip in layer.strips:
                for bag in getattr(strip, "channelbags", []):
                    for fcurve in bag.fcurves:
                        yield fcurve
        return
    for fcurve in getattr(action, "fcurves", []):
        yield fcurve


# Blender keyframes are in frames; ae3d clips are in seconds.
def seconds_per_frame(scene):
    fps = scene.render.fps
    base = scene.render.fps_base or 1.0
    return base / float(fps)


# Blenders count from frame 1, and scenes need not even start there. A clip
# whose first key sits at t=0.0417 because that is where frame 1 lands is a
# clip whose duration is 1.0417 seconds for one second of motion, and whose
# start a caller has to know to seek to. Times are measured from the scene's
# own start instead, so a clip begins at zero.
def frame_origin(scene):
    return scene.frame_start


# CONSTANT and LINEAR map straight across. BEZIER does not: ae3d has STEP and
# LINEAR only (#192), so a Bezier curve is resampled and the manifest says so
# rather than quietly losing the easing.
INTERPOLATION = {"CONSTANT": "STEP", "LINEAR": "LINEAR", "BEZIER": "LINEAR"}


def channel_from_fcurves(target, curves, spf, component_count, warnings, object_name):
    """One ae3d channel from the per-component fcurves Blender stores.

    Blender keeps one fcurve per component -- location[0], location[1],
    location[2] -- and ae3d wants one channel with a vector per key. The
    components need not share keyframe times, so the union of their times is
    taken and each curve is evaluated at all of them. Sampling only the times
    one component happens to have would drop the others' motion.
    """
    times = set()
    for curve in curves.values():
        for key in curve.keyframe_points:
            times.add(round(key.co[0], 6))
    if not times:
        return None

    modes = set()
    for curve in curves.values():
        for key in curve.keyframe_points:
            mode = key.interpolation
            modes.add(mode)
            if mode not in INTERPOLATION:
                warnings.append(
                    "%s: %s uses %s interpolation, exported as LINEAR"
                    % (object_name, target, mode)
                )
    if "BEZIER" in modes:
        warnings.append(
            "%s: %s was keyed with Bezier easing and ae3d has no cubic sampler; "
            "exported as LINEAR (see ae3d#192)" % (object_name, target)
        )

    # One mode for the channel. Mixed modes within a channel cannot be
    # represented, so the least lossy wins and the warning above already said
    # what happened.
    interpolation = "LINEAR"
    if modes == {"CONSTANT"}:
        interpolation = "STEP"

    keys = []
    for frame in sorted(times):
        values = []
        for index in range(component_count):
            curve = curves.get(index)
            values.append(curve.evaluate(frame) if curve else 0.0)
        keys.append((frame, values))
    return {"target": target, "interpolation": interpolation, "keys": keys, "spf": spf}


def build_clip(obj, scene, warnings):
    """The object's action as an ae3d clip, or None when it has no animation."""
    anim = obj.animation_data
    if not anim or not anim.action:
        return None
    action = anim.action
    spf = seconds_per_frame(scene)
    origin = frame_origin(scene)

    grouped = {}
    for fcurve in iter_fcurves(action):
        grouped.setdefault(fcurve.data_path, {})[fcurve.array_index] = fcurve

    channels = []

    location = grouped.get("location")
    if location:
        built = channel_from_fcurves("translation", location, spf, 3, warnings, obj.name)
        if built:
            keys = []
            for frame, values in built["keys"]:
                x, y, z = to_y_up(values[0], values[1], values[2])
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(x), rounded(y), rounded(z)]})
            channels.append({"target": "translation",
                             "interpolation": built["interpolation"],
                             "keys": keys})

    scale = grouped.get("scale")
    if scale:
        built = channel_from_fcurves("scale", scale, spf, 3, warnings, obj.name)
        if built:
            keys = []
            for frame, values in built["keys"]:
                # Scale is a magnitude per axis, so it is reordered like a
                # position but never negated: the -y of the axis change would
                # turn a scale into a mirror.
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(values[0]), rounded(values[2]),
                                   rounded(values[1])]})
            channels.append({"target": "scale",
                             "interpolation": built["interpolation"],
                             "keys": keys})

    quat = grouped.get("rotation_quaternion")
    euler = grouped.get("rotation_euler")
    if quat:
        built = channel_from_fcurves("rotation", quat, spf, 4, warnings, obj.name)
        if built:
            keys = []
            for frame, values in built["keys"]:
                # Blender stores quaternions w-first.
                w, x, y, z = values
                qx, qy, qz = to_y_up(x, y, z)
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(qx), rounded(qy), rounded(qz), rounded(w)]})
            channels.append({"target": "rotation",
                             "interpolation": built["interpolation"], "keys": keys})
    elif euler:
        built = channel_from_fcurves("rotation", euler, spf, 3, warnings, obj.name)
        if built:
            import mathutils
            keys = []
            mode = obj.rotation_mode if obj.rotation_mode in {
                "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX"} else "XYZ"
            for frame, values in built["keys"]:
                q = mathutils.Euler((values[0], values[1], values[2]), mode).to_quaternion()
                qx, qy, qz = to_y_up(q.x, q.y, q.z)
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(qx), rounded(qy), rounded(qz), rounded(q.w)]})
            channels.append({"target": "rotation",
                             "interpolation": built["interpolation"], "keys": keys})

    if not channels:
        return None

    duration = 0.0
    for channel in channels:
        for key in channel["keys"]:
            duration = max(duration, key["t"])
    return {"name": action.name, "duration": rounded(duration), "channels": channels}


def write_obj(obj, depsgraph, path, material_name):
    """Triangulated geometry, in the OBJ ae3d's loader already reads."""
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        mesh.calc_loop_triangles()
        uv_layer = mesh.uv_layers.active.data if mesh.uv_layers.active else None

        # Positions, normals and UVs are deduplicated by value so the file does
        # not repeat a vertex once per triangle that touches it.
        positions, normals, uvs = [], [], []
        position_index, normal_index, uv_index = {}, {}, {}
        faces = []

        def intern(store, index_of, key):
            found = index_of.get(key)
            if found is None:
                store.append(key)
                found = len(store)
                index_of[key] = found
            return found

        for tri in mesh.loop_triangles:
            corners = []
            for loop_index, vertex_index in zip(tri.loops, tri.vertices):
                vertex = mesh.vertices[vertex_index]
                px, py, pz = to_y_up(*vertex.co)
                nx, ny, nz = to_y_up(*mesh.loops[loop_index].normal)
                p = intern(positions, position_index,
                           (rounded(px), rounded(py), rounded(pz)))
                n = intern(normals, normal_index,
                           (rounded(nx), rounded(ny), rounded(nz)))
                if uv_layer:
                    uv = uv_layer[loop_index].uv
                    t = intern(uvs, uv_index, (rounded(uv[0]), rounded(uv[1])))
                else:
                    t = intern(uvs, uv_index, (0.0, 0.0))
                corners.append((p, t, n))
            faces.append(corners)

        lines = ["# exported by ae3d_export.py v%d" % EXPORTER_VERSION,
                 "# object: %s" % obj.name]
        if material_name:
            lines.append("mtllib %s.mtl" % os.path.splitext(os.path.basename(path))[0])
        for v in positions:
            lines.append("v %.6f %.6f %.6f" % v)
        for t in uvs:
            lines.append("vt %.6f %.6f" % t)
        for n in normals:
            lines.append("vn %.6f %.6f %.6f" % n)
        if material_name:
            lines.append("usemtl %s" % material_name)
        for corners in faces:
            lines.append("f " + " ".join("%d/%d/%d" % c for c in corners))

        with open(path, "w", newline="\n") as handle:
            handle.write("\n".join(lines) + "\n")
        return len(positions), len(faces)
    finally:
        evaluated.to_mesh_clear()


def write_mtl(material, path):
    diffuse = (0.8, 0.8, 0.8)
    if material and material.use_nodes:
        for node in material.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                base = node.inputs.get("Base Color")
                if base is not None:
                    diffuse = tuple(base.default_value[:3])
                break
    elif material:
        diffuse = tuple(material.diffuse_color[:3])

    lines = ["# exported by ae3d_export.py v%d" % EXPORTER_VERSION,
             "newmtl %s" % material.name,
             "Kd %.6f %.6f %.6f" % tuple(rounded(c) for c in diffuse),
             "Ka 0.000000 0.000000 0.000000",
             "Ks 0.500000 0.500000 0.500000",
             "Ns 50.000000"]
    with open(path, "w", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def file_hash(path):
    if not path or not os.path.exists(path):
        return None
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
    return digest.hexdigest()


def asset_id(source_name, object_name):
    """A name for this object that survives every stage of the pipeline.

    Derived from the source file and the object rather than from anything about
    the export, so re-exporting produces the same id and a model in the engine
    can be matched back to the object it came from.
    """
    return hashlib.sha1(("%s::%s" % (source_name, object_name)).encode("utf-8")).hexdigest()[:16]


def main(argv):
    parser = argparse.ArgumentParser(prog="ae3d_export")
    parser.add_argument("--out", required=True, help="directory to write assets into")
    parser.add_argument("--only", default=None,
                        help="export just this object, by name")
    args = parser.parse_args(argv)

    scene = bpy.context.scene
    depsgraph = bpy.context.evaluated_depsgraph_get()
    os.makedirs(args.out, exist_ok=True)

    source_path = bpy.data.filepath
    source_name = os.path.basename(source_path) if source_path else "untitled.blend"

    warnings = []
    records = []

    # Sorted, so the manifest is the same on every run and on every machine.
    meshes = sorted((o for o in scene.objects if o.type == "MESH"), key=lambda o: o.name)
    for obj in meshes:
        if args.only and obj.name != args.only:
            continue

        stem = obj.name
        obj_path = os.path.join(args.out, stem + ".obj")
        material = obj.data.materials[0] if obj.data.materials else None
        material_name = material.name if material else None

        vertices, triangles = write_obj(obj, depsgraph, obj_path, material_name)

        files = {"mesh": os.path.basename(obj_path)}
        if material:
            mtl_path = os.path.join(args.out, stem + ".mtl")
            write_mtl(material, mtl_path)
            files["material"] = os.path.basename(mtl_path)

        clip = build_clip(obj, scene, warnings)
        if clip:
            anim_path = os.path.join(args.out, stem + ".anim.json")
            with open(anim_path, "w", newline="\n") as handle:
                json.dump({"clips": [clip]}, handle, indent=2, sort_keys=True)
                handle.write("\n")
            files["animation"] = os.path.basename(anim_path)

        records.append({
            "id": asset_id(source_name, obj.name),
            "object": obj.name,
            "files": files,
            "vertices": vertices,
            "triangles": triangles,
            "clips": [clip["name"]] if clip else [],
            "channels": len(clip["channels"]) if clip else 0,
        })

    manifest = {
        "exporter_version": EXPORTER_VERSION,
        "blender": bpy.app.version_string,
        "source": {"name": source_name, "sha256": file_hash(source_path)},
        "scene": {"fps": scene.render.fps, "fps_base": scene.render.fps_base},
        "up_axis": "Y",
        "objects": records,
        "warnings": warnings,
    }
    manifest_path = os.path.join(args.out, "manifest.json")
    with open(manifest_path, "w", newline="\n") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
        handle.write("\n")

    print("ae3d_export: %d object(s) -> %s" % (len(records), args.out))
    for warning in warnings:
        print("ae3d_export: warning: %s" % warning)
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
