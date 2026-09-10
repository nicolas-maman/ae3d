"""Build the street the zombie_street example loads.

    blender --background --factory-startup --python tools/blender/make_zombie_street.py -- --out resources/blender/zombie_street.blend

The zombie is rigid parts on a parent chain rather than a skinned mesh, because
ae3d has no skinning (ae3d#192). Every joint is the origin of the part below it
and every part overlaps the one above, so a bent elbow bends rather than coming
apart: the transform hierarchy carries each limb through the arc of its parent
(ae3d#230).

Everything is textured from generated images (zombie_street_textures) with UVs
projected per face at a fixed number of repeats per metre, so a wall and a kerb
carry brick and paving of the same size whatever their own size.

The timeline is one clip in three phases at 24fps:

    frames   1- 64   walking up the street
    frames  65- 92   the lunge and the swipe
    frames  93-120   recovering, still walking

Seek to 3.2s over the agent channel and the zombie is mid-attack.
"""

import bpy
import bmesh
import argparse
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zombie_street_textures as textures

FPS = 24
WALK_END = 64
ATTACK_END = 92
TOTAL = 120

STRIDE_RATE = 1.9
WALK_SPEED = 0.9
WALK_START_X = -6.4


def material(name, image, roughness=0.9, metallic=0.0, emission=None):
    made = bpy.data.materials.new(name)
    made.use_nodes = True
    tree = made.node_tree
    principled = next(n for n in tree.nodes if n.type == "BSDF_PRINCIPLED")
    principled.inputs["Roughness"].default_value = roughness
    principled.inputs["Metallic"].default_value = metallic
    if emission is not None:
        principled.inputs["Emission Color"].default_value = emission + (1.0,)
        principled.inputs["Emission Strength"].default_value = 4.0
        principled.inputs["Base Color"].default_value = emission + (1.0,)
        return made

    node = tree.nodes.new("ShaderNodeTexImage")
    node.image = image
    node.location = (-320.0, 200.0)
    tree.links.new(node.outputs["Color"], principled.inputs["Base Color"])
    # The exported MTL carries the image and a base colour. Keeping the colour
    # white means a renderer that samples the texture is not also tinting it by
    # whatever the swatch happened to be.
    principled.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    return made


def project_uvs(bm, repeats_per_metre):
    """Box projection: every face takes its two axes from its own normal.

    A wall and a kerb of different sizes then carry brick of the same size,
    which is the whole reason to do this in the builder rather than leave the
    mesh unwrapped.
    """
    layer = bm.loops.layers.uv.verify()
    for face in bm.faces:
        normal = face.normal
        axis = max(range(3), key=lambda i: abs(normal[i]))
        if axis == 0:
            first, second = 1, 2
        elif axis == 1:
            first, second = 0, 2
        else:
            first, second = 0, 1
        for loop in face.loops:
            position = loop.vert.co
            loop[layer].uv = (position[first] * repeats_per_metre,
                              position[second] * repeats_per_metre)


def block(name, low, high, surface, repeats=0.5, bevel=0.0, taper=1.0):
    """A box between two corners, in the space of the object that carries it.

    A limb is built from its own joint outwards, so the high end of the box is
    the joint it hangs from and the low end is the joint it carries. taper
    scales the high end, which is what makes an arm thicker at the shoulder
    than at the elbow and read as an arm rather than as a length of timber.
    """
    bm = bmesh.new()
    mid_x = (low[0] + high[0]) * 0.5
    mid_y = (low[1] + high[1]) * 0.5

    def far(x, y):
        return (mid_x + (x - mid_x) * taper, mid_y + (y - mid_y) * taper)

    fx0, fy0 = far(low[0], low[1])
    fx1, fy1 = far(high[0], high[1])
    corners = [
        (low[0], low[1], low[2]), (high[0], low[1], low[2]),
        (high[0], high[1], low[2]), (low[0], high[1], low[2]),
        (fx0, fy0, high[2]), (fx1, fy0, high[2]),
        (fx1, fy1, high[2]), (fx0, fy1, high[2]),
    ]
    verts = [bm.verts.new(corner) for corner in corners]
    for indices in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                    (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        bm.faces.new([verts[i] for i in indices])
    bm.normal_update()

    if bevel > 0.0:
        bmesh.ops.bevel(bm, geom=list(bm.verts) + list(bm.edges), offset=bevel,
                        segments=2, profile=0.5, affect="EDGES", clamp_overlap=True)
        bm.normal_update()

    project_uvs(bm, repeats)

    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(surface)

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


# How often a material repeats across a metre. Texel density is this times the
# size of the image, and it is the number that decides whether a wall reads as
# brick or as a photograph of brick seen from an inch away. One standard across
# the street, so no two surfaces in a frame are textured to different ones.
WALL_REPEATS = 0.84
GROUND_REPEATS = 0.84
GLASS_REPEATS = 1.7

# A storey, and the opening in it. These are the numbers that decide whether a
# street reads at the scale of a person: a window a person could climb through,
# a door a person could walk through, a floor-to-floor height a person could
# stand in.
STOREY = 3.15
WINDOW_W = 1.15
WINDOW_H = 1.55
SILL_UP = 0.95
REVEAL = 0.24
DOOR_W = 1.35
DOOR_H = 2.45
BAY = 2.6


def _face(bm, corners):
    bm.faces.new([bm.verts.new(corner) for corner in corners])


def _slab(bm, low, high):
    x0, y0, z0 = low
    x1, y1, z1 = high
    _face(bm, [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)])
    _face(bm, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)])
    _face(bm, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)])
    _face(bm, [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)])
    _face(bm, [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)])
    _face(bm, [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)])


def _unit_uvs(bm):
    """Every face mapped across the whole image, once.

    A window is not tiled. What is behind the glass is a room, and a room that
    repeats twice across one pane is a wallpaper pattern: the image has to land
    on the pane exactly once, whatever size the pane is.
    """
    layer = bm.loops.layers.uv.verify()
    for face in bm.faces:
        normal = face.normal
        axis = max(range(3), key=lambda i: abs(normal[i]))
        first, second = [(1, 2), (0, 2), (0, 1)][axis]
        lows = [min(loop.vert.co[a] for loop in face.loops) for a in (first, second)]
        spans = [max(1e-6, max(loop.vert.co[a] for loop in face.loops) - lows[n])
                 for n, a in enumerate((first, second))]
        for loop in face.loops:
            position = loop.vert.co
            loop[layer].uv = ((position[first] - lows[0]) / spans[0],
                              (position[second] - lows[1]) / spans[1])


def _finish(bm, name, surface, repeats):
    """Weld, face outwards, project and hand back an object."""
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.normal_update()
    if repeats > 0.0:
        project_uvs(bm, repeats)
    else:
        _unit_uvs(bm)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(surface)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def _openings(width, base, height):
    """Where the holes in a front go: windows by storey and bay, a door in the
    middle of the ground floor.

    Bays are laid out from the middle outwards rather than divided into the
    width, so a wide building gets more windows and not wider ones. A window is
    a size, not a fraction.
    """
    bays = max(1, int(width / BAY))
    storeys = max(1, int((height - base - 0.6) / STOREY))
    spacing = width / bays
    holes = []
    for storey in range(storeys):
        floor = base + storey * STOREY
        for bay in range(bays):
            centre = -width * 0.5 + (bay + 0.5) * spacing
            if storey == 0 and bay == bays // 2:
                holes.append((centre - DOOR_W * 0.5, centre + DOOR_W * 0.5,
                              base + 0.02, base + DOOR_H, True))
                continue
            low = floor + SILL_UP
            holes.append((centre - WINDOW_W * 0.5, centre + WINDOW_W * 0.5,
                          low, low + WINDOW_H, False))
    return holes, storeys


def _front(bm, x0, x1, z0, z1, sign, holes):
    """The plane of a front, as a grid of quads with the holes left out, and
    the reveal each hole is set back into."""
    xs = sorted({x0, x1} | {v for hole in holes for v in hole[:2]})
    zs = sorted({z0, z1} | {v for hole in holes for v in hole[2:4]})
    for i in range(len(xs) - 1):
        for j in range(len(zs) - 1):
            cx = (xs[i] + xs[i + 1]) * 0.5
            cz = (zs[j] + zs[j + 1]) * 0.5
            if any(h[0] < cx < h[1] and h[2] < cz < h[3] for h in holes):
                continue
            _face(bm, [(xs[i], 0.0, zs[j]), (xs[i + 1], 0.0, zs[j]),
                       (xs[i + 1], 0.0, zs[j + 1]), (xs[i], 0.0, zs[j + 1])])

    back = sign * REVEAL
    for hx0, hx1, hz0, hz1, _door in holes:
        _face(bm, [(hx0, 0.0, hz0), (hx0, back, hz0), (hx0, back, hz1), (hx0, 0.0, hz1)])
        _face(bm, [(hx1, 0.0, hz0), (hx1, back, hz0), (hx1, back, hz1), (hx1, 0.0, hz1)])
        _face(bm, [(hx0, 0.0, hz0), (hx1, 0.0, hz0), (hx1, back, hz0), (hx0, back, hz0)])
        _face(bm, [(hx0, 0.0, hz1), (hx1, 0.0, hz1), (hx1, back, hz1), (hx0, back, hz1)])


def building(name, width, depth, height, base, surface, repeats, sign, trim,
             glass, glow, rng):
    """A terrace front: a mass, the holes in it, what frames them, what fills them.

    Three objects rather than one, because the exporter writes the material an
    object names and these are three materials. They are also three things: a
    wall of brick, a course of stone, and glass with a room behind it.
    """
    holes, storeys = _openings(width, base, height)
    half = width * 0.5
    far = sign * depth

    shell = bmesh.new()
    _front(shell, -half, half, base, height, sign, holes)
    _face(shell, [(-half, far, base), (half, far, base), (half, far, height), (-half, far, height)])
    _face(shell, [(-half, 0.0, base), (-half, far, base), (-half, far, height), (-half, 0.0, height)])
    _face(shell, [(half, 0.0, base), (half, far, base), (half, far, height), (half, 0.0, height)])
    _face(shell, [(-half, 0.0, height), (half, 0.0, height), (half, far, height), (-half, far, height)])
    _face(shell, [(-half, 0.0, base), (half, 0.0, base), (half, far, base), (-half, far, base)])
    shell_obj = _finish(shell, name, surface, repeats)

    # What a facade is articulated by: a sill under every window, a band at
    # every floor line, and a cornice that throws the top of the wall into
    # shadow. All of it stands proud of the front, so all of it catches the
    # light from one side and not the other, which is most of what makes a
    # wall read as built rather than printed.
    band = bmesh.new()
    out = sign * -0.09
    for hx0, hx1, hz0, _hz1, door in holes:
        if door:
            _slab(band, (hx0 - 0.12, min(0.0, out * 1.6), hz0),
                  (hx1 + 0.12, max(0.0, out * 1.6), hz0 + DOOR_H + 0.14))
            continue
        _slab(band, (hx0 - 0.09, min(0.0, out), hz0 - 0.08),
              (hx1 + 0.09, max(0.0, out), hz0))
    for storey in range(1, storeys + 1):
        z = base + storey * STOREY
        if z >= height - 0.1:
            continue
        _slab(band, (-half, min(0.0, out * 0.55), z - 0.07),
              (half, max(0.0, out * 0.55), z))
    cornice = sign * -0.26
    _slab(band, (-half - 0.1, min(0.0, cornice), height - 0.42),
          (half + 0.1, max(0.0, cornice), height))

    # Glass at the back of its reveal, in two objects rather than one: a lit
    # room and a dark one are different materials, and a night street is mostly
    # made of which windows are which. Which ones are lit is drawn rather than
    # patterned -- a terrace where every fourth window is on reads as wallpaper.
    dark = bmesh.new()
    lit = bmesh.new()
    at = sign * (REVEAL - 0.015)
    for hx0, hx1, hz0, hz1, door in holes:
        panes = dark if (door or rng.random() > 0.34) else lit
        _face(panes, [(hx0 + 0.04, at, hz0 + 0.04), (hx1 - 0.04, at, hz0 + 0.04),
                      (hx1 - 0.04, at, hz1 - 0.04), (hx0 + 0.04, at, hz1 - 0.04)])
        if door:
            continue
        # The glazing bar is joinery and belongs to the frame, not to the
        # glass: a pane is two panes because something solid divides them.
        middle = (hx0 + hx1) * 0.5
        _slab(band, (middle - 0.025, min(at, at - sign * 0.03), hz0 + 0.04),
              (middle + 0.025, max(at, at - sign * 0.03), hz1 - 0.04))
    trim_obj = _finish(band, name + "_Trim", trim, 2.2)
    trim_obj.parent = shell_obj

    dark_obj = _finish(dark, name + "_Glass", glass, 0.0)
    dark_obj.parent = shell_obj
    lit_obj = _finish(lit, name + "_Lit", glow, 0.0)
    lit_obj.parent = shell_obj
    return shell_obj, trim_obj, dark_obj, lit_obj


def ground_plane(name, length, width, surface, repeats, rng):
    """What the street stands on: a base with a fall across it, not a table top.

    It is mostly hidden by the road and the pavements, so it is cut coarsely --
    detail nobody sees is the one thing an expensive-looking scene never
    spends on. What it does need is not to be one flat quad, because the far
    end of a street is exactly where a flat quad shows itself.
    """
    bm = bmesh.new()
    along, across = 14, 8
    grid = []
    for i in range(along + 1):
        x = -length * 0.5 + length * i / along
        row = []
        for j in range(across + 1):
            y = -width * 0.5 + width * j / across
            row.append(bm.verts.new((x, y, -0.34 + rng.uniform(-0.05, 0.02))))
        grid.append(row)
    for i in range(along):
        for j in range(across):
            bm.faces.new([grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]])
    return _finish(bm, name, surface, repeats)


def road_surface(name, length, width, camber, surface, repeats, rng):
    """A carriageway with a crown down the middle and a fall to each gutter.

    A road is not flat. It is built with a camber so water runs off it, and
    that camber is most of what stops a street reading as a corridor with a
    black rectangle in it: the crown catches the lamps down its length and the
    gutters stay dark. Cut across its length as well, so the surface is a
    surface and not one quad.
    """
    bm = bmesh.new()
    along, across = 44, 8
    top = []
    for i in range(along + 1):
        x = -length * 0.5 + length * i / along
        row = []
        for j in range(across + 1):
            t = j / across
            y = -width * 0.5 + width * t
            fall = camber * (1.0 - (2.0 * t - 1.0) ** 2)
            # Worn, not machined: the surface sags a little where it has been
            # driven on and rises where it has not.
            wear = rng.uniform(-0.006, 0.006)
            row.append(bm.verts.new((x, y, fall + wear)))
        top.append(row)
    for i in range(along):
        for j in range(across):
            bm.faces.new([top[i][j], top[i + 1][j], top[i + 1][j + 1], top[i][j + 1]])

    # A skirt down to the base, so the road is a solid and not a sheet.
    base = -0.30
    for i in range(along):
        for edge in (0, across):
            a, b = top[i][edge], top[i + 1][edge]
            low_a = bm.verts.new((a.co.x, a.co.y, base))
            low_b = bm.verts.new((b.co.x, b.co.y, base))
            bm.faces.new([a, b, low_b, low_a])
    return _finish(bm, name, surface, repeats)


def kerbs(name, length, width, surface, repeats, rng):
    """Individual stones, laid end to end with a joint between them.

    One long block reads as an extrusion. What makes a kerb a kerb is that it
    is a run of separate stones, each sitting a millimetre or two differently
    from its neighbour.
    """
    bm = bmesh.new()
    stone = 0.9
    count = int(length / stone)
    for i in range(count):
        x0 = -length * 0.5 + i * stone + 0.012
        x1 = x0 + stone - 0.024
        lift = rng.uniform(-0.004, 0.006)
        _slab(bm, (x0, -width * 0.5, -0.42), (x1, width * 0.5, 0.15 + lift))
    return _finish(bm, name, surface, repeats)


def build_street(parts, surfaces):
    # Nothing here shares a plane with anything else: the road sinks into the
    # ground and the kerbs sink into the road, so no two faces are coplanar and
    # the depth test never has to choose between them.
    shape = random.Random(9173)
    ground = ground_plane("Street_Ground", 84.0, 44.0, surfaces["tarmac"],
                          GROUND_REPEATS, shape)
    ground.location = (6.0, 0.0, 0.0)
    parts["Street_Ground"] = ground

    road = road_surface("Street_Road", 68.0, 7.0, 0.075,
                        surfaces["tarmac"], GROUND_REPEATS, shape)
    road.location = (6.0, 0.0, 0.0)
    parts["Street_Road"] = road

    # Longer than the road and wider than the gap it fills, so its ends and its
    # far edge run past what they meet rather than stopping level with it: two
    # faces that stop in the same plane are two faces the depth test has to
    # choose between.
    for side, y in (("L", 5.0), ("R", -5.0)):
        path = block("Street_Path" + side,
                     (-35.0, -1.6, -0.45), (35.0, 1.6, 0.14),
                     surfaces["paving"], repeats=GROUND_REPEATS, bevel=0.02)
        path.location = (6.0, y, 0.0)
        parts["Street_Path" + side] = path

        # The stones between the pavement and the gutter, on the road side of
        # it, laid one at a time.
        stones = kerbs("Street_Kerb" + side, 70.0, 0.34,
                       surfaces["stone"], 2.2, shape)
        stones.location = (6.0, y - 1.62 if y > 0.0 else y + 1.62, 0.0)
        parts["Street_Kerb" + side] = stones

    # The far terrace runs unbroken; the near side is set back, so the street
    # reads as a corridor rather than a trench. Blender Y becomes ae3d -Z.
    far = ((-22.0, 7.0, 12.5, "brick"), (-9.0, 8.0, 9.0, "concrete"),
           (2.0, 6.5, 13.5, "brick"), (13.0, 7.5, 10.0, "concrete"),
           (25.0, 7.0, 15.0, "brick"))
    # Each is sunk to a depth of its own. Buildings founded at the same level
    # share the plane of their own footings wherever two of them touch, and one
    # sitting exactly on the ground shares that.
    rng = random.Random(4021)
    for index, (x, depth, height, surface) in enumerate(far):
        name = "Street_BlockL%d" % index
        shell, trim, dark, lit = building(name, 11.2, depth, height,
                                          -0.4 - index * 0.03, surfaces[surface],
                                          WALL_REPEATS, 1.0, surfaces["stone"],
                                          surfaces["glass"], surfaces["glow"], rng)
        # A terrace is not machined: each front sets back a few centimetres
        # from its neighbour, which is also what keeps two of them from sharing
        # the plane they face the street in.
        shell.location = (x, 6.55 + index * 0.04, 0.0)
        parts[name] = shell
        parts[name + "_Trim"] = trim
        parts[name + "_Glass"] = dark
        parts[name + "_Lit"] = lit

    near = ((-16.0, 8.0, 11.0, "concrete"), (2.0, 9.0, 14.0, "brick"),
            (20.0, 8.0, 12.0, "concrete"))
    for index, (x, depth, height, surface) in enumerate(near):
        name = "Street_BlockR%d" % index
        shell, trim, dark, lit = building(name, 13.0, depth, height,
                                          -0.55 - index * 0.03, surfaces[surface],
                                          WALL_REPEATS, -1.0, surfaces["stone"],
                                          surfaces["glass"], surfaces["glow"], rng)
        shell.location = (x, -7.55 - index * 0.04, 0.0)
        parts[name] = shell
        parts[name + "_Trim"] = trim
        parts[name + "_Glass"] = dark
        parts[name + "_Lit"] = lit

    for index, x in enumerate((-14.0, 0.0, 14.0)):
        post = block("Street_Lamp%d" % index,
                     (-0.075, -0.075, 0.0), (0.075, 0.075, 4.4),
                     surfaces["metal"], repeats=1.2, bevel=0.015)
        post.location = (x, 4.1, 0.1)
        parts["Street_Lamp%d" % index] = post

        arm = block("Street_LampArm%d" % index,
                    (-0.06, -1.15, -0.06), (0.06, 0.0, 0.06),
                    surfaces["metal"], repeats=1.2, bevel=0.02)
        arm.parent = post
        arm.location = (0.0, -0.04, 4.35)
        parts["Street_LampArm%d" % index] = arm

        head = block("Street_LampHead%d" % index,
                     (-0.17, -0.28, -0.16), (0.17, 0.28, 0.0),
                     surfaces["lamp"], repeats=1.0, bevel=0.03)
        head.parent = arm
        head.location = (0.0, -1.12, -0.05)
        parts["Street_LampHead%d" % index] = head


def build_zombie(parts, surfaces):
    """Every limb is a box whose joint is its own origin.

    Each one extends downwards past its child joint and upwards past its own,
    so consecutive parts overlap: the overlap is what closes the seam when a
    joint bends.
    """
    skin = surfaces["skin"]
    cloth = surfaces["cloth"]

    hips = block("Zombie_Hips", (-0.16, -0.175, -0.11), (0.16, 0.175, 0.15),
                 cloth, repeats=3.0, bevel=0.03, taper=0.86)
    hips.location = (WALK_START_X, 0.0, 0.95)
    parts["Zombie_Hips"] = hips

    spine = block("Zombie_Spine", (-0.155, -0.125, -0.06), (0.155, 0.125, 0.46),
                  cloth, repeats=3.0, bevel=0.04, taper=1.28)
    spine.parent = hips
    spine.location = (0.0, 0.0, 0.12)
    parts["Zombie_Spine"] = spine

    neck = block("Zombie_Neck", (-0.062, -0.062, -0.03), (0.062, 0.062, 0.10),
                 skin, repeats=6.0, bevel=0.02, taper=0.92)
    neck.parent = spine
    neck.location = (0.0, 0.0, 0.42)
    parts["Zombie_Neck"] = neck

    head = block("Zombie_Head", (-0.115, -0.10, -0.04), (0.115, 0.10, 0.21),
                 skin, repeats=4.0, bevel=0.045, taper=0.88)
    head.parent = neck
    head.location = (0.0, 0.0, 0.06)
    parts["Zombie_Head"] = head

    jaw = block("Zombie_Jaw", (-0.10, -0.075, -0.06), (0.12, 0.075, 0.0),
                skin, repeats=5.0, bevel=0.02)
    jaw.parent = head
    jaw.location = (0.02, 0.0, 0.015)
    parts["Zombie_Jaw"] = jaw

    # A blank box reads as the back of a head from every angle. Sockets and a
    # mouth are what make it the front.
    gore = surfaces["gore"]
    mouth = block("Zombie_Mouth", (-0.02, -0.055, -0.045), (0.02, 0.055, 0.0),
                  gore, repeats=8.0)
    mouth.parent = head
    mouth.location = (0.10, 0.0, 0.02)
    parts["Zombie_Mouth"] = mouth

    for side, y in (("L", 0.048), ("R", -0.048)):
        socket = block("Zombie_Eye" + side, (-0.03, -0.025, -0.022),
                       (0.012, 0.025, 0.022), gore, repeats=10.0)
        socket.parent = head
        socket.location = (0.105, y, 0.115)
        parts["Zombie_Eye" + side] = socket

    # Inside the chest, not beside it: a shoulder level with the edge of the
    # torso opens a gap the moment the arm swings.
    for side, y in (("L", 0.196), ("R", -0.196)):
        upper = block("Zombie_ArmUpper" + side,
                      (-0.055, -0.055, -0.30), (0.055, 0.055, 0.075),
                      cloth, repeats=4.0, bevel=0.025, taper=1.22)
        upper.parent = spine
        upper.location = (0.0, y, 0.37)
        parts["Zombie_ArmUpper" + side] = upper

        lower = block("Zombie_ArmLower" + side,
                      (-0.05, -0.05, -0.27), (0.05, 0.05, 0.055),
                      skin, repeats=4.5, bevel=0.022, taper=1.26)
        lower.parent = upper
        lower.location = (0.0, 0.0, -0.29)
        parts["Zombie_ArmLower" + side] = lower

        hand = block("Zombie_Hand" + side,
                     (-0.058, -0.035, -0.15), (0.058, 0.035, 0.04),
                     skin, repeats=6.0, bevel=0.02, taper=1.18)
        hand.parent = lower
        hand.location = (0.0, 0.0, -0.26)
        parts["Zombie_Hand" + side] = hand

    for side, y in (("L", 0.105), ("R", -0.105)):
        thigh = block("Zombie_LegUpper" + side,
                      (-0.078, -0.078, -0.40), (0.078, 0.078, 0.07),
                      cloth, repeats=3.5, bevel=0.03, taper=1.34)
        thigh.parent = hips
        thigh.location = (0.0, y, -0.06)
        parts["Zombie_LegUpper" + side] = thigh

        shin = block("Zombie_LegLower" + side,
                     (-0.066, -0.066, -0.39), (0.066, 0.066, 0.06),
                     cloth, repeats=3.5, bevel=0.028, taper=1.28)
        shin.parent = thigh
        shin.location = (0.0, 0.0, -0.39)
        parts["Zombie_LegLower" + side] = shin

        foot = block("Zombie_Foot" + side,
                     (-0.075, -0.072, -0.09), (0.185, 0.072, 0.035),
                     cloth, repeats=4.0, bevel=0.025)
        foot.parent = shin
        foot.location = (0.0, 0.0, -0.38)
        parts["Zombie_Foot" + side] = foot


def key(obj, frame, location=None, rotation=None):
    if location is not None:
        obj.location = location
        obj.keyframe_insert(data_path="location", frame=frame)
    if rotation is not None:
        obj.rotation_euler = rotation
        obj.keyframe_insert(data_path="rotation_euler", frame=frame)


def phase(frame):
    """The angle the walk cycle has reached at this frame."""
    return (frame - 1) / FPS * STRIDE_RATE * math.tau


def strike(frame):
    """0 outside the lunge, 1 at the moment of the swipe."""
    if frame <= WALK_END or frame > ATTACK_END:
        return 0.0
    t = (frame - WALK_END) / float(ATTACK_END - WALK_END)
    return math.sin(t * math.pi) ** 1.4


def advance(frame):
    """How far up the street the hips have travelled by this frame."""
    walked = min(frame, WALK_END + 1) - 1
    distance = walked / FPS * WALK_SPEED
    if frame > WALK_END:
        lunge = min(frame, ATTACK_END) - WALK_END
        span = float(ATTACK_END - WALK_END)
        distance += 0.75 * math.sin(lunge / span * math.pi * 0.5)
    if frame > ATTACK_END:
        distance += (frame - ATTACK_END) / FPS * WALK_SPEED * 0.8
    return distance


def animate(parts):
    for frame in range(1, TOTAL + 1, 2):
        step = phase(frame)
        hit = strike(frame)
        settled = 1.0 - hit
        x = WALK_START_X + advance(frame)

        # A zombie does not so much walk as fall forwards and catch itself.
        bob = 0.035 * math.cos(step * 2.0) - 0.02 * hit
        lean = 0.13 + 0.30 * hit
        sway = 0.06 * math.sin(step)

        key(parts["Zombie_Hips"], frame,
            location=(x, 0.0, 0.95 + bob),
            rotation=(sway * 0.4, lean * 0.35, sway))
        key(parts["Zombie_Spine"], frame,
            rotation=(0.0, 0.16 + 0.22 * hit, 0.10 * math.sin(step) - 0.12 * hit))
        key(parts["Zombie_Head"], frame,
            rotation=(0.13 * math.sin(step * 0.5) + 0.10 * hit,
                      0.10 - 0.30 * hit, 0.16 * math.sin(step * 0.5)))
        key(parts["Zombie_Jaw"], frame, rotation=(0.0, 0.22 + 0.55 * hit, 0.0))

        for side, sign in (("L", 1.0), ("R", -1.0)):
            swing = math.sin(step + (0.0 if sign > 0.0 else math.pi))

            # Both arms are held out in front; the right one does the swiping.
            reach = -1.15 - 0.12 * swing * settled
            elbow = 0.55 - 0.15 * swing * settled
            if sign < 0.0:
                reach -= 1.05 * hit
                elbow -= 0.40 * hit
            else:
                reach -= 0.35 * hit
            key(parts["Zombie_ArmUpper" + side], frame,
                rotation=(0.10 * sign, reach, 0.16 * sign))
            key(parts["Zombie_ArmLower" + side], frame, rotation=(0.0, elbow, 0.0))
            key(parts["Zombie_Hand" + side], frame,
                rotation=(0.0, 0.35 - 0.30 * hit, 0.0))

            # The legs keep walking through the lunge, on a shorter stride.
            gait = 0.55 * swing * (1.0 - 0.55 * hit)
            knee = max(0.0, -0.9 * swing) * (1.0 - 0.5 * hit) + 0.12
            key(parts["Zombie_LegUpper" + side], frame, rotation=(0.0, gait, 0.0))
            key(parts["Zombie_LegLower" + side], frame, rotation=(0.0, knee, 0.0))
            key(parts["Zombie_Foot" + side], frame,
                rotation=(0.0, -0.35 * gait - 0.10, 0.0))


def main(argv):
    parser = argparse.ArgumentParser(prog="make_zombie_street")
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 1
    scene.frame_end = TOTAL

    surfaces = {
        "brick": material("WallBrick", textures.brick(), roughness=0.95),
        "concrete": material("WallConcrete", textures.concrete(), roughness=0.92),
        "stone": material("TrimStone", textures.stone(), roughness=0.8),
        "glass": material("WindowGlass", textures.glass_dark(), roughness=0.15,
                          metallic=0.1),
        "glow": material("WindowLit", textures.glass_lit(), roughness=0.5),
        "tarmac": material("RoadTarmac", textures.tarmac(), roughness=0.88),
        "paving": material("PathPaving", textures.paving(), roughness=0.9),
        "metal": material("LampMetal", textures.metal(), roughness=0.4, metallic=0.8),
        "lamp": material("LampGlow", None, emission=(1.0, 0.72, 0.36)),
        "skin": material("ZombieSkin", textures.skin(), roughness=0.85),
        "gore": material("ZombieGore", textures.gore(), roughness=0.55),
        "cloth": material("ZombieCloth", textures.cloth(), roughness=0.96),
    }

    parts = {}
    build_street(parts, surfaces)
    build_zombie(parts, surfaces)
    animate(parts)

    # The sky is not an object in the scene and the exporter only writes what a
    # material names, so it is written here, beside the file rather than into
    # it: the program loads it as a skybox.
    sky_path = os.path.join(os.path.dirname(os.path.abspath(args.out)),
                            "..", "sky", "dusk.png")
    sky_path = os.path.abspath(sky_path)
    os.makedirs(os.path.dirname(sky_path), exist_ok=True)
    sky = textures.dusk_sky()
    sky.file_format = "PNG"
    sky.save_render(sky_path)
    print("make_zombie_street: sky -> %s" % sky_path)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.out))
    print("make_zombie_street: %d objects -> %s" % (len(parts), args.out))
    return 0


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    sys.exit(main(argv))
