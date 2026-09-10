"""Build the .blend the export tests run against.

    blender --background --factory-startup --python tools/blender/make_fixture.py -- --out tests/fixtures/spin.blend

The .blend it writes is committed. Blender does not save reproducible files --
two runs of this script produce different bytes and, more to the point, a
different vertex order -- so the exported assets can only be regenerated from
the same .blend. This script is how that file is authored and refreshed, not
something to run before every export.

Two objects, deliberately different:

  Spinner  animated, rotating a quarter turn about Z over one second, with a
           material. The thing the pipeline is meant to carry end to end.
  Still    no animation, no material. Proves the exporter does not invent a
           clip for an object that has none.
  Bender   a column over six bones, weighted by Blender and bent by an action
           on the pose bones. The skinned half of the pipeline, in the simplest
           thing that has one.
"""

import bpy
import mathutils
import argparse
import math
import os
import sys


BENDER_SEGMENTS = 6
BENDER_HEIGHT = 0.5
BENDER_AT = (-4.0, 0.0, 0.0)


def build_bender(scene):
    """A column over a chain of bones, weighted and bent.

    This is the skinned half of the pipeline, and it is deliberately the
    simplest thing that has one: a mesh, an armature, vertex weights that blend
    across each joint, and an action on the pose bones. Everything a skinned
    character needs from the exporter is here, and none of what makes a
    character hard to check.
    """
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=12, radius=0.35, depth=BENDER_SEGMENTS * BENDER_HEIGHT,
        location=(BENDER_AT[0], BENDER_AT[1],
                  BENDER_AT[2] + BENDER_SEGMENTS * BENDER_HEIGHT / 2.0))
    column = bpy.context.active_object
    column.name = "Bender"

    # Rings along the column, so the surface has somewhere to bend. Without
    # them the whole thing is two end caps and it hinges rather than bends.
    modifier = column.modifiers.new("Rings", "SUBSURF")
    modifier.subdivision_type = "SIMPLE"
    modifier.levels = 2
    modifier.render_levels = 2
    bpy.ops.object.modifier_apply(modifier="Rings")

    armature_data = bpy.data.armatures.new("BenderRig")
    armature = bpy.data.objects.new("BenderRig", armature_data)
    armature.location = BENDER_AT
    scene.collection.objects.link(armature)

    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode="EDIT")
    previous = None
    for index in range(BENDER_SEGMENTS):
        bone = armature_data.edit_bones.new("Bone%d" % index)
        bone.head = (0.0, 0.0, index * BENDER_HEIGHT)
        bone.tail = (0.0, 0.0, (index + 1) * BENDER_HEIGHT)
        if previous is not None:
            bone.parent = previous
            bone.use_connect = True
        previous = bone
    bpy.ops.object.mode_set(mode="OBJECT")

    # Blender's own weights, so the blend across each joint is the one a person
    # would get by rigging this by hand rather than one invented here.
    bpy.ops.object.select_all(action="DESELECT")
    column.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")

    # A lean that accumulates up the column: every bone above the second turns
    # by the same small angle, so the top travels and the base does not.
    armature.animation_data_create()
    action = bpy.data.actions.new("BenderBend")
    armature.animation_data.action = action
    for index in range(BENDER_SEGMENTS):
        pose_bone = armature.pose.bones["Bone%d" % index]
        pose_bone.rotation_mode = "QUATERNION"
        pose_bone.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=1)
        angle = math.radians(12.0) if index >= 2 else 0.0
        pose_bone.rotation_quaternion = mathutils.Quaternion((1.0, 0.0, 0.0), angle)
        pose_bone.keyframe_insert(data_path="rotation_quaternion", frame=25)
    for fcurve in action_fcurves(action):
        for point in fcurve.keyframe_points:
            point.interpolation = "LINEAR"


def action_fcurves(action):
    """Blender 5 keeps fcurves under layers; older ones keep them flat."""
    layers = getattr(action, "layers", None)
    if layers:
        for layer in layers:
            for strip in layer.strips:
                for bag in getattr(strip, "channelbags", []):
                    for fcurve in bag.fcurves:
                        yield fcurve
        return
    for fcurve in action.fcurves:
        yield fcurve


def main(argv):
    parser = argparse.ArgumentParser(prog="make_fixture")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = 24
    scene.render.fps_base = 1.0

    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(0.0, 0.0, 0.0))
    spinner = bpy.context.active_object
    spinner.name = "Spinner"

    material = bpy.data.materials.new("SpinnerRed")
    material.use_nodes = True
    for node in material.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            node.inputs["Base Color"].default_value = (0.8, 0.2, 0.15, 1.0)
    spinner.data.materials.append(material)

    # A quarter turn about Z over 24 frames, which at 24fps is one second.
    spinner.rotation_mode = "XYZ"
    spinner.rotation_euler = (0.0, 0.0, 0.0)
    spinner.keyframe_insert(data_path="rotation_euler", frame=1)
    spinner.rotation_euler = (0.0, 0.0, math.radians(90.0))
    spinner.keyframe_insert(data_path="rotation_euler", frame=25)

    # And a straight run along X, so a seek has an answer that is easy to check.
    spinner.location = (0.0, 0.0, 0.0)
    spinner.keyframe_insert(data_path="location", frame=1)
    spinner.location = (4.0, 0.0, 0.0)
    spinner.keyframe_insert(data_path="location", frame=25)

    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, location=(3.0, 0.0, 0.0))
    still = bpy.context.active_object
    still.name = "Still"

    build_bender(scene)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.out))
    print("make_fixture: wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
