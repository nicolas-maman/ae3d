"""The zombie: one surface over a skeleton, and what moves it.

The figure used to be nineteen boxes on a parent chain, because ae3d could not
skin. It can now, so this is what the scene was always meant to carry: a single
body built around a stick of joints, a bone at every one of them, weights that
blend across each joint, and an animation authored on the bones.

The joints come first and everything else is derived from them. The mesh is
grown around them, the armature is laid along them, and the walk is written as
rotations about them, so the three cannot disagree about where a knee is.

Rotations are authored in the figure's own axes -- swing a thigh about Y and it
walks -- and converted to bone space here rather than in the head of whoever
writes the walk. Blender points a bone's Y along its own length and picks the
other two axes by a roll convention; authoring against that convention means
rewriting the walk whenever a bone changes direction, and getting it subtly
wrong in the meantime. The conversion is exact and costs four matrix multiplies
per bone per key.
"""

import bpy
import bmesh
import math
import mathutils

# name, parent, offset from that parent, and how thick the body is here.
#
# A figure is measured in heads: this one is 1.80 m and its head is 0.24, which
# is seven and a half heads and the proportion a person actually is. The limb
# ratios are anatomical too -- the shin a shade shorter than the thigh, the
# forearm a shade shorter than the upper arm -- because a rig that is wrong
# here cannot be fixed by any amount of animation on top of it.
JOINTS = (
    # Where the figure is, as opposed to what it is doing. Root carries the walk
    # up the street; everything else is the walk itself. It has no thickness, so
    # nothing is grown around it -- a bone can drive a mesh without being in it.
    ("Root",      None,        (0.00,  0.00,  0.00), 0.000),
    ("Hips",      "Root",      (0.00,  0.00,  1.02), 0.150),
    ("Spine",     "Hips",      (0.00,  0.00,  0.13), 0.145),
    ("Chest",     "Spine",     (0.00,  0.00,  0.26), 0.170),
    ("Neck",      "Chest",     (0.00,  0.00,  0.22), 0.062),
    ("Head",      "Neck",      (0.00,  0.00,  0.09), 0.115),
    ("Crown",     "Head",      (0.00,  0.00,  0.13), 0.055),
    # Where the face points. No thickness, so nothing is grown around it and
    # nothing is weighted to it: it exists so that something can be turned
    # towards what the figure is looking at. Without it the only thing to aim is
    # the neck, and the neck points up -- aiming that at anything ahead lays the
    # head over on its side.
    ("Face",      "Head",      (0.11,  0.00,  0.02), 0.000),

    ("ShoulderL", "Chest",     (0.00,  0.17,  0.14), 0.072),
    ("ElbowL",    "ShoulderL", (0.00,  0.02, -0.29), 0.055),
    ("WristL",    "ElbowL",    (0.00,  0.01, -0.26), 0.044),
    ("HandL",     "WristL",    (0.00,  0.00, -0.10), 0.038),
    ("ShoulderR", "Chest",     (0.00, -0.17,  0.14), 0.072),
    ("ElbowR",    "ShoulderR", (0.00, -0.02, -0.29), 0.055),
    ("WristR",    "ElbowR",    (0.00, -0.01, -0.26), 0.044),
    ("HandR",     "WristR",    (0.00,  0.00, -0.10), 0.038),

    ("ThighL",    "Hips",      (0.00,  0.10, -0.07), 0.090),
    ("KneeL",     "ThighL",    (0.00,  0.00, -0.44), 0.068),
    ("AnkleL",    "KneeL",     (0.00,  0.00, -0.42), 0.055),
    ("ToeL",      "AnkleL",    (0.15,  0.00, -0.05), 0.045),
    ("ThighR",    "Hips",      (0.00, -0.10, -0.07), 0.090),
    ("KneeR",     "ThighR",    (0.00,  0.00, -0.44), 0.068),
    ("AnkleR",    "KneeR",     (0.00,  0.00, -0.42), 0.055),
    ("ToeR",      "AnkleR",    (0.15,  0.00, -0.05), 0.045),
)

# Which joints wear cloth and which are bare. A zombie in a suit that has been
# through something is skin at the head, the forearms and the hands.
BARE = {"Neck", "Head", "Crown", "WristL", "HandL", "WristR", "HandR"}

PARENT = {name: parent for name, parent, _offset, _radius in JOINTS}
OFFSET = {name: offset for name, _parent, offset, _radius in JOINTS}
RADIUS = {name: radius for name, _parent, _offset, radius in JOINTS}
ORDER = [name for name, _parent, _offset, _radius in JOINTS]


def rest_positions():
    """Where every joint is, in the figure's own space."""
    at = {}
    for name in ORDER:
        parent = PARENT[name]
        base = at[parent] if parent else mathutils.Vector((0.0, 0.0, 0.0))
        at[name] = base + mathutils.Vector(OFFSET[name])
    return at


# The joints the body is actually grown around. A joint with no thickness is a
# handle for the animation to hold, not a part of the figure.
GROWN = [name for name, _parent, _offset, radius in JOINTS if radius > 0.0]


def _stick(at, joints):
    """A vertex at every joint and an edge along every bone between them."""
    bm = bmesh.new()
    verts = {}
    for name in joints:
        verts[name] = bm.verts.new(at[name])
    bm.verts.ensure_lookup_table()
    for name in joints:
        parent = PARENT[name]
        if parent in verts:
            bm.edges.new((verts[parent], verts[name]))
    return bm, verts


def build_body(name, material, subdivisions=3):
    """A body grown around the joints, as one surface.

    The skin modifier sweeps a hull along the stick and the subdivision rounds
    it: what comes out is a single closed surface with the joints inside it,
    which is the whole point. A limb that is its own box can only ever meet its
    neighbour by overlapping it.
    """
    at = rest_positions()
    bm, verts = _stick(at, GROWN)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj

    skin = obj.modifiers.new("Skin", "SKIN")
    skin.use_smooth_shade = True
    layer = mesh.skin_vertices[0].data
    for index, joint in enumerate(GROWN):
        radius = RADIUS[joint]
        layer[index].radius = (radius, radius)
    # The hips are where the sweep starts, so the surface closes around them
    # rather than around whichever vertex happened to be first.
    layer[GROWN.index("Hips")].use_root = True

    smooth = obj.modifiers.new("Round", "SUBSURF")
    smooth.levels = subdivisions
    smooth.render_levels = subdivisions

    bpy.ops.object.modifier_apply(modifier="Skin")
    bpy.ops.object.modifier_apply(modifier="Round")

    mesh.materials.append(material)
    return obj


def unwrap(obj, repeats_per_metre, angle=1.15, margin=0.02):
    """UVs from the shape, scaled to a texel density rather than to the square.

    A box projection is right for a wall and wrong for a body: it seams every
    surface that turns through more than a right angle, which on a figure is
    all of them. This cuts where the surface actually turns.

    Then it is scaled. An unwrap packs the whole figure into the unit square,
    which fixes its texel density at the size of the image divided by the root
    of its area -- four square metres of zombie in one square of texture is 256
    texels to the metre however the image was authored, and no amount of
    painting fixes that. Scaling the layout to a stated number of repeats a
    metre puts the figure on the same standard as the walls behind it, and a
    tiling material is what it is wearing anyway.
    """
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=angle, island_margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")

    bm = bmesh.new()
    bm.from_mesh(obj.data)
    layer = bm.loops.layers.uv.verify()
    surface = sum(face.calc_area() for face in bm.faces)
    covered = 0.0
    for face in bm.faces:
        loops = face.loops
        first = loops[0][layer].uv
        for index in range(1, len(loops) - 1):
            second = loops[index][layer].uv - first
            third = loops[index + 1][layer].uv - first
            covered += abs(second.x * third.y - second.y * third.x) * 0.5
    if surface > 1e-6 and covered > 1e-9:
        scale = repeats_per_metre / (covered / surface) ** 0.5
        for face in bm.faces:
            for loop in face.loops:
                loop[layer].uv *= scale
    bm.to_mesh(obj.data)
    bm.free()


def build_rig(name):
    """A bone at every joint, laid along the stick the body was grown on."""
    at = rest_positions()
    data = bpy.data.armatures.new(name)
    rig = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")

    children = {}
    for joint in ORDER:
        if PARENT[joint]:
            children.setdefault(PARENT[joint], []).append(joint)

    edit = {}
    for joint in ORDER:
        bone = data.edit_bones.new(joint)
        bone.head = at[joint]
        below = children.get(joint)
        if below:
            # Down the first thing that hangs off it, which for a spine is the
            # next vertebra and for a wrist is the hand.
            bone.tail = at[below[0]]
        else:
            # A bone with nothing under it still needs a length, and the
            # direction it came from is the direction it points.
            bone.tail = at[joint] + mathutils.Vector(OFFSET[joint]) * 0.6
        if (bone.tail - bone.head).length < 1e-4:
            bone.tail = bone.head + mathutils.Vector((0.0, 0.0, 0.05))
        edit[joint] = bone

    for joint in ORDER:
        if PARENT[joint]:
            edit[joint].parent = edit[PARENT[joint]]
        # A bone with no thickness carries the figure; it is not part of it.
        # Left deforming, Blender's automatic weights hand it whatever is
        # nearest -- and Root runs from the ground up through the pelvis, so it
        # took the hips and half the torso with it and then dragged them up the
        # street. A root motion bone never deforms.
        edit[joint].use_deform = joint in GROWN
    bpy.ops.object.mode_set(mode="OBJECT")
    return rig


def bind(rig, meshes, influences=4):
    """Blender's own weights, so each joint blends the way a rigged one does.

    Limited to four bones a vertex, because four is what the vertex shader
    blends. Left unlimited, Blender happily gives a shoulder eight, the
    exporter keeps the heaviest four and the mesh that is drawn is not the mesh
    that was rigged -- with nothing to say so except a warning nobody reads.
    Cutting it here means the rig and the draw agree.
    """
    for obj in meshes:
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")

        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.vertex_group_limit_total(limit=influences)
        bpy.ops.object.vertex_group_normalize_all()


class Pose:
    """A pose of the figure, written in the figure's own axes.

    A joint is given a rotation about X, Y or Z as a person would describe it --
    the thigh swings about Y, the head turns about Z -- and this works out what
    that means for a bone whose axes Blender chose. The conversion is the only
    place the two conventions meet.
    """

    def __init__(self, rig):
        self.rig = rig
        self.at = rest_positions()
        self.rest = {bone.name: bone.matrix_local.copy() for bone in rig.data.bones}
        self.turns = {}

    def set(self, joint, rotation, shift=None):
        self.turns[joint] = (mathutils.Quaternion(rotation) if rotation else
                             mathutils.Quaternion((1.0, 0.0, 0.0, 0.0)),
                             mathutils.Vector(shift) if shift else
                             mathutils.Vector((0.0, 0.0, 0.0)))

    def world(self):
        """Where every joint is right now, given what has been set so far."""
        posed, _rest = self._joint_matrices()
        return posed

    def set_world(self, joint, rotation, shift=None):
        """Turn a joint to face a way in the figure's axes, whatever its parent
        is doing. A leg solved to reach the road is solved in the world the road
        is in, and the hips it hangs off have leaned since."""
        posed = self.world()
        parent = PARENT[joint]
        if parent:
            rotation = posed[parent].to_quaternion().inverted() @ rotation
        self.set(joint, rotation, shift)

    def _joint_matrices(self):
        """Where every joint ends up, posed and at rest, in the figure's axes."""
        posed, rest = {}, {}
        for joint in ORDER:
            turn, shift = self.turns.get(
                joint, (mathutils.Quaternion((1.0, 0.0, 0.0, 0.0)),
                        mathutils.Vector((0.0, 0.0, 0.0))))
            offset = mathutils.Vector(OFFSET[joint]) + shift
            local = mathutils.Matrix.Translation(offset) @ turn.to_matrix().to_4x4()
            rest_local = mathutils.Matrix.Translation(mathutils.Vector(OFFSET[joint]))
            parent = PARENT[joint]
            if parent:
                posed[joint] = posed[parent] @ local
                rest[joint] = rest[parent] @ rest_local
            else:
                posed[joint] = local
                rest[joint] = rest_local
        return posed, rest

    def apply(self, frame):
        """Write the pose onto the bones and key it.

        A bone's own matrix is its rest times whatever the pose adds, so what
        the pose has to add is the rest undone and the wanted transform put in
        its place -- expressed relative to the parent, which is where a bone
        keeps its pose.
        """
        posed, rest = self._joint_matrices()
        for joint in ORDER:
            wanted = posed[joint] @ rest[joint].inverted() @ self.rest[joint]
            parent = PARENT[joint]
            if parent:
                parent_wanted = (posed[parent] @ rest[parent].inverted()
                                 @ self.rest[parent])
                basis = (self.rest[joint].inverted() @ self.rest[parent]
                         @ parent_wanted.inverted() @ wanted)
            else:
                basis = self.rest[joint].inverted() @ wanted
            bone = self.rig.pose.bones[joint]
            bone.rotation_mode = "QUATERNION"
            bone.location = basis.to_translation()
            bone.rotation_quaternion = basis.to_quaternion()
            bone.keyframe_insert(data_path="location", frame=frame)
            bone.keyframe_insert(data_path="rotation_quaternion", frame=frame)


def build_clothes(name, material, subdivisions=2):
    """What the figure is wearing, as a second surface over the same joints.

    Cloth hangs; it does not follow a limb the way skin does. So it is grown at
    a slightly larger radius than the body and stops at the wrist and the neck,
    which is where a sleeve and a collar stop. Growing it from the same joints
    is what keeps it on the figure when the figure moves: both are weighted to
    the same bones, so a sleeve bends with the elbow inside it.
    """
    at = rest_positions()
    # GROWN, not ORDER: a joint with no thickness is a handle for the animation
    # to hold and not a part of the figure. Root has none and sits on the
    # ground, so including it grew a limb from the hips down between the legs --
    # a third leg, in every frame, which every count and every density still
    # read as correct.
    covered = [joint for joint in GROWN if joint not in BARE]
    bm = bmesh.new()
    verts = {}
    for joint in covered:
        verts[joint] = bm.verts.new(at[joint])
    bm.verts.ensure_lookup_table()
    for joint in covered:
        parent = PARENT[joint]
        if parent in verts:
            bm.edges.new((verts[parent], verts[joint]))

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj

    skin = obj.modifiers.new("Skin", "SKIN")
    skin.use_smooth_shade = True
    layer = mesh.skin_vertices[0].data
    for index, joint in enumerate(covered):
        # Loose over the body, and loosest where a coat actually hangs.
        slack = 1.16 if joint in ("Chest", "Spine", "Hips") else 1.09
        radius = RADIUS[joint] * slack
        layer[index].radius = (radius, radius)
    layer[covered.index("Hips")].use_root = True

    smooth = obj.modifiers.new("Round", "SUBSURF")
    smooth.levels = subdivisions
    smooth.render_levels = subdivisions
    bpy.ops.object.modifier_apply(modifier="Skin")
    bpy.ops.object.modifier_apply(modifier="Round")

    mesh.materials.append(material)
    return obj


def action_curves(obj):
    """Every fcurve of whatever this object is animated by, on any Blender."""
    animation = obj.animation_data
    if not animation or not animation.action:
        return
    layers = getattr(animation.action, "layers", None)
    if layers:
        for layer in layers:
            for strip in layer.strips:
                for bag in getattr(strip, "channelbags", []):
                    for curve in bag.fcurves:
                        yield curve
        return
    for curve in animation.action.fcurves:
        yield curve


def solve_leg(hip, target, upper, lower):
    """The thigh and knee angles that put an ankle on a target.

    Two bones and a distance are a triangle, so there is nothing to iterate
    towards: the law of cosines gives both angles exactly. Solved in the plane
    the figure walks in, which is where a stride happens; the small sideways
    component of a leg is left to the swing that set it.

    This is what stops the feet skating. A leg swung by a sine covers whatever
    distance that sine covers, the body covers whatever the walk speed covers,
    and the difference is a foot sliding along the road. Planting the stance
    foot and solving the leg to reach it makes the two agree by construction --
    the hips travel over a foot that stays where it was put.
    """
    to = target - hip
    reach = math.hypot(to.x, to.z)
    reach = max(abs(upper - lower) + 1e-4, min(upper + lower - 1e-4, reach))
    line = math.atan2(-to.x, -to.z)
    thigh = math.acos(max(-1.0, min(1.0,
                     (upper * upper + reach * reach - lower * lower) / (2.0 * upper * reach))))
    interior = math.acos(max(-1.0, min(1.0,
                        (upper * upper + lower * lower - reach * reach) / (2.0 * upper * lower))))
    return line - thigh, math.pi - interior


def ankle_of(thigh_angle, knee_angle, upper, lower):
    """Where those angles put the ankle, relative to the hip. The check that
    solve_leg means what it says."""
    thigh = mathutils.Quaternion((0.0, 1.0, 0.0), thigh_angle)
    knee = mathutils.Quaternion((0.0, 1.0, 0.0), knee_angle)
    down = mathutils.Vector((0.0, 0.0, -1.0))
    upper_end = thigh @ (down * upper)
    return upper_end + (thigh @ knee) @ (down * lower)
