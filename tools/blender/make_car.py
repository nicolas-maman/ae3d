"""Build the car the street is driven in, as a .blend the exporter reads.

A saloon at the scale of the street's own physics (examples/street_drive.ae:
a body 4.1 m long and 1.72 m wide, its floor 0.30 m under the chassis
frame, wheels of 0.36 m on 0.24 m tyres at the axles the car drives on):
the body lofted from a side profile, narrowing toward the roof, the cabin
glass and the roof as parts of their own so each carries its material
whole, four wheels each with a hub, and the lamps as emissive blocks that
the game lights the road from. Everything is a part under the body or a
wheel, named Car_<part>, so the game assembles it by name:

    blender --background --factory-startup --python tools/blender/make_car.py -- --out resources/blender/car.blend
    ./scripts/export_assets.sh resources/blender/car.blend resources/blender/car

The engine is Y-up and the exporter maps Blender's (x, y, z) to (x, z, -y),
so the car is built here with +Y forward, which the game sees as -Z, the
way its chassis frame has always faced.
"""
import bpy
import bmesh
import mathutils
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zombie_street_textures as textures  # noqa: E402
from make_zombie_street import material, block, project_uvs  # noqa: E402

# The chassis frame: the body's floor and beltline, its length and width,
# where the axles are and how big the wheels. The same numbers as
# examples/street_drive.ae, so the export fits the physics without a scale.
FLOOR = -0.30
BELT = 0.32
ROOF = 0.98
HALF_LENGTH = 2.05
HALF_WIDTH = 0.86
ROOF_HALF_WIDTH = 0.74
AXLE_X = 0.92
AXLE_Z = -0.28
FRONT_Y = 1.35
REAR_Y = -1.35
WHEEL_RADIUS = 0.36
WHEEL_WIDTH = 0.24


def loft(name, profile, half_lo, half_hi, z_lo, z_hi, surface, repeats=0.5,
         bevel=0.0, caps=True, skip=()):
    """A closed (y, z) profile extruded across x, the half-width running
    from half_lo at z_lo to half_hi at z_hi so a body narrows toward its
    roof. `skip` names the profile edges (by the index of their first
    point) left open, and caps=False leaves the two sides open: the cabin
    glass is the walls of its loft and nothing else, since the roof is a
    part of its own and the beltline is inside the car."""
    bm = bmesh.new()

    def width(z):
        if z_hi <= z_lo:
            return half_lo
        t = min(1.0, max(0.0, (z - z_lo) / (z_hi - z_lo)))
        return half_lo + (half_hi - half_lo) * t

    left = [bm.verts.new((-width(z), y, z)) for (y, z) in profile]
    right = [bm.verts.new((width(z), y, z)) for (y, z) in profile]
    if caps:
        bm.faces.new(left)
        bm.faces.new(right)
    n = len(profile)
    for i in range(n):
        if i in skip:
            continue
        j = (i + 1) % n
        bm.faces.new([left[i], left[j], right[j], right[i]])
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.normal_update()
    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.verts) + list(bm.edges), offset=bevel,
                        segments=2, profile=0.5, affect="EDGES", clamp_overlap=True)
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        bm.normal_update()
    project_uvs(bm, repeats)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(surface)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def cylinder(name, radius, depth, surface, repeats, segments=24):
    """A cylinder along x: a wheel on its axle."""
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
                          radius1=radius, radius2=radius, depth=depth)
    # create_cone stands along z; the axle lies along x.
    bmesh.ops.rotate(bm, verts=list(bm.verts), cent=(0.0, 0.0, 0.0),
                     matrix=mathutils.Matrix.Rotation(math.pi * 0.5, 3, "Y"))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.normal_update()
    for face in bm.faces:
        # The tread is round: smooth around, flat on the caps.
        face.smooth = abs(face.normal.x) < 0.5
    project_uvs(bm, repeats)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(surface)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def build_car(surfaces):
    parts = []
    # The body: the shell from the floor to the beltline, a bumper's lip
    # at each end, the bonnet falling away to the nose and the boot to the
    # tail. Counter-clockwise seen from +x (y to the right, z up).
    body_profile = [
        (-HALF_LENGTH, FLOOR), (HALF_LENGTH, FLOOR),
        (HALF_LENGTH, 0.04), (1.97, 0.18), (1.82, 0.26), (0.72, BELT),
        (-1.62, BELT), (-1.98, 0.30), (-HALF_LENGTH, 0.12),
    ]
    body = loft("Car_Body", body_profile, HALF_WIDTH, HALF_WIDTH, FLOOR, BELT,
                surfaces["paint"], repeats=0.9, bevel=0.03)
    parts.append(body)

    # The cabin's glass: the windscreen raked back, the rear window steeper,
    # the side windows as the loft's caps; open at the top for the roof and
    # at the bottom where it meets the body.
    glass_profile = [(0.72, BELT), (0.10, 0.94), (-1.05, 0.94), (-1.62, BELT)]
    glass = loft("Car_Glass", glass_profile, HALF_WIDTH, ROOF_HALF_WIDTH, BELT, 0.94,
                 surfaces["glass"], repeats=1.7, skip=(1, 3))
    glass.parent = body
    parts.append(glass)

    # The roof: a slab over the glass, the paint's.
    roof = block("Car_Roof", (-ROOF_HALF_WIDTH, -1.05, 0.94), (ROOF_HALF_WIDTH, 0.10, ROOF),
                 surfaces["paint"], repeats=0.9, bevel=0.02)
    roof.parent = body
    parts.append(roof)

    # The lamps: a pair at the nose that glow the way the street's lamps
    # do (the game hangs its spot lights on the car where these are), a
    # pair at the tail in red.
    for side, sign in (("L", -1.0), ("R", 1.0)):
        head = block("Car_Lamp_%s" % side, (-0.20, -0.02, -0.08), (0.20, 0.02, 0.08),
                     surfaces["lamp"], repeats=1.0)
        head.parent = body
        head.location = (sign * 0.58, HALF_LENGTH + 0.005, 0.12)
        parts.append(head)
        tail = block("Car_Tail_%s" % side, (-0.18, -0.02, -0.05), (0.18, 0.02, 0.05),
                     surfaces["tail"], repeats=1.0)
        tail.parent = body
        tail.location = (sign * 0.60, -HALF_LENGTH - 0.005, 0.20)
        parts.append(tail)

    # The wheels: a tyre with a hub in it, one at each axle end. Each is a
    # body of its own in the game, so none is a child of the car.
    for name, x, y in (("FL", -AXLE_X, FRONT_Y), ("FR", AXLE_X, FRONT_Y),
                       ("RL", -AXLE_X, REAR_Y), ("RR", AXLE_X, REAR_Y)):
        tyre = cylinder("Car_Wheel_%s" % name, WHEEL_RADIUS, WHEEL_WIDTH, surfaces["tyre"], 1.4)
        tyre.location = (x, y, AXLE_Z)
        parts.append(tyre)
        hub = cylinder("Car_Hub_%s" % name, WHEEL_RADIUS * 0.62, WHEEL_WIDTH + 0.02,
                       surfaces["metal"], 1.4, segments=12)
        hub.parent = tyre
        parts.append(hub)
    return parts


def main(argv):
    parser = argparse.ArgumentParser(prog="make_car")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    surfaces = {
        "paint": material("CarPaint", textures.car_paint(), roughness=0.28, metallic=0.35,
                          normal=textures.car_paint_normal()),
        "glass": material("CarGlass", textures.glass_dark(), roughness=0.12, metallic=0.1),
        "tyre": material("CarTyre", textures.rubber(), roughness=0.92,
                         normal=textures.rubber_normal()),
        "metal": material("CarHub", textures.metal(), roughness=0.35, metallic=0.85),
        "lamp": material("CarLamp", None, emission=(1.0, 0.92, 0.72)),
        "tail": material("CarTail", None, emission=(1.0, 0.10, 0.06)),
    }
    parts = build_car(surfaces)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.out))
    print("make_car: %d objects -> %s" % (len(parts), args.out))
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
