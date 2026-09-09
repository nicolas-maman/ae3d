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
"""

import bpy
import argparse
import math
import os
import sys


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

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.out))
    print("make_fixture: wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
