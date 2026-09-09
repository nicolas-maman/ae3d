"""Build the zombie-on-a-street scene the zombie_street example loads.

    blender --background --factory-startup --python tools/blender/make_zombie_street.py -- --out resources/blender/zombie_street.blend

The zombie is rigid parts rather than a skinned mesh, because ae3d has no
skinning (ae3d#192). Every part carries its own world-space animation, so the
walk and the attack are keyframed maths rather than a rig.

The timeline is one clip in three phases at 24fps:

    frames   1- 72   walking up the street
    frames  73- 96   the lunge and the swipe
    frames  97-120   recovering, still walking

Seek to 3.0s over the agent channel and the zombie is mid-attack.
"""

import bpy
import argparse
import math
import os
import sys

FPS = 24
WALK_END = 72
ATTACK_END = 96
TOTAL = 120

STRIDE = 0.55
CADENCE = 24.0


def material(name, colour, roughness=0.85, metallic=0.0):
    made = bpy.data.materials.new(name)
    made.use_nodes = True
    for node in made.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            node.inputs["Base Color"].default_value = colour
            node.inputs["Roughness"].default_value = roughness
            node.inputs["Metallic"].default_value = metallic
    return made


def box(name, size, location, surface):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(surface)
    return obj


def key(obj, frame, location, rotation):
    obj.location = location
    obj.rotation_euler = rotation
    obj.keyframe_insert(data_path="location", frame=frame)
    obj.keyframe_insert(data_path="rotation_euler", frame=frame)


def walk_phase(frame):
    return (frame - 1) / CADENCE * math.tau


def advance(frame):
    """How far up the street the zombie has walked by this frame."""
    if frame <= WALK_END:
        return (frame - 1) / CADENCE * STRIDE * 2.0
    walked = (WALK_END - 1) / CADENCE * STRIDE * 2.0
    if frame <= ATTACK_END:
        lunge = (frame - WALK_END) / float(ATTACK_END - WALK_END)
        return walked + 0.9 * math.sin(lunge * math.pi * 0.5)
    resumed = (frame - ATTACK_END) / CADENCE * STRIDE * 2.0
    return walked + 0.9 + resumed


def attack_weight(frame):
    """0 outside the lunge, rising to 1 at the strike and easing back."""
    if frame <= WALK_END or frame > ATTACK_END:
        return 0.0
    t = (frame - WALK_END) / float(ATTACK_END - WALK_END)
    return math.sin(t * math.pi)


def build_zombie(parts):
    skin = material("ZombieSkin", (0.42, 0.52, 0.33, 1.0), 0.9)
    cloth = material("ZombieCloth", (0.20, 0.19, 0.24, 1.0), 0.95)
    bone = material("ZombieBone", (0.78, 0.75, 0.62, 1.0), 0.7)

    parts["Zombie_Torso"] = box("Zombie_Torso", (0.52, 0.30, 0.78), (0.0, 0.0, 1.18), cloth)
    parts["Zombie_Head"] = box("Zombie_Head", (0.34, 0.32, 0.36), (0.0, 0.0, 1.76), skin)
    parts["Zombie_Jaw"] = box("Zombie_Jaw", (0.26, 0.22, 0.10), (0.10, 0.0, 1.62), bone)

    for side, y in (("L", 0.36), ("R", -0.36)):
        parts["Zombie_Arm" + side] = box(
            "Zombie_Arm" + side, (0.46, 0.18, 0.18), (0.28, y, 1.42), cloth)
        parts["Zombie_Hand" + side] = box(
            "Zombie_Hand" + side, (0.34, 0.16, 0.16), (0.70, y, 1.42), skin)
        parts["Zombie_Thigh" + side] = box(
            "Zombie_Thigh" + side, (0.22, 0.22, 0.52), (0.0, y * 0.5, 0.66), cloth)
        parts["Zombie_Shin" + side] = box(
            "Zombie_Shin" + side, (0.19, 0.19, 0.48), (0.0, y * 0.5, 0.24), cloth)


def animate_zombie(parts):
    for frame in range(1, TOTAL + 1, 2):
        phase = walk_phase(frame)
        strike = attack_weight(frame)
        x = advance(frame)
        bob = 0.04 * abs(math.sin(phase))
        lean = 0.16 + 0.35 * strike

        key(parts["Zombie_Torso"], frame, (x, 0.0, 1.18 + bob), (0.0, lean, 0.0))
        key(parts["Zombie_Head"], frame, (x + 0.06 * strike, 0.0, 1.76 + bob),
            (0.10 * math.sin(phase * 0.5), 0.22 + 0.30 * strike, 0.0))
        key(parts["Zombie_Jaw"], frame, (x + 0.12, 0.0, 1.62 + bob),
            (0.0, 0.30 + 0.55 * strike, 0.0))

        for side, y, swing in (("L", 0.36, 1.0), ("R", -0.36, -1.0)):
            reach = 0.28 + 0.34 * strike
            drop = -0.55 * strike + 0.05 * math.sin(phase + swing)
            key(parts["Zombie_Arm" + side], frame,
                (x + reach, y, 1.42 + bob + drop * 0.35),
                (0.0, -0.15 + 1.15 * strike, 0.10 * swing))
            key(parts["Zombie_Hand" + side], frame,
                (x + reach + 0.42, y, 1.42 + bob + drop),
                (0.0, -0.15 + 1.35 * strike, 0.10 * swing))

            step = math.sin(phase + (0.0 if swing > 0 else math.pi))
            planted = 1.0 - strike
            key(parts["Zombie_Thigh" + side], frame,
                (x + 0.22 * step * planted, y * 0.5, 0.66 + bob),
                (0.0, 0.45 * step * planted, 0.0))
            key(parts["Zombie_Shin" + side], frame,
                (x + 0.30 * step * planted, y * 0.5, 0.24 + 0.10 * max(step, 0.0) * planted),
                (0.0, 0.30 * step * planted, 0.0))


def build_street(parts):
    tarmac = material("StreetTarmac", (0.09, 0.09, 0.10, 1.0), 0.95)
    kerb = material("StreetKerb", (0.44, 0.43, 0.41, 1.0), 0.9)
    brick = material("StreetBrick", (0.30, 0.16, 0.13, 1.0), 0.92)
    concrete = material("StreetConcrete", (0.36, 0.35, 0.34, 1.0), 0.9)
    lamp = material("StreetLamp", (0.16, 0.17, 0.18, 1.0), 0.35, 0.85)

    ground = material("StreetGround", (0.13, 0.13, 0.14, 1.0), 0.95)
    parts["Street_Ground"] = box("Street_Ground", (90.0, 70.0, 0.20), (10.0, 0.0, -0.30), ground)
    parts["Street_Road"] = box("Street_Road", (34.0, 7.0, 0.12), (10.0, 0.0, -0.06), tarmac)
    for side, y in (("L", 4.3), ("R", -4.3)):
        parts["Street_Path" + side] = box(
            "Street_Path" + side, (34.0, 2.4, 0.30), (10.0, y, 0.15), kerb)
    # The far side is a terrace; the near side is set well back so the camera
    # has somewhere to stand and the street reads as a corridor rather than a
    # wall. Blender Y becomes ae3d -Z, so these are the two sides of the road.
    for index, (bx, depth, height, surface) in enumerate((
            (-4.0, 5.0, 6.5, brick), (3.0, 4.4, 9.5, concrete),
            (10.0, 5.2, 5.5, brick), (17.0, 4.6, 8.0, concrete),
            (24.0, 5.0, 7.0, brick))):
        parts["Street_BlockL%d" % index] = box(
            "Street_BlockL%d" % index, (6.4, depth, height),
            (bx, 9.5, height * 0.5), surface)
    for index, (bx, height) in enumerate(((-2.0, 5.0), (8.0, 6.5), (18.0, 5.5))):
        parts["Street_BlockR%d" % index] = box(
            "Street_BlockR%d" % index, (7.0, 5.0, height),
            (bx, -15.0, height * 0.5), concrete if index % 2 else brick)
    for index, lx in enumerate((-1.0, 8.0, 17.0, 26.0)):
        parts["Street_Lamp%d" % index] = box(
            "Street_Lamp%d" % index, (0.18, 0.18, 4.6), (lx, 5.0, 2.3), lamp)
        parts["Street_LampArm%d" % index] = box(
            "Street_LampArm%d" % index, (1.3, 0.14, 0.14), (lx - 0.6, 5.0, 4.5), lamp)


def main(argv):
    parser = argparse.ArgumentParser(prog="make_zombie_street")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 1
    scene.frame_end = TOTAL

    parts = {}
    build_street(parts)
    build_zombie(parts)
    animate_zombie(parts)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.out))
    print("make_zombie_street: %d objects -> %s" % (len(parts), args.out))
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
