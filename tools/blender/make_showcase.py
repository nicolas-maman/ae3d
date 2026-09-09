"""Build the .blend the examples/blender_pipeline example is exported from.

    blender --background --factory-startup --python tools/blender/make_showcase.py -- --out resources/blender/showcase.blend

The .blend it writes is committed. Blender does not save reproducible files --
two runs of this script produce different bytes and, more to the point, a
different vertex order -- so the exported assets can only be regenerated from
the same .blend. This script is how that file is authored and refreshed, not
something to run before every export.

import bpy
import argparse
import math
import os
import sys


def principled(name, colour, metallic, roughness):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    for node in material.node_tree.nodes:
        if node.type == "BSDF_PRINCIPLED":
            node.inputs["Base Color"].default_value = colour
            node.inputs["Metallic"].default_value = metallic
            node.inputs["Roughness"].default_value = roughness
    return material


def main(argv):
    parser = argparse.ArgumentParser(prog="make_showcase")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = 24
    scene.frame_start = 1
    scene.frame_end = 48

    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3, radius=1.0, location=(0.0, 0.0, 1.5))
    orb = bpy.context.active_object
    orb.name = "Orb"
    orb.data.materials.append(principled("OrbMetal", (0.55, 0.62, 0.85, 1.0), 0.9, 0.25))

    # Two seconds at 24fps. Bezier by default, which is the point: the export
    # has to carry the easing rather than flatten it.
    orb.rotation_mode = "XYZ"
    for frame, height, turn in ((1, 1.5, 0.0), (24, 3.2, math.pi), (48, 1.5, math.tau)):
        orb.location = (0.0, 0.0, height)
        orb.rotation_euler = (0.0, 0.0, turn)
        orb.keyframe_insert(data_path="location", frame=frame)
        orb.keyframe_insert(data_path="rotation_euler", frame=frame)

    bpy.ops.mesh.primitive_cylinder_add(radius=2.6, depth=0.3, location=(0.0, 0.0, -0.15))
    plinth = bpy.context.active_object
    plinth.name = "Plinth"
    plinth.data.materials.append(principled("PlinthStone", (0.35, 0.33, 0.30, 1.0), 0.0, 0.85))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.out))
    print("make_showcase: wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
