"""Build the street the zombie_street example loads.

    blender --background --factory-startup --python tools/blender/make_zombie_street.py -- --out resources/blender/zombie_street.blend

The zombie is one surface over a skeleton -- see zombie_figure -- and the walk
is a footstep plan rather than a swing: the stance foot is nailed to the place
it came down and the body travels over it, which is the difference between a
figure walking and a figure on castors.

The street is built rather than printed. Windows are openings with a reveal, a
sill, a glazing bar and glass at the back of them, because a window painted
into a wall tile forces that tile to span a whole storey and pins the entire
street at ninety texels to the metre. Everything is textured from generated
images (zombie_street_textures) with UVs projected per face at a fixed number
of repeats per metre, so a wall and a kerb carry brick and paving of the same
size whatever their own size, and every surface in the frame is held to one
standard of texel density.

The timeline is one clip in three phases at 24fps:

    frames   1- 64   walking up the street
    frames  65- 92   the lunge and the swipe
    frames  93-120   recovering, still walking

Seek to 3.2s over the agent channel and the zombie is mid-attack.
"""

import bpy
import bmesh
import mathutils
import argparse
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zombie_street_textures as textures
import zombie_figure as figure

FPS = 24
WALK_END = 64
ATTACK_END = 92
TOTAL = 120

# How many gait cycles a second. This is not a free choice. A leg reaches only
# so far sideways once it has reached down: 0.86 m of leg with 0.78 of it spent
# on the drop to the road leaves 0.36 to spare, so a foot can be planted 0.36
# ahead and leave 0.36 behind and no further. That is 0.72 m of street a stance,
# 1.45 m a cycle, and 0.9 m/s is 0.62 cycles a second; 0.66 keeps a margin. At
# 1.9 the feet were doing three times the steps the street needed and sliding
# the difference along the road.
# Three whole gait cycles in the 120 frames of the clip. This is not a rounding
# of the speed: a clip that ends part way through a cycle does not loop, and
# every time it restarts the limbs travel from wherever they stopped back to
# wherever they started. The neck was swinging 173 degrees at the seam and the
# shoulders 156, which reads as the head and arms turning all the way round.
STRIDE_RATE = 0.6
# And the speed follows from it. A leg reaches 0.362 m sideways once it has
# reached down, so 0.85 m/s at 0.6 cycles is a half-step of 0.354 and 0.9 would
# be 0.375 -- past what the leg can do, which the solver would clamp and the
# feet would slide.
WALK_SPEED = 0.85
WALK_START_X = -6.4


def material(name, image, roughness=0.9, metallic=0.0, emission=None, normal=None):
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

    # A normal map through a normal-map node, which is where every exporter
    # looks for one. Without it a wall catches the light as one flat plane and
    # no amount of resolution in the colour makes brick read as brick.
    if normal is not None:
        bump = tree.nodes.new("ShaderNodeTexImage")
        bump.image = normal
        bump.image.colorspace_settings.name = "Non-Color"
        bump.location = (-620.0, -160.0)
        shaper = tree.nodes.new("ShaderNodeNormalMap")
        shaper.location = (-320.0, -160.0)
        tree.links.new(bump.outputs["Color"], shaper.inputs["Color"])
        tree.links.new(shaper.outputs["Normal"], principled.inputs["Normal"])
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
# The figure stands closer to the camera than anything else in the scene, so it
# carries more texture across a metre than the street does.
FIGURE_REPEATS = 1.9

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
# The top of the plinth course, above the pavement.
PLINTH = 0.48


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
             glass, glow, pipes_surface, rng):
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
    # A plinth course at the foot of the wall. Where a wall meets the
    # pavement is where it gets kicked, splashed and leant on, and a terrace
    # is built with a harder stone there for that reason; it is also where
    # the eye reads whether the building stands on the street or floats over
    # it. Proud of the wall by more than the sills, so the corner it makes
    # with the pavement is a real one and the occlusion bake finds it.
    plinth = sign * -0.13
    _slab(band, (-half - 0.06, min(0.0, plinth), base),
          (half + 0.06, max(0.0, plinth), PLINTH))

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

    # Downpipes at the party walls, from the gutter to a shoe above the
    # plinth. Nothing breaks a facade's flatness like a vertical that stands
    # off it, and every terrace has one at every joint.
    pipes = bmesh.new()
    stand = sign * -0.16
    for px in (-half + 0.28, half - 0.28):
        _slab(pipes, (px - 0.055, min(stand, stand + sign * 0.11), PLINTH + 0.12),
              (px + 0.055, max(stand, stand + sign * 0.11), height - 0.30))
        # The bracket that holds it to the wall, one a storey.
        for storey in range(storeys + 1):
            z = base + storey * STOREY + 1.2
            if z >= height - 0.5:
                continue
            _slab(pipes, (px - 0.085, min(0.0, stand), z - 0.04),
                  (px + 0.085, max(0.0, stand), z + 0.04))
    pipes_obj = _finish(pipes, name + "_Pipes", pipes_surface, 3.0)
    pipes_obj.parent = shell_obj
    return shell_obj, trim_obj, dark_obj, lit_obj, pipes_obj


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


def roofline(parts, name, shell, width, depth, height, sign, surface, surfaces, rng):
    """What stands above the cornice.

    A terrace's roofline is the one edge in the frame that is drawn against
    the sky, and a row of flat tops is a row of boxes whatever their fronts
    are made of. Brick buildings carry chimney stacks; concrete ones carry a
    water tank on legs and an aerial. Every one of them is placed by the
    building's own random draw, so no two rooflines repeat.
    """
    half = width * 0.5
    back = sign * depth
    props = []
    if surface == "brick":
        for index in range(2 + int(rng.random() * 2)):
            x = rng.uniform(-half + 1.2, half - 1.2)
            y = back * rng.uniform(0.25, 0.7)
            tall = 1.35 + rng.uniform(0.0, 0.3)
            stack = block("%s_Stack%d" % (name, index),
                          (-0.38, -0.38, 0.0), (0.38, 0.38, tall),
                          surfaces["brick"], repeats=WALL_REPEATS, bevel=0.0)
            stack.parent = shell
            stack.location = (x, y, height - 0.05)
            props.append(stack)
            # The pot on top, and a cap over it, which is what makes a stack
            # read as a chimney rather than as a post.
            pot = block("%s_Pot%d" % (name, index),
                        (-0.16, -0.16, 0.0), (0.16, 0.16, 0.55),
                        surfaces["stone"], repeats=2.2, bevel=0.02, taper=0.85)
            pot.parent = stack
            pot.location = (0.0, 0.0, tall - 0.02)
            props.append(pot)
    else:
        x = rng.uniform(-half + 2.0, half - 2.0)
        y = back * rng.uniform(0.35, 0.65)
        tank = block("%s_Tank" % name, (-1.05, -1.05, 0.0), (1.05, 1.05, 1.7),
                     surfaces["paint"], repeats=1.6, bevel=0.06)
        tank.parent = shell
        tank.location = (x, y, height + 1.1)
        props.append(tank)
        for index, (lx, ly) in enumerate(((-0.8, -0.8), (0.8, -0.8), (0.8, 0.8), (-0.8, 0.8))):
            leg = block("%s_TankLeg%d" % (name, index),
                        (-0.06, -0.06, 0.0), (0.06, 0.06, 1.15),
                        surfaces["metal"], repeats=1.2, bevel=0.0)
            leg.parent = tank
            leg.location = (lx, ly, -1.15)
            props.append(leg)
        mast = block("%s_Aerial" % name, (-0.025, -0.025, 0.0), (0.025, 0.025, 3.2),
                     surfaces["metal"], repeats=1.2, bevel=0.0)
        mast.parent = shell
        mast.location = (rng.uniform(-half + 1.0, half - 1.0), back * 0.8, height - 0.05)
        props.append(mast)
        for index, z in enumerate((2.2, 2.7, 3.1)):
            bar = block("%s_AerialBar%d" % (name, index),
                        (-0.55 + index * 0.12, -0.015, -0.015), (0.55 - index * 0.12, 0.015, 0.015),
                        surfaces["metal"], repeats=1.2, bevel=0.0)
            bar.parent = mast
            bar.location = (0.0, 0.0, z)
            props.append(bar)
    # A parapet raised over part of the front, on about half of them. This is
    # the cheapest thing that stops two cornices reading as one line.
    if rng.random() < 0.55:
        span = rng.uniform(2.0, 2.7)
        at = rng.uniform(-half + span * 0.5 + 0.3, half - span * 0.5 - 0.3)
        raise_by = rng.uniform(0.5, 0.62)
        parapet = block("%s_Parapet" % name,
                        (-span * 0.5, min(0.0, sign * -0.20), 0.0),
                        (span * 0.5, max(0.0, sign * -0.20) + sign * 0.42, raise_by),
                        surfaces[surface], repeats=WALL_REPEATS, bevel=0.0)
        parapet.parent = shell
        parapet.location = (at, 0.0, height - 0.02)
        props.append(parapet)
        coping = block("%s_Coping" % name,
                       (-span * 0.5 - 0.08, min(0.0, sign * -0.26), 0.0),
                       (span * 0.5 + 0.08, max(0.0, sign * -0.26) + sign * 0.58, 0.12),
                       surfaces["stone"], repeats=2.2, bevel=0.015)
        coping.parent = parapet
        coping.location = (0.0, 0.0, raise_by - 0.01)
        props.append(coping)
    for prop in props:
        parts[prop.name] = prop


# The pavement top is at 0.14. A prop resting its base exactly there shares
# that plane with it and the depth test has nothing to choose between; sunk a
# few centimetres, its base is inside the slab, the way the road sinks into
# the ground and the kerbs into the road.
PROP_FOOT = 0.10


def build_clutter(parts, surfaces, rng):
    """What a street has on it that nobody put there on purpose.

    A pavement with nothing on it is a corridor. Bins against the walls, sacks
    beside them, crates where a shop takes its deliveries, bollards along the
    kerb where cars used to mount it, and a bench. Each is placed by a random
    draw seeded once, so the street is the same street every time it is built
    and no two stretches of it are the same stretch.

    Blender Y is across the street: +5 is the far pavement, -5 the near one.
    """
    props = []

    def bin_at(index, x, side):
        y = side * 6.05
        body = block("Street_Bin%d" % index, (-0.29, -0.36, 0.0), (0.29, 0.36, 0.98),
                     surfaces["paint"], repeats=1.6, bevel=0.03, taper=1.06)
        body.location = (x, y, PROP_FOOT)
        body.rotation_euler = (0.0, 0.0, rng.uniform(-0.25, 0.25))
        lid = block("Street_BinLid%d" % index, (-0.31, -0.38, 0.0), (0.31, 0.38, 0.08),
                    surfaces["paint"], repeats=1.6, bevel=0.025, taper=0.9)
        lid.parent = body
        lid.location = (0.0, 0.0, 0.98)
        lid.rotation_euler = (rng.uniform(0.0, 0.35), 0.0, 0.0)
        props.extend((body, lid))

    def sack_at(index, x, y):
        sack = block("Street_Sack%d" % index, (-0.30, -0.26, 0.0), (0.30, 0.26, 0.42),
                     surfaces["sack"], repeats=1.6, bevel=0.11, taper=0.62)
        sack.location = (x, y, PROP_FOOT)
        sack.rotation_euler = (0.0, 0.0, rng.uniform(0.0, 6.28))
        props.append(sack)

    def crate_at(index, x, y, layers):
        below = None
        for layer in range(layers):
            crate = block("Street_Crate%d_%d" % (index, layer),
                          (-0.26, -0.21, 0.0), (0.26, 0.21, 0.30),
                          surfaces["wood"], repeats=1.7, bevel=0.012)
            if below is None:
                crate.location = (x, y, PROP_FOOT)
                crate.rotation_euler = (0.0, 0.0, rng.uniform(-0.3, 0.3))
            else:
                crate.parent = below
                crate.location = (rng.uniform(-0.03, 0.03), rng.uniform(-0.03, 0.03), 0.30)
                crate.rotation_euler = (0.0, 0.0, rng.uniform(-0.2, 0.2))
            props.append(crate)
            below = crate

    def bollard_at(index, x, side):
        post = block("Street_Bollard%d" % index, (-0.075, -0.075, 0.0), (0.075, 0.075, 0.92),
                     surfaces["paint"], repeats=1.6, bevel=0.02, taper=0.8)
        post.location = (x, side * 3.62, PROP_FOOT)
        props.append(post)

    def bench_at(index, x, side):
        y = side * 5.6
        seat = block("Street_Bench%d" % index, (-0.85, -0.24, 0.0), (0.85, 0.24, 0.05),
                     surfaces["wood"], repeats=1.7, bevel=0.008)
        seat.location = (x, y, PROP_FOOT + 0.44)
        for end, ex in enumerate((-0.78, 0.78)):
            leg = block("Street_BenchEnd%d_%d" % (index, end),
                        (-0.04, -0.22, 0.0), (0.04, 0.22, 0.44),
                        surfaces["metal"], repeats=1.2, bevel=0.0)
            leg.parent = seat
            leg.location = (ex, 0.0, -0.44)
            props.append(leg)
        back = block("Street_BenchBack%d" % index, (-0.85, -0.03, 0.0), (0.85, 0.03, 0.34),
                     surfaces["wood"], repeats=1.7, bevel=0.008)
        back.parent = seat
        back.location = (0.0, side * 0.2, 0.12)
        back.rotation_euler = (side * -0.18, 0.0, 0.0)
        props.append(seat)
        props.append(back)

    # The near pavement is the one the camera stands on and the far one is the
    # one it looks across at, so both are dressed, and more thickly where the
    # walk begins than where it ends.
    for index, x in enumerate((-27.5, -19.0, -11.5, -3.0, 5.5)):
        bin_at(index, x + rng.uniform(-0.4, 0.4), 1.0)
    for index, x in enumerate((-23.0, -14.5, -7.0, 2.0)):
        bin_at(10 + index, x + rng.uniform(-0.4, 0.4), -1.0)
    for index, (x, y) in enumerate(((-26.8, 5.6), (-18.2, 5.9), (-10.9, 5.5), (-22.3, -5.7),
                                    (-13.8, -5.9), (-6.3, -5.4), (-2.3, 5.8))):
        sack_at(index, x + rng.uniform(-0.3, 0.3), y)
    crate_at(0, -15.6, 6.0, 3)
    crate_at(1, -15.0, 6.05, 2)
    crate_at(2, -8.4, -6.1, 2)
    crate_at(3, 3.8, 6.0, 3)
    for index, x in enumerate((-29.0, -25.0, -21.0, -17.0, -13.0, -9.0, -5.0, -1.0)):
        bollard_at(index, x, 1.0)
    for index, x in enumerate((-24.0, -16.0, -8.0, 0.0)):
        bollard_at(10 + index, x, -1.0)
    bench_at(0, -20.5, 1.0)
    bench_at(1, -4.5, -1.0)

    for prop in props:
        parts[prop.name] = prop


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
        shell, trim, dark, lit, pipes = building(name, 11.2, depth, height,
                                                 -0.4 - index * 0.03, surfaces[surface],
                                                 WALL_REPEATS, 1.0, surfaces["stone"],
                                                 surfaces["glass"], surfaces["glow"],
                                                 surfaces["paint"], rng)
        # A terrace is not machined: each front sets back a few centimetres
        # from its neighbour, which is also what keeps two of them from sharing
        # the plane they face the street in.
        shell.location = (x, 6.55 + index * 0.04, 0.0)
        parts[name] = shell
        parts[name + "_Trim"] = trim
        parts[name + "_Glass"] = dark
        parts[name + "_Lit"] = lit
        parts[name + "_Pipes"] = pipes
        roofline(parts, name, shell, 11.2, depth, height, 1.0, surface, surfaces, rng)

    near = ((-16.0, 8.0, 11.0, "concrete"), (2.0, 9.0, 14.0, "brick"),
            (20.0, 8.0, 12.0, "concrete"))
    for index, (x, depth, height, surface) in enumerate(near):
        name = "Street_BlockR%d" % index
        shell, trim, dark, lit, pipes = building(name, 13.0, depth, height,
                                                 -0.55 - index * 0.03, surfaces[surface],
                                                 WALL_REPEATS, -1.0, surfaces["stone"],
                                                 surfaces["glass"], surfaces["glow"],
                                                 surfaces["paint"], rng)
        shell.location = (x, -7.55 - index * 0.04, 0.0)
        parts[name] = shell
        parts[name + "_Trim"] = trim
        parts[name + "_Glass"] = dark
        parts[name + "_Lit"] = lit
        parts[name + "_Pipes"] = pipes
        roofline(parts, name, shell, 13.0, depth, height, -1.0, surface, surfaces, rng)

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


# The crowd's zombie is not this zombie. A full figure is ~26k triangles, and
# half a million of those is thirteen billion a frame -- the crowd cannot draw
# the hero mesh. So a second, decimated figure is built beside it: grown on the
# same skeleton at the lowest subdivision (the skin hull, ~340 triangles) and
# collapsed to about half that. The render-scale sweep (tests/test_crowd_render_
# scale.ae) measured the budget -- half a million instances stay inside a 60fps
# frame at ~150-200 triangles a figure -- and this lands there. Tunable: raise
# the ratio for a rounder zombie at fewer frames, lower it for more crowd.
LOD_SUBDIV = 0
LOD_DECIMATE_RATIO = 0.5


def _decimate(obj, ratio):
    """Collapse a mesh to a fraction of its faces, in place.

    Applied here, before the rig's weights are assigned, so the weights land on
    the mesh that is actually drawn rather than on geometry the collapse then
    throws away. Same apply-the-modifier path the skin and subsurf take in
    figure.build_body, so the mesh that leaves is plain geometry with no
    modifier stack for the exporter to have an opinion about.
    """
    bpy.context.view_layer.objects.active = obj
    modifier = obj.modifiers.new("Decimate", "DECIMATE")
    modifier.decimate_type = "COLLAPSE"
    modifier.ratio = ratio
    bpy.ops.object.modifier_apply(modifier="Decimate")


def build_zombie(parts, surfaces):
    """One surface over a skeleton, and the skeleton the walk is written on.

    Two meshes rather than one, because a zombie is skin at the head and the
    hands and cloth everywhere else, and the exporter writes the material an
    object names. Both hang off the same rig, so they are one figure: a weight
    that blends across a shoulder blends across it in both.

    A third mesh, Zombie_Body_LOD, is the low-poly stand-in the crowd draws: the
    same body grown coarse and collapsed to the crowd's triangle budget, bound
    to the same rig so the one baked walk drives it too. It is skin only -- at
    the distance half a million of them are seen, the cloth seam the hero figure
    splits into a second mesh for is not there to see.
    """
    body = figure.build_body("Zombie_Body", surfaces["skin"])
    figure.unwrap(body, FIGURE_REPEATS)
    clothes = figure.build_clothes("Zombie_Clothes", surfaces["cloth"])
    figure.unwrap(clothes, FIGURE_REPEATS)

    crowd_lod = figure.build_body("Zombie_Body_LOD", surfaces["skin"],
                                  subdivisions=LOD_SUBDIV)
    _decimate(crowd_lod, LOD_DECIMATE_RATIO)
    figure.unwrap(crowd_lod, FIGURE_REPEATS)

    rig = figure.build_rig("Zombie_Rig")
    figure.bind(rig, (body, clothes, crowd_lod))

    parts["Zombie_Body"] = body
    parts["Zombie_Clothes"] = clothes
    parts["Zombie_Body_LOD"] = crowd_lod
    return rig


def phase(frame):
    """How far through the gait this frame is."""
    return (frame - 1) / FPS * STRIDE_RATE * math.tau


def strike(frame):
    """0 outside the lunge, 1 at the moment of the swipe."""
    if frame <= WALK_END or frame > ATTACK_END:
        return 0.0
    t = (frame - WALK_END) / float(ATTACK_END - WALK_END)
    return math.sin(t * math.pi) ** 1.4


def advance(frame):
    """How far up the street the figure has travelled by this frame."""
    walked = min(frame, WALK_END + 1) - 1
    distance = walked / FPS * WALK_SPEED
    if frame > WALK_END:
        lunge = min(frame, ATTACK_END) - WALK_END
        span = float(ATTACK_END - WALK_END)
        distance += 0.75 * math.sin(lunge / span * math.pi * 0.5)
    if frame > ATTACK_END:
        distance += (frame - ATTACK_END) / FPS * WALK_SPEED * 0.8
    return distance


def _turn(axis, angle):
    return mathutils.Quaternion(axis, angle)


def _compose(*turns):
    out = mathutils.Quaternion((1.0, 0.0, 0.0, 0.0))
    for turn in turns:
        out = out @ turn
    return out


X = mathutils.Vector((1.0, 0.0, 0.0))
Y = mathutils.Vector((0.0, 1.0, 0.0))
Z = mathutils.Vector((0.0, 0.0, 1.0))

# The right leg is the one that does not work. Everything asymmetric about the
# walk comes from this one number: that side takes a shorter step, lifts less,
# and the hip has to be hauled round to bring it through.
DRAG = 0.45


# A gait cycle, and where in it the foot is on the road. Stance is the first
# half: the heel lands in front, the body travels over it, the toe leaves
# behind. The second half is the leg coming forward through the air, and it is
# the only half the knee folds in.
UPPER_LEG = 0.44
LOWER_LEG = 0.42
# Where the ankle sits when the foot is down: the road surface, plus the height
# of the ankle above the sole. The road has a camber and the figure walks along
# its crown, so the surface there is 0.075 and not zero -- planting at zero put
# the figure into the road up to its ankles.
ROAD = 0.165
# How high a foot is carried over the road on its way through. A zombie clears
# it by very little.
LIFT = 0.16


def _cycle_frame(cycle, offset):
    """The frame at which this leg reaches this point of its cycle."""
    return 1.0 + (cycle - offset) * FPS / (STRIDE_RATE * math.tau)


def _step_at(cycle, offset):
    """Where the foot comes down for the stance beginning at this cycle.

    Half of what the body travels in a stance ahead of where the body is, so
    the hip passes over the foot rather than dragging it along behind.
    """
    frame = _cycle_frame(cycle, offset)
    return WALK_START_X + advance(frame) + 0.5 * WALK_SPEED / (2.0 * STRIDE_RATE)


def foot_target(cycle, offset, lame=0.0):
    """Where a foot should be, as a function of where the walk has got to.

    A footstep plan rather than a swing: the stance foot is nailed to the place
    it came down and the swing foot is carried from there to the next place,
    lifted over the road and eased into both ends. Everything about a walk that
    reads as weight comes out of this -- a foot that is put down and stays down,
    and a body that travels over it.

    Planned rather than swung, so the stride cannot disagree with the speed. The
    two used to, and the difference came out as a quarter of a metre of skating
    every frame the foot was down.
    """
    # Both halves of this decision come off the same number. Asking sin(cycle)
    # which half of the cycle this is, and floor(cycle / tau) which cycle it is,
    # is two measures of one instant, and at the seam they disagree: sin(tau) is
    # -2.4e-16, so the exact end of a cycle reads as swing, while the floor has
    # already moved on to the next stance. That put `through` at -1, where the
    # smoothstep t*t*(3-2t) is 5 rather than 0, and threw the foot five strides
    # up the street for the single frame the walk landed on a cycle boundary.
    stance_start = math.floor(cycle / math.tau) * math.tau
    into = cycle - stance_start
    here = _step_at(stance_start, offset)
    if into < math.pi:
        return mathutils.Vector((here, 0.0, ROAD)), 0.0

    ahead = _step_at(stance_start + math.tau, offset)
    through = (into - math.pi) / math.pi
    # Smoothstep, so the foot leaves the road and meets it again with no
    # horizontal speed. Landing with speed and stopping dead is the pop that
    # says "animation" louder than anything else in a walk.
    eased = through * through * (3.0 - 2.0 * through)
    # The bad leg barely clears the road. Expressed here, in where the foot is
    # asked to go, because that is the only place it can be expressed without
    # contradicting the solve that puts it there.
    carried = math.sin(through * math.pi) * (1.0 - 0.6 * lame)
    return (mathutils.Vector((here + (ahead - here) * eased, 0.0, ROAD + LIFT * carried)),
            carried)


def _leg_to(pose, side, target, carried, hit):
    """Solve the leg so the ankle sits on the target the plan gave it.

    Nothing is added to the angles the solve returns. Adding to the knee after
    solving for it is not a limp, it is a different pose: the ankle stops
    landing on the target and the foot swings up to knee height. It did, by a
    quarter of a metre, on the leg that was meant to be dragging.
    """
    lame = DRAG if side == "R" else 0.0
    hip = pose.world()["Thigh" + side].to_translation()
    aim = mathutils.Vector((target.x, hip.y, target.z))
    thigh, knee = figure.solve_leg(hip, aim, UPPER_LEG, LOWER_LEG)
    pose.set_world("Thigh" + side, _compose(_turn(Y, thigh),
                                            _turn(X, (0.05 if side == "L" else -0.05))))
    pose.set("Knee" + side, _turn(Y, knee))
    # Flat to the road while it is on it, and hanging while it is not: the sole
    # follows the ground rather than the shin.
    flat = 0.0 - thigh - knee
    pose.set("Ankle" + side, _turn(Y, flat * (1.0 - carried) - 0.22 * carried
                                   - 0.24 * lame))
    pose.set("Toe" + side, _turn(Y, 0.0))


def _arm(pose, side, swing, hit, reach_for):
    """One arm, hanging forward the way a zombie carries them.

    Both are held up and out; the right one is the one that swipes. The
    shoulder leads, the elbow follows a beat later and the wrist later still,
    which is what makes a swing read as a whip rather than a gate.
    """
    lead = 1.0 if side == "R" else 0.55
    # At rest the arms hang low and loose in front, the shuffle a zombie
    # carries them at -- not held up level, which is a mannequin with its arms
    # out. They sway with the step, further than the body does, and the left
    # hangs a little lower and limper than the right that swipes, so the two
    # are not a mirrored pair. The strike thrusts to horizontal and straightens
    # the elbow, so the hand is carried a good half-metre further forward than
    # the walk ever takes it -- the reach is the arm's, not the body's lunge.
    hang = 0.0 if side == "R" else 0.12
    forward = -0.44 + hang - 0.30 * swing - reach_for * 0.95 * lead
    out = (0.20 if side == "L" else -0.20) - reach_for * 0.10 * lead
    wrist = 0.30 - reach_for * 0.55 * lead
    # The elbow bends about the axis across the arm, not along it: a rotation
    # about the bone's own length is a twist and leaves the arm as straight as
    # it found it, which is why a zombie posed only in twists reaches with a
    # dead-straight arm. Kept bent in the shuffle and flexing with the sway --
    # a fixed bend is what made the arm read as a rod -- then driven straight
    # at the strike, so the thrust has a bend to spend.
    bend = 1.55 + 0.35 * swing - reach_for * 1.8 * lead

    pose.set("Shoulder" + side, _compose(_turn(Y, forward), _turn(X, out),
                                         _turn(Z, -0.12 * lead * reach_for)))
    pose.set("Elbow" + side, _compose(_turn(X, bend), _turn(Z, 0.10 * lead)))
    pose.set("Wrist" + side, _turn(Y, wrist))
    pose.set("Hand" + side, _turn(Y, 0.15 + 0.35 * reach_for * lead))


def animate(rig):
    """The walk, the lunge, and the recovery, on the bones.

    What makes this read as a body rather than as a rig: the hips fall onto
    whichever leg is taking the weight and rise at push-off, the shoulders turn
    against the pelvis, and the head arrives at everything a few frames after
    the body does. The lag is the whole of it -- a head that turns with the
    chest belongs to a mannequin.
    """
    pose = figure.Pose(rig)
    for frame in range(1, TOTAL + 1):
        step = phase(frame)
        hit = strike(frame)
        # The head is late to everything, and the chest a little late too.
        late = phase(frame - 3.0)
        chest_late = phase(frame - 1.5)

        swing_l = math.sin(step)
        swing_r = math.sin(step + math.pi)

        # Down onto each foot and up off it: twice a cycle, and deeper on the
        # side that is not carrying itself.
        drop = -0.080 - 0.022 * math.cos(step * 2.0) - 0.014 * max(0.0, swing_r)
        lean = 0.17 + 0.34 * hit
        roll = 0.075 * math.sin(step) - 0.02
        yaw = -0.10 * math.sin(step) - 0.05

        pose.set("Hips", _compose(_turn(Y, lean * 0.42), _turn(X, roll),
                                  _turn(Z, yaw)),
                 shift=(0.0, 0.0, drop))
        pose.set("Spine", _compose(_turn(Y, lean * 0.30),
                                   _turn(Z, -yaw * 0.55),
                                   _turn(X, -roll * 0.35)))
        # The shoulders turn against the pelvis, and later than it.
        pose.set("Chest", _compose(_turn(Y, lean * 0.34 + 0.10 * hit),
                                   _turn(Z, 0.16 * math.sin(chest_late) - yaw * 0.4),
                                   _turn(X, -roll * 0.5)))
        pose.set("Neck", _compose(_turn(Y, -lean * 0.30 + 0.16 * hit),
                                  _turn(Z, 0.09 * math.sin(late))))
        pose.set("Head", _compose(_turn(Y, -lean * 0.34 + 0.10 - 0.34 * hit),
                                  _turn(Z, 0.13 * math.sin(late)),
                                  _turn(X, 0.10 * math.sin(late * 0.5) + 0.06)))

        # Where the figure is, rather than what it is doing. This goes on a
        # bone rather than on the armature because a bone is what the engine is
        # given: the armature is not a mesh, so nothing exports it, and a walk
        # carried there would arrive with the figure treading water.
        pose.set("Root", _turn(Z, -0.06 + 0.03 * math.sin(step)),
                 shift=(WALK_START_X + advance(frame), 0.0, 0.0))
        # Both legs the same way: to wherever the footstep plan says the foot
        # is. A stance leg and a swing leg differ in what the plan asks of them,
        # not in how they are driven.
        for side, offset in (("L", 0.0), ("R", math.pi)):
            lame = DRAG if side == "R" else 0.0
            target, carried = foot_target(step + offset, offset, lame)
            _leg_to(pose, side, target, carried, hit)

        _arm(pose, "L", swing_r, hit, hit * 0.55)
        _arm(pose, "R", swing_l, hit, hit)
        pose.apply(frame)

    # Every frame is keyed, so there is nothing between two keys for a curve to
    # shape. Bezier here would only make the exporter resample a curve it
    # already has at full rate.
    for curve in figure.action_curves(rig):
        for point in curve.keyframe_points:
            point.interpolation = "LINEAR"


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
        "brick": material("WallBrick", textures.brick(), roughness=0.95,
                          normal=textures.brick_normal()),
        "concrete": material("WallConcrete", textures.concrete(), roughness=0.92,
                             normal=textures.concrete_normal()),
        "stone": material("TrimStone", textures.stone(), roughness=0.8,
                          normal=textures.stone_normal()),
        "glass": material("WindowGlass", textures.glass_dark(), roughness=0.15,
                          metallic=0.1),
        "glow": material("WindowLit", textures.glass_lit(), roughness=0.5),
        # Wet. The lamps and the lit windows are reflected in the road, which
        # is most of what a night street looks like, and all of it comes from
        # the roughness: the colour is only darker for it.
        "tarmac": material("RoadTarmac", textures.wet_tarmac(), roughness=0.16,
                           normal=textures.tarmac_normal()),
        "paving": material("PathPaving", textures.paving(), roughness=0.9,
                           normal=textures.paving_normal()),
        "metal": material("LampMetal", textures.metal(), roughness=0.4, metallic=0.8),
        "wood": material("PropWood", textures.wood(), roughness=0.85,
                         normal=textures.wood_normal()),
        "paint": material("PropPaint", textures.painted_metal(), roughness=0.55,
                          metallic=0.15, normal=textures.painted_metal_normal()),
        "sack": material("PropSack", textures.sacking(), roughness=0.35,
                         normal=textures.sacking_normal()),
        "lamp": material("LampGlow", None, emission=(1.0, 0.72, 0.36)),
        "skin": material("ZombieSkin", textures.skin(), roughness=0.85,
                         normal=textures.skin_normal()),
        "gore": material("ZombieGore", textures.gore(), roughness=0.55),
        "cloth": material("ZombieCloth", textures.cloth(), roughness=0.96,
                          normal=textures.cloth_normal()),
    }

    parts = {}
    build_street(parts, surfaces)
    build_clutter(parts, surfaces, random.Random(6211))
    animate(build_zombie(parts, surfaces))

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
