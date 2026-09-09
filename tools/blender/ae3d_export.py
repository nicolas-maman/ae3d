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

Determinism is a requirement, not a nicety, and Blender does not offer it: the
same scene, generated twice, comes back with the same vertices in the same
order but a different triangulation and a different polygon order. So the
output is made a function of the geometry rather than of Blender's internal
layout. Quads split along their shorter diagonal, each triangle is rotated so
its smallest corner leads, the triangle list is sorted, and the vertex, uv and
normal tables are sorted and indexed from that. Objects are sorted by name,
floats are written to a fixed precision, JSON keys are sorted, and nothing
records a timestamp.

Two .blend files generated separately from the same script export to identical
geometry, animation and materials. The only field that moves is the source
file's own sha256, which is a fact about a file Blender cannot write
reproducibly.

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
import os
import sys

EXPORTER_VERSION = 2

# Blender is Z-up, ae3d is Y-up. Every position, normal and translation key
# goes through this, and it is the one conversion that has to be applied
# consistently or a model arrives lying on its side.
def to_y_up(x, y, z):
    return (x, z, -y)


def local_transform(obj, exported_names):
    """Where the object sits, relative to its parent, in ae3d's axes.

    The mesh is written in its own local space, so without this every static
    object arrives at the origin and a scene of thirty props is thirty props in
    one pile. Blender's matrix_local is already relative to the parent and
    already carries the parent inverse, so the decomposition is the transform
    ae3d needs and nothing has to be undone afterwards.

    A parent that is not itself exported cannot be composed onto, so the child
    keeps its world transform instead of an offset from something absent.
    """
    parent = obj.parent
    if parent is not None and parent.name in exported_names:
        matrix = obj.matrix_local
    else:
        parent = None
        matrix = obj.matrix_world

    location, rotation, scale = matrix.decompose()
    x, y, z = to_y_up(location.x, location.y, location.z)
    qx, qy, qz = to_y_up(rotation.x, rotation.y, rotation.z)
    return parent, {
        "location": [rounded(x), rounded(y), rounded(z)],
        "rotation": [rounded(qx), rounded(qy), rounded(qz), rounded(rotation.w)],
        "scale": [rounded(scale.x), rounded(scale.z), rounded(scale.y)],
    }


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


INTERPOLATION = {"CONSTANT": "STEP", "LINEAR": "LINEAR", "BEZIER": "CUBIC"}


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
                    "%s: %s uses %s interpolation, exported as CUBIC"
                    % (object_name, target, mode)
                )

    if modes == {"CONSTANT"}:
        interpolation = "STEP"
    elif modes == {"LINEAR"}:
        interpolation = "LINEAR"
    else:
        interpolation = "CUBIC"

    ordered = sorted(times)
    if interpolation == "CUBIC":
        ordered = subdivide(ordered, CUBIC_SUBDIVISIONS)
    keys = []
    for frame in ordered:
        values = []
        incoming = []
        outgoing = []
        for index in range(component_count):
            curve = curves.get(index)
            if curve is None:
                values.append(0.0)
                incoming.append(0.0)
                outgoing.append(0.0)
                continue
            values.append(curve.evaluate(frame))
            before, after = tangents_at(curve, frame, spf)
            incoming.append(before)
            outgoing.append(after)
        keys.append((frame, values, incoming, outgoing))
    return {"target": target, "interpolation": interpolation, "keys": keys, "spf": spf}


TANGENT_STEP = 0.01

# Hermite reconstructs a Bezier segment exactly only where the curve is
# uniformly parameterised in time, which Blender's auto-clamped handles are
# not. Splitting each span costs keys in the asset and nothing at run time.
CUBIC_SUBDIVISIONS = 8


def tangents_at(curve, frame, spf):
    here = curve.evaluate(frame)
    before = curve.evaluate(frame - TANGENT_STEP)
    after = curve.evaluate(frame + TANGENT_STEP)
    scale = 1.0 / (TANGENT_STEP * spf)
    return (here - before) * scale, (after - here) * scale


def subdivide(ordered, subdivisions):
    if subdivisions < 2 or len(ordered) < 2:
        return ordered
    dense = []
    for index in range(len(ordered) - 1):
        start = ordered[index]
        end = ordered[index + 1]
        for step in range(subdivisions):
            dense.append(start + (end - start) * step / float(subdivisions))
    dense.append(ordered[-1])
    return dense


SAMPLE_COUNT = 33


def sample_reference(grouped, scene, spf, origin, duration):
    """What Blender itself says the curves evaluate to, at even intervals.

    Carried in the asset so the engine's sampler can be checked against the
    thing it is meant to reproduce, rather than against another implementation
    of the same guess.
    """
    if duration <= 0.0:
        return None
    paths = {"location": "translation", "scale": "scale",
             "rotation_quaternion": "rotation", "rotation_euler": "rotation"}
    out = {}
    for path, target in paths.items():
        curves = grouped.get(path)
        if not curves or target in out:
            continue
        rows = []
        for step in range(SAMPLE_COUNT):
            t = duration * step / float(SAMPLE_COUNT - 1)
            frame = origin + t / spf
            if path == "rotation_euler":
                import mathutils
                angles = [curves[i].evaluate(frame) if i in curves else 0.0 for i in range(3)]
                mode = "XYZ"
                q = mathutils.Euler(tuple(angles), mode).to_quaternion()
                qx, qy, qz = to_y_up(q.x, q.y, q.z)
                rows.append({"t": rounded(t), "v": [rounded(qx), rounded(qy), rounded(qz), rounded(q.w)]})
            elif path == "rotation_quaternion":
                w, x, y, z = [curves[i].evaluate(frame) if i in curves else 0.0 for i in range(4)]
                qx, qy, qz = to_y_up(x, y, z)
                rows.append({"t": rounded(t), "v": [rounded(qx), rounded(qy), rounded(qz), rounded(w)]})
            else:
                a, b, c = [curves[i].evaluate(frame) if i in curves else 0.0 for i in range(3)]
                if path == "scale":
                    vx, vy, vz = a, c, b
                else:
                    vx, vy, vz = to_y_up(a, b, c)
                rows.append({"t": rounded(t), "v": [rounded(vx), rounded(vy), rounded(vz)]})
        out[target] = rows
    return out or None


def euler_quaternion(curves, frame, mode):
    import mathutils
    angles = [curves[i].evaluate(frame) if i in curves else 0.0 for i in range(3)]
    q = mathutils.Euler(tuple(angles), mode).to_quaternion()
    return [q.x, q.y, q.z, q.w]


def align(q, reference):
    """q and -q are the same rotation; a finite difference across a sign flip is
    not a tangent but a leap across the hypersphere."""
    if sum(q[i] * reference[i] for i in range(4)) < 0.0:
        return [-value for value in q]
    return q


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
            for frame, values, incoming, outgoing in built["keys"]:
                x, y, z = to_y_up(values[0], values[1], values[2])
                ix, iy, iz = to_y_up(incoming[0], incoming[1], incoming[2])
                ox, oy, oz = to_y_up(outgoing[0], outgoing[1], outgoing[2])
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(x), rounded(y), rounded(z)],
                             "in": [rounded(ix), rounded(iy), rounded(iz)],
                             "out": [rounded(ox), rounded(oy), rounded(oz)]})
            channels.append({"target": "translation",
                             "interpolation": built["interpolation"],
                             "keys": keys})

    scale = grouped.get("scale")
    if scale:
        built = channel_from_fcurves("scale", scale, spf, 3, warnings, obj.name)
        if built:
            keys = []
            for frame, values, incoming, outgoing in built["keys"]:
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(values[0]), rounded(values[2]), rounded(values[1])],
                             "in": [rounded(incoming[0]), rounded(incoming[2]), rounded(incoming[1])],
                             "out": [rounded(outgoing[0]), rounded(outgoing[2]), rounded(outgoing[1])]})
            channels.append({"target": "scale",
                             "interpolation": built["interpolation"],
                             "keys": keys})

    quat = grouped.get("rotation_quaternion")
    euler = grouped.get("rotation_euler")
    if quat:
        built = channel_from_fcurves("rotation", quat, spf, 4, warnings, obj.name)
        if built:
            keys = []
            for frame, values, incoming, outgoing in built["keys"]:
                w, x, y, z = values
                qx, qy, qz = to_y_up(x, y, z)
                iw, ix, iy, iz = incoming
                ow, ox, oy, oz = outgoing
                qix, qiy, qiz = to_y_up(ix, iy, iz)
                qox, qoy, qoz = to_y_up(ox, oy, oz)
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(qx), rounded(qy), rounded(qz), rounded(w)],
                             "in": [rounded(qix), rounded(qiy), rounded(qiz), rounded(iw)],
                             "out": [rounded(qox), rounded(qoy), rounded(qoz), rounded(ow)]})
            channels.append({"target": "rotation",
                             "interpolation": built["interpolation"], "keys": keys})
    elif euler:
        built = channel_from_fcurves("rotation", euler, spf, 3, warnings, obj.name)
        if built:
            import mathutils
            keys = []
            mode = obj.rotation_mode if obj.rotation_mode in {
                "XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX"} else "XYZ"
            step = 0.01
            for frame, values, _incoming, _outgoing in built["keys"]:
                here = euler_quaternion(euler, frame, mode)
                before = align(euler_quaternion(euler, frame - step, mode), here)
                after = align(euler_quaternion(euler, frame + step, mode), here)
                scale_factor = 1.0 / (step * spf)
                incoming = [(here[i] - before[i]) * scale_factor for i in range(4)]
                outgoing = [(after[i] - here[i]) * scale_factor for i in range(4)]
                qx, qy, qz = to_y_up(here[0], here[1], here[2])
                ix, iy, iz = to_y_up(incoming[0], incoming[1], incoming[2])
                ox, oy, oz = to_y_up(outgoing[0], outgoing[1], outgoing[2])
                keys.append({"t": rounded((frame - origin) * spf),
                             "v": [rounded(qx), rounded(qy), rounded(qz), rounded(here[3])],
                             "in": [rounded(ix), rounded(iy), rounded(iz), rounded(incoming[3])],
                             "out": [rounded(ox), rounded(oy), rounded(oz), rounded(outgoing[3])]})
            channels.append({"target": "rotation",
                             "interpolation": built["interpolation"], "keys": keys})

    if not channels:
        return None

    duration = 0.0
    for channel in channels:
        for key in channel["keys"]:
            duration = max(duration, key["t"])

    clip = {"name": action.name, "duration": rounded(duration), "channels": channels}
    reference = sample_reference(grouped, scene, spf, origin, duration)
    if reference:
        clip["reference"] = reference
    return clip


def canonical_winding(corners):
    """The same triangle written the same way, whichever corner Blender began at.

    Rotated so the smallest corner leads. Rotation is cyclic, so the winding --
    and therefore the facing -- is unchanged.
    """
    first = min(range(3), key=lambda i: corners[i])
    return tuple(corners[(first + i) % 3] for i in range(3))


def triangulate(mesh, warnings, object_name):
    """Loop-index triangles, chosen the same way on every run.

    Blender's own calc_loop_triangles picks a different diagonal for the same
    quad between processes: the vertex data is identical run to run and the
    triangulation is not, which is enough to make an export unreproducible.
    Vertices and loops are stable, so the split is decided here from geometry
    alone.

    A quad splits along its shorter diagonal, which is also the better-shaped
    of the two. Larger polygons fan from their first loop, correct while the
    polygon is convex; one that is not says so rather than exporting a fold
    nobody would see until it was rendered.
    """
    out = []
    for polygon in mesh.polygons:
        loops = list(polygon.loop_indices)
        count = len(loops)
        if count < 3:
            continue
        if count == 3:
            out.append((loops[0], loops[1], loops[2]))
            continue
        if count == 4:
            a, b, c, d = (mesh.vertices[mesh.loops[i].vertex_index].co for i in loops)
            if (a - c).length_squared <= (b - d).length_squared:
                out.append((loops[0], loops[1], loops[2]))
                out.append((loops[0], loops[2], loops[3]))
            else:
                out.append((loops[1], loops[2], loops[3]))
                out.append((loops[1], loops[3], loops[0]))
            continue
        if not is_convex(mesh, loops, polygon.normal):
            warnings.append(
                "%s: a %d-sided face is not convex and was fanned; triangulate it "
                "in Blender for an exact result" % (object_name, count)
            )
        for index in range(1, count - 1):
            out.append((loops[0], loops[index], loops[index + 1]))
    return out


def is_convex(mesh, loops, normal):
    count = len(loops)
    for index in range(count):
        a = mesh.vertices[mesh.loops[loops[index]].vertex_index].co
        b = mesh.vertices[mesh.loops[loops[(index + 1) % count]].vertex_index].co
        c = mesh.vertices[mesh.loops[loops[(index + 2) % count]].vertex_index].co
        if (b - a).cross(c - b).dot(normal) < 0.0:
            return False
    return True


def write_obj(obj, depsgraph, path, material_name, warnings):
    """Triangulated geometry, in the OBJ ae3d's loader already reads."""
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()
    try:
        uv_layer = mesh.uv_layers.active.data if mesh.uv_layers.active else None

        corner_triangles = []
        for triangle in triangulate(mesh, warnings, obj.name):
            corners = []
            for loop_index in triangle:
                vertex = mesh.vertices[mesh.loops[loop_index].vertex_index]
                px, py, pz = to_y_up(*vertex.co)
                nx, ny, nz = to_y_up(*mesh.loops[loop_index].normal)
                uv = uv_layer[loop_index].uv if uv_layer else (0.0, 0.0)
                corners.append((
                    (rounded(px), rounded(py), rounded(pz)),
                    (rounded(uv[0]), rounded(uv[1])),
                    (rounded(nx), rounded(ny), rounded(nz)),
                ))
            corner_triangles.append(canonical_winding(corners))
        corner_triangles.sort()

        positions = sorted({corner[0] for tri in corner_triangles for corner in tri})
        uvs = sorted({corner[1] for tri in corner_triangles for corner in tri})
        normals = sorted({corner[2] for tri in corner_triangles for corner in tri})
        position_index = {key: n + 1 for n, key in enumerate(positions)}
        uv_index = {key: n + 1 for n, key in enumerate(uvs)}
        normal_index = {key: n + 1 for n, key in enumerate(normals)}

        faces = [
            [(position_index[c[0]], uv_index[c[1]], normal_index[c[2]]) for c in tri]
            for tri in corner_triangles
        ]

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


def linked_image(socket):
    if not socket or not socket.is_linked:
        return None
    node = socket.links[0].from_node
    if node.type != "TEX_IMAGE" or not node.image:
        return None
    return node.image


def principled_surface(material):
    """Base colour, metallic, roughness and the base-colour image, if any."""
    surface = {"diffuse": (0.8, 0.8, 0.8), "metallic": 0.0,
               "roughness": 0.5, "texture": None}
    if not material:
        return surface
    if not getattr(material, "node_tree", None):
        surface["diffuse"] = tuple(material.diffuse_color[:3])
        return surface

    for node in material.node_tree.nodes:
        if node.type != "BSDF_PRINCIPLED":
            continue
        base = node.inputs.get("Base Color")
        if base is not None:
            surface["diffuse"] = tuple(base.default_value[:3])
            surface["texture"] = linked_image(base)
        for name, key in (("Metallic", "metallic"), ("Roughness", "roughness")):
            socket = node.inputs.get(name)
            if socket is not None:
                surface[key] = float(socket.default_value)
        break
    return surface


def write_mtl(material, path, out_directory):
    surface = principled_surface(material)

    lines = ["# exported by ae3d_export.py v%d" % EXPORTER_VERSION,
             "newmtl %s" % material.name,
             "Kd %.6f %.6f %.6f" % tuple(rounded(c) for c in surface["diffuse"]),
             "Ka 0.000000 0.000000 0.000000",
             "Ks 0.500000 0.500000 0.500000",
             "Ns 50.000000",
             "Pm %.6f" % rounded(surface["metallic"]),
             "Pr %.6f" % rounded(surface["roughness"])]

    texture = surface["texture"]
    written_texture = None
    if texture is not None:
        written_texture = export_image(texture, out_directory)
        if written_texture:
            lines.append("map_Kd %s" % written_texture)

    with open(path, "w", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    return written_texture


def export_image(image, out_directory):
    """Copies the image beside the material under a name the MTL can name.

    Packed and generated images are written out; a file on disk is copied. An
    image that cannot be resolved is skipped rather than named, because an MTL
    pointing at a file that is not there loads as an untextured material with
    no explanation.
    """
    name = os.path.basename(image.filepath_from_user()) if image.filepath else ""
    if not name:
        name = "%s.png" % image.name
    if not os.path.splitext(name)[1]:
        name += ".png"
    destination = os.path.join(out_directory, name)

    try:
        if image.packed_file or not image.filepath:
            image.file_format = "PNG"
            image.save_render(destination)
        else:
            source = bpy.path.abspath(image.filepath_from_user())
            if not os.path.exists(source):
                return None
            if os.path.abspath(source) != os.path.abspath(destination):
                with open(source, "rb") as src, open(destination, "wb") as dst:
                    dst.write(src.read())
    except RuntimeError:
        return None
    return os.path.basename(destination)


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
    # Exporting one object exports one object: nothing else is there to be a
    # parent of it.
    if args.only:
        meshes = [o for o in meshes if o.name == args.only]
    exported_names = {o.name for o in meshes}

    for obj in meshes:
        stem = obj.name
        obj_path = os.path.join(args.out, stem + ".obj")
        material = obj.data.materials[0] if obj.data.materials else None
        material_name = material.name if material else None

        vertices, triangles = write_obj(obj, depsgraph, obj_path, material_name, warnings)

        files = {"mesh": os.path.basename(obj_path)}
        if material:
            mtl_path = os.path.join(args.out, stem + ".mtl")
            texture = write_mtl(material, mtl_path, args.out)
            files["material"] = os.path.basename(mtl_path)
            if texture:
                files["texture"] = texture

        clip = build_clip(obj, scene, warnings)
        if clip:
            anim_path = os.path.join(args.out, stem + ".anim.json")
            with open(anim_path, "w", newline="\n") as handle:
                json.dump({"clips": [clip]}, handle, indent=2, sort_keys=True)
                handle.write("\n")
            files["animation"] = os.path.basename(anim_path)

        parent, transform = local_transform(obj, exported_names)

        records.append({
            "id": asset_id(source_name, obj.name),
            "object": obj.name,
            "parent": parent.name if parent else "",
            "transform": transform,
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
