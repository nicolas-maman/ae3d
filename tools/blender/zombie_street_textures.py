"""The images the zombie street is surfaced with, generated rather than shipped.

Every texture here is built from a seeded PRNG, so the same file comes out of
every run on every machine: the .blend is committed, and a texture that drifted
would make it a different file each time it was rebuilt.

The images are packed into the .blend as generated images. ae3d_export writes
them out beside the material that names them.
"""

import bpy
import math
import random

import numpy


def _image(name, size, pixels):
    """pixels is an (size, size, 4) float array, bottom row first."""
    image = bpy.data.images.new(name, width=size, height=size, alpha=False)
    image.colorspace_settings.name = "sRGB"
    image.pixels.foreach_set(pixels.reshape(-1))
    image.pack()
    return image


def _noise(rng, size, octaves, base):
    """Value noise: a few octaves of random lattices, bilinearly resampled."""
    out = numpy.zeros((size, size), dtype=numpy.float32)
    amplitude = 1.0
    total = 0.0
    for octave in range(octaves):
        cells = base * (2 ** octave)
        lattice = numpy.array(
            [[rng.random() for _ in range(cells + 1)] for _ in range(cells + 1)],
            dtype=numpy.float32)
        axis = numpy.linspace(0.0, cells, size, endpoint=False, dtype=numpy.float32)
        low = numpy.floor(axis).astype(numpy.int32)
        frac = axis - low
        smooth = frac * frac * (3.0 - 2.0 * frac)
        rows = (lattice[low, :] * (1.0 - smooth)[:, None] +
                lattice[low + 1, :] * smooth[:, None])
        sampled = (rows[:, low] * (1.0 - smooth)[None, :] +
                   rows[:, low + 1] * smooth[None, :])
        out += sampled * amplitude
        total += amplitude
        amplitude *= 0.5
    return out / total


def _rgba(rgb):
    height, width, _ = rgb.shape
    out = numpy.ones((height, width, 4), dtype=numpy.float32)
    out[:, :, 0:3] = numpy.clip(rgb, 0.0, 1.0)
    return out


def _tint(base, shade, amount):
    """base and shade are RGB triples; amount is a (size, size) field in 0..1."""
    field = amount[:, :, None]
    return numpy.array(base, dtype=numpy.float32)[None, None, :] * (1.0 - field) + \
        numpy.array(shade, dtype=numpy.float32)[None, None, :] * field


def _windows(rgb, size, rng, sill):
    """Two courses of two windows, painted into a wall tile.

    Kept for nothing: windows are built as openings now, with a reveal, a sill
    and glass set back behind it. Painting them into the tile forced the tile to
    span a whole storey, which is what pinned the whole street at ninety texels
    to the metre -- a texel every eleven millimetres, which is a smear.
    """
    lit = [(1.00, 0.78, 0.44), None, None, (0.86, 0.62, 0.30)]
    rng.shuffle(lit)
    pane = numpy.array((0.045, 0.050, 0.065), dtype=numpy.float32)
    frame = numpy.array(sill, dtype=numpy.float32)

    half = size // 2
    width = int(size * 0.20)
    height = int(size * 0.24)
    edge = max(1, size // 128)

    for row in range(2):
        for column in range(2):
            glow = lit[row * 2 + column]
            cx = column * half + half // 2
            cy = row * half + half // 2
            x0, x1 = cx - width // 2, cx + width // 2
            y0, y1 = cy - height // 2, cy + height // 2
            rgb[y0 - edge:y1 + edge, x0 - edge:x1 + edge] = frame
            if glow is None:
                rgb[y0:y1, x0:x1] = pane
            else:
                colour = numpy.array(glow, dtype=numpy.float32)
                # Brighter towards the top, the way a room lit from its ceiling
                # falls off towards the sill.
                ramp = numpy.linspace(0.55, 1.0, y1 - y0, dtype=numpy.float32)
                rgb[y0:y1, x0:x1] = colour[None, None, :] * ramp[:, None, None]
            # A glazing bar down the middle.
            rgb[y0:y1, cx - edge // 2 - 1:cx + edge // 2 + 1] = frame
    return rgb


def brick(size=1024, seed=11):
    """Courses of brick, offset every other row, with mortar between.

    Sixteen courses across a tile that covers 1.2 metres is a course of 75mm,
    which is what a brick is. The tile carries nothing else, so it can be this
    small and still tile without a pattern anybody can see.

    A wall is not one brick repeated. Every brick here gets its own tone -- a
    little lighter or darker, a little more orange or more purple-brown -- the
    way a kiln fires a batch unevenly, and the mortar is grubbier in some
    joints than others. Over that, weathering: dark patches where rain has
    streaked and soot has settled, and the odd spalled brick with a chipped,
    darker face. One flat colour with a per-row shade read as a cartoon tile,
    and no lighting could make it brick.
    """
    rng = random.Random(seed)
    rows, columns = 16, 6
    grain = _noise(rng, size, 4, 4)        # fine surface texture
    weather = _noise(rng, size, 3, 2)      # broad soot and rain patches
    spall = _noise(rng, size, 5, 16)       # small chips and pits
    field = numpy.zeros((size, size), dtype=numpy.float32)
    shade = numpy.zeros((size, size), dtype=numpy.float32)
    warmth = numpy.zeros((size, size), dtype=numpy.float32)

    row_height = size / rows
    column_width = size / columns
    mortar = max(1.0, size / 128.0)

    # Each brick's own tone, keyed by course and position so it tiles.
    tones = {}
    for y in range(size):
        row = int(y / row_height)
        in_row = y - row * row_height
        offset = 0.5 * column_width if row % 2 else 0.0
        for x in range(size):
            u = (x + offset) % size
            index = int(u / column_width)
            in_column = u % column_width
            joint = (in_row < mortar or in_column < mortar or
                     in_row > row_height - mortar or in_column > column_width - mortar)
            field[y, x] = 1.0 if joint else 0.0
            key = (row, index)
            tone = tones.get(key)
            if tone is None:
                # Mostly near the base tone; now and then a clearly odd brick.
                if rng.random() < 0.12:
                    tone = (rng.uniform(-0.30, 0.28), rng.uniform(-0.14, 0.16))
                else:
                    tone = (rng.uniform(-0.14, 0.14), rng.uniform(-0.07, 0.08))
                tones[key] = tone
            shade[y, x] = tone[0]
            warmth[y, x] = tone[1]

    face = numpy.array((0.42, 0.20, 0.15), dtype=numpy.float32)
    joint_colour = numpy.array((0.52, 0.50, 0.47), dtype=numpy.float32)

    # Per-brick brightness and warmth: warmth pushes red up and blue down.
    rgb = face[None, None, :] * (1.0 + shade[:, :, None])
    rgb[:, :, 0] *= 1.0 + warmth
    rgb[:, :, 2] *= 1.0 - warmth
    rgb *= 1.0 + (grain[:, :, None] - 0.5) * 0.35

    # Weathering: broad dark patches of soot and rain-streaking, on brick and
    # mortar alike, and a scatter of spalled, chipped faces on the bricks.
    soot = numpy.clip((0.52 - weather) * 1.6, 0.0, 1.0) * 0.38
    chips = numpy.clip((spall - 0.64) * 5.0, 0.0, 1.0) * 0.55
    rgb *= (1.0 - soot[:, :, None])
    rgb *= (1.0 - (chips * (1.0 - field))[:, :, None])

    # Mortar: uneven, and grubby where the weather has got into the joint.
    joint_var = 1.0 + (grain - 0.5) * 0.5 - numpy.clip((0.56 - weather) * 1.8, 0.0, 1.0) * 0.45
    joint_rgb = joint_colour[None, None, :] * joint_var[:, :, None]
    rgb = rgb * (1.0 - field[:, :, None]) + joint_rgb * field[:, :, None]
    return _image("BrickWall", size, _rgba(rgb))


def _concrete_panels(size):
    """Where a precast panel meets its neighbour, and the form-tie holes down
    each joint. A blank concrete wall is a slab; the joints and the ties are
    what say it was cast in pieces and lifted into place."""
    seam = numpy.zeros((size, size), dtype=numpy.float32)
    tie = numpy.zeros((size, size), dtype=numpy.float32)
    groove = max(1.0, size / 220.0)
    panels = 3
    pitch = size / panels
    axis = numpy.arange(size)
    near = numpy.minimum(axis % pitch, pitch - (axis % pitch))
    line = (near < groove).astype(numpy.float32)
    seam = numpy.maximum(line[None, :], line[:, None])
    # Two tie holes down every vertical joint.
    radius = max(1.5, size / 300.0)
    for col in range(1, panels):
        cx = col * pitch
        for row in (0.32, 0.68):
            cy = row * size
            yy, xx = numpy.ogrid[0:size, 0:size]
            tie = numpy.maximum(tie, ((xx - cx) ** 2 + (yy - cy) ** 2 < radius * radius).astype(numpy.float32))
    return seam, tie


def concrete(size=1024, seed=23):
    rng = random.Random(seed)
    grain = _noise(rng, size, 5, 6)
    streak = _noise(rng, size, 4, 3)
    stain = numpy.clip((streak - 0.5) * 2.0, 0.0, 1.0)
    rgb = _tint((0.40, 0.40, 0.39), (0.21, 0.21, 0.22), stain * 0.8)
    rgb *= (0.84 + grain[:, :, None] * 0.32)
    # The joints sit in shadow and the tie holes darker still.
    seam, tie = _concrete_panels(size)
    rgb *= (1.0 - 0.35 * seam[:, :, None])
    rgb *= (1.0 - 0.5 * tie[:, :, None])
    return _image("ConcreteWall", size, _rgba(rgb))


def tarmac(size=1024, seed=37):
    rng = random.Random(seed)
    grit = _noise(rng, size, 6, 10)
    patch = _noise(rng, size, 3, 5)
    rgb = _tint((0.070, 0.070, 0.076), (0.105, 0.104, 0.110),
                numpy.clip(patch * 1.4 - 0.35, 0.0, 1.0))
    rgb *= (0.76 + grit[:, :, None] * 0.48)
    return _image("RoadTarmac", size, _rgba(rgb))


def paving(size=1024, seed=53):
    """Slabs, with a groove between them. Two across a 1.2 metre tile is a
    slab of 600mm, which is the one a pavement is laid from.

    A pavement is not four identical slabs. Each is its own shade; grime
    gathers in the joints and along every slab's edge where feet do not scuff
    it off; broad patches lie damp and dark; and the surface is speckled with
    grit and trodden-in gum. Four flat grey squares read as a floor tile in a
    game, and no lighting could make them a street.
    """
    rng = random.Random(seed)
    grain = _noise(rng, size, 5, 4)
    damp = _noise(rng, size, 3, 2)        # broad damp and dirty patches
    speck = _noise(rng, size, 6, 24)      # grit and trodden gum
    slabs = 2
    pitch = size / slabs
    groove = max(1.0, size / 96.0)
    field = numpy.zeros((size, size), dtype=numpy.float32)
    shade = numpy.zeros((size, size), dtype=numpy.float32)
    edge = numpy.zeros((size, size), dtype=numpy.float32)
    tones = {}
    for y in range(size):
        row = int(y / pitch)
        in_row = y % pitch
        for x in range(size):
            column = int(x / pitch)
            in_column = x % pitch
            joint = (in_row < groove or in_column < groove)
            field[y, x] = 1.0 if joint else 0.0
            key = (row, column)
            tone = tones.get(key)
            if tone is None:
                tone = rng.uniform(-0.11, 0.11)
                tones[key] = tone
            shade[y, x] = tone
            # How far into the slab from its nearest edge, as a fraction of
            # the slab; grime falls off away from the joint.
            edge[y, x] = min(in_row, in_column, pitch - in_row, pitch - in_column) / pitch

    base = numpy.array((0.46, 0.45, 0.43), dtype=numpy.float32)
    groove_colour = numpy.array((0.22, 0.22, 0.22), dtype=numpy.float32)
    rgb = base[None, None, :] * (1.0 + shade[:, :, None])
    rgb *= (0.86 + grain[:, :, None] * 0.28)

    # Grime along every slab edge, damp patches across the whole surface, and
    # a scatter of dark specks.
    # Broken up by the grain, or it reads as a bevel around every slab.
    edge_grime = numpy.clip(1.0 - edge / 0.10, 0.0, 1.0) * 0.26 * (0.45 + grain * 1.1)
    dampness = numpy.clip((0.50 - damp) * 1.6, 0.0, 1.0) * 0.30
    specks = numpy.clip((speck - 0.70) * 6.0, 0.0, 1.0) * 0.45
    rgb *= (1.0 - edge_grime[:, :, None])
    rgb *= (1.0 - dampness[:, :, None])
    rgb *= (1.0 - specks[:, :, None])

    # The joint itself, uneven and dirtier where the surface is damp.
    joint_rgb = groove_colour[None, None, :] * (1.0 + (grain[:, :, None] - 0.5) * 0.4 - dampness[:, :, None] * 0.3)
    rgb = rgb * (1.0 - field[:, :, None]) + joint_rgb * field[:, :, None]
    return _image("PavingSlab", size, _rgba(rgb))


def _skin_fields(size, seed):
    """The noise the skin is built from, shared by its colour and its relief
    so the vein that is darker is also the vein that is sunken."""
    rng = random.Random(seed)
    blotch = _noise(rng, size, 4, 3)      # broad: bruising and rot
    mottle = _noise(rng, size, 5, 12)     # mid: uneven patches
    vein = _noise(rng, size, 4, 24)       # ridge source for the veins
    pore = _noise(rng, size, 3, 64)       # fine grain
    return blotch, mottle, vein, pore


def _skin_marks(blotch, vein, pore):
    """Bruise, rot, veins and pores as 0..1 fields, from the shared noise."""
    bruise = numpy.clip((blotch - 0.54) * 3.2, 0.0, 1.0)
    rot = numpy.clip((0.42 - blotch) * 3.2, 0.0, 1.0)
    # Thin, and only where the skin is thin enough to show them: gated by the
    # blotch field so they lace a patch here and there rather than crazing
    # the whole body like a map of rivers, which is how they read up close.
    lines = numpy.clip(1.0 - numpy.abs(vein - 0.5) / 0.014, 0.0, 1.0)
    lines = lines * numpy.clip((blotch - 0.40) * 4.0, 0.0, 1.0)
    return bruise, rot, lines, pore


def skin(size=1024, seed=71):
    """Dead skin: pale and sickly, not green paint.

    The old tile was a dark olive with a mottle so faint and so broad that
    nothing read at any distance -- the figure was a flat green shape, and
    the per-zombie tint on top made it a muddy one. Dead skin is PALE, a
    grey-green drained of blood, and what makes it read as skin is detail at
    several scales: broad blotches of grey-purple bruising where the blood
    has pooled, patches gone dark with rot, a lace of thin dark veins, and a
    fine grain of pores over the lot. The pale base leaves the tint room to
    make one zombie greener, one greyer, one yellower.
    """
    blotch, mottle, vein, pore = _skin_fields(size, seed)

    base = numpy.array((0.46, 0.48, 0.39), dtype=numpy.float32)
    rgb = numpy.ones((size, size, 3), dtype=numpy.float32) * base[None, None, :]
    rgb *= (0.80 + mottle[:, :, None] * 0.40)

    bruise, rot, lines, pore = _skin_marks(blotch, vein, pore)
    # Bruising: grey-purple where blood has settled.
    b = bruise * 0.55
    rgb = rgb * (1.0 - b[:, :, None]) + \
        numpy.array((0.34, 0.25, 0.34), dtype=numpy.float32)[None, None, :] * b[:, :, None]
    # Rot: gone dark and greenish where the tissue has broken down.
    r = rot * 0.6
    rgb = rgb * (1.0 - r[:, :, None]) + \
        numpy.array((0.15, 0.18, 0.11), dtype=numpy.float32)[None, None, :] * r[:, :, None]
    # Veins: the contour lines of a noise field are thin, branching and
    # closed -- the shape veins have.
    rgb *= (1.0 - (lines * 0.28)[:, :, None])
    # Pores.
    rgb *= (0.90 + pore[:, :, None] * 0.20)
    return _image("ZombieSkin", size, _rgba(rgb))


def _cloth_fields(size, seed):
    """The noise and the weave the cloth is built from, shared by its colour
    and its relief so the thread that catches the light is the one drawn."""
    rng = random.Random(seed)
    grime = _noise(rng, size, 4, 3)       # broad dirt
    dust = _noise(rng, size, 3, 2)        # broad settled dust
    wear = _noise(rng, size, 5, 10)       # worn and faded patches
    blood = _noise(rng, size, 4, 5)       # a few stains
    fray = _noise(rng, size, 6, 40)       # threads and frays
    # A fine cross-hatch at eight texels, which on the figure is a thread
    # every couple of millimetres: fabric, not paint.
    yy, xx = numpy.mgrid[0:size, 0:size].astype(numpy.float32)
    period = size / 128.0
    weave = 0.5 + 0.25 * numpy.sin(xx * 2.0 * math.pi / period) + \
        0.25 * numpy.sin(yy * 2.0 * math.pi / period)
    return grime, dust, wear, blood, fray, weave


def cloth(size=1024, seed=89):
    """A suit that has been through something.

    The old tile was near-black with a two-texel weave that averaged away to
    nothing: the clothes drew as a flat dark shape. Cloth reads as cloth by
    its weave, at a scale the eye can see, and as WORN cloth by what has
    happened to it: dust, which lightens a dark fabric where it has settled;
    grime, which darkens it; patches gone thin and faded at the wear points;
    dried blood in a few dark rust-brown blots; and a scatter of pulled
    threads and frays. A base that is dark but not black leaves the folds
    something to shade.
    """
    grime, dust, wear, blood, fray, weave = _cloth_fields(size, seed)

    base = numpy.array((0.30, 0.27, 0.31), dtype=numpy.float32)
    rgb = numpy.ones((size, size, 3), dtype=numpy.float32) * base[None, None, :]
    rgb *= (0.86 + weave[:, :, None] * 0.28)
    rgb *= (0.84 + wear[:, :, None] * 0.32)

    settled = numpy.clip((dust - 0.50) * 2.0, 0.0, 1.0) * 0.35
    rgb = rgb * (1.0 - settled[:, :, None]) + \
        numpy.array((0.44, 0.41, 0.36), dtype=numpy.float32)[None, None, :] * settled[:, :, None]
    dirt = numpy.clip((0.50 - grime) * 1.8, 0.0, 1.0) * 0.45
    rgb *= (1.0 - dirt[:, :, None])
    stain = numpy.clip((blood - 0.68) * 5.0, 0.0, 1.0) * 0.7
    rgb = rgb * (1.0 - stain[:, :, None]) + \
        numpy.array((0.22, 0.06, 0.05), dtype=numpy.float32)[None, None, :] * stain[:, :, None]
    threads = numpy.clip((fray - 0.75) * 8.0, 0.0, 1.0) * 0.5
    rgb *= (1.0 - threads[:, :, None])
    return _image("ZombieCloth", size, _rgba(rgb))


def gore(size=256, seed=101):
    """What is behind the eye sockets and the mouth: dark, wet, not much of it."""
    rng = random.Random(seed)
    wet = _noise(rng, size, 4, 4)
    rgb = _tint((0.14, 0.035, 0.035), (0.045, 0.012, 0.018), wet)
    return _image("ZombieGore", size, _rgba(rgb))


def dusk_sky(width=1024, seed=131):
    """The sky the street is under, as an equirectangular image.

    v runs from straight down at the bottom of the image to straight up at the
    top, which is what the skybox shader reads it as. Below the horizon there
    is nothing to see and the colour only has to meet the fog; above it the
    blue deepens towards the zenith, the last of the sun sits in one quarter of
    the sky, and there are as many stars as a street with lamps on it lets you
    see, which is not many.
    """
    rng = random.Random(seed)
    height = width // 2
    image = numpy.zeros((height, width, 4), dtype=numpy.float32)
    image[:, :, 3] = 1.0

    horizon = numpy.array((0.075, 0.088, 0.125), dtype=numpy.float32)
    zenith = numpy.array((0.018, 0.024, 0.055), dtype=numpy.float32)
    below = numpy.array((0.030, 0.032, 0.042), dtype=numpy.float32)
    sunset = numpy.array((0.42, 0.24, 0.14), dtype=numpy.float32)

    up = numpy.linspace(-1.0, 1.0, height, dtype=numpy.float32)[:, None]
    azimuth = numpy.linspace(0.0, 2.0 * math.pi, width, endpoint=False,
                             dtype=numpy.float32)[None, :]

    # Above the horizon the blue deepens with the square root of the height, so
    # the band near the horizon is wide and the change overhead is slow.
    lift = numpy.clip(up, 0.0, 1.0) ** 0.5
    sky = horizon[None, None, :] * (1.0 - lift[:, :, None]) + zenith[None, None, :] * lift[:, :, None]
    ground = numpy.broadcast_to(below[None, None, :], (height, width, 3))
    # Over a few degrees rather than at a line: a hard edge at the horizon is
    # a black band across the picture wherever the ground does not reach.
    settle = numpy.clip((up + 0.09) / 0.18, 0.0, 1.0)
    settle = settle * settle * (3.0 - 2.0 * settle)
    sky = ground * (1.0 - settle[:, :, None]) + sky * settle[:, :, None]

    # What is left of the sun, low and in one direction.
    glow = numpy.clip(numpy.cos(azimuth - 2.6), 0.0, 1.0) ** 6
    near = numpy.clip(1.0 - numpy.abs(up) * 7.0, 0.0, 1.0)
    sky = sky + sunset[None, None, :] * (glow * near)[:, :, None] * 0.55

    for _ in range(420):
        y = int(height * 0.5 + rng.random() ** 0.6 * height * 0.5)
        if y >= height:
            continue
        x = rng.randrange(width)
        brightness = 0.25 + rng.random() ** 3 * 0.75
        sky[y, x] = numpy.clip(sky[y, x] + brightness, 0.0, 1.0)

    image[:, :, 0:3] = numpy.clip(sky, 0.0, 1.0)
    made = bpy.data.images.new("DuskSky", width=width, height=height, alpha=False)
    made.colorspace_settings.name = "sRGB"
    made.pixels.foreach_set(image.reshape(-1))
    return made


def metal(size=512, seed=97):
    rng = random.Random(seed)
    grain = _noise(rng, size, 5, 5)
    rgb = _tint((0.20, 0.21, 0.23), (0.11, 0.11, 0.13), numpy.clip(grain * 1.4 - 0.3, 0.0, 1.0))
    return _image("LampMetal", size, _rgba(rgb))


def stone(size=512, seed=113):
    """The pale limestone a sill, a band and a cornice are cut from.

    Trim is what a facade is articulated by, and it is a different stone from
    the wall it interrupts: lighter, smoother, and laid in long pieces rather
    than in courses. Reading as a different material is the whole job.
    """
    rng = random.Random(seed)
    grain = _noise(rng, size, 5, 5)
    weathering = _noise(rng, size, 3, 3)
    rgb = _tint((0.52, 0.51, 0.48), (0.34, 0.34, 0.33),
                numpy.clip(weathering * 1.3 - 0.35, 0.0, 1.0))
    rgb *= (0.88 + grain[:, :, None] * 0.24)
    return _image("TrimStone", size, _rgba(rgb))


def glass_dark(size=1024, seed=127):
    """A window with nobody home.

    Not black: a dark pane still carries the sky it faces and the room it hides,
    and a street of pure black rectangles reads as holes cut in a wall.
    """
    rng = random.Random(seed)
    sheen = _noise(rng, size, 3, 2)
    rgb = _tint((0.055, 0.062, 0.080), (0.100, 0.112, 0.140),
                numpy.clip(sheen * 1.5 - 0.4, 0.0, 1.0))
    # The pane leans back a little towards the top, so what it reflects there
    # is more sky and less street.
    ramp = numpy.linspace(1.0, 1.5, size, dtype=numpy.float32)
    rgb *= ramp[:, None, None]
    return _image("WindowGlassDark", size, _rgba(rgb))


def glass_lit(size=1024, seed=131):
    """A window with the light on, and something in the room behind it.

    A lit window that is one flat colour reads as a light box. What makes it a
    room is that it is brighter at the top, where the ceiling fitting is, and
    that something interrupts it: a curtain down one side, and the shadow of
    whatever the room has in it across the bottom.
    """
    rng = random.Random(seed)
    grain = _noise(rng, size, 4, 3)
    warm = numpy.array((1.00, 0.80, 0.48), dtype=numpy.float32)
    ramp = numpy.linspace(1.0, 0.42, size, dtype=numpy.float32)
    rgb = numpy.empty((size, size, 3), dtype=numpy.float32)
    rgb[:, :, :] = warm[None, None, :] * ramp[:, None, None]
    rgb *= (0.86 + grain[:, :, None] * 0.28)

    curtain = int(size * 0.26)
    fold = numpy.linspace(0.0, 3.0, curtain, dtype=numpy.float32)
    rgb[:, :curtain] *= (0.42 + 0.16 * numpy.abs(numpy.sin(fold)))[None, :, None]

    furniture = int(size * 0.72)
    rgb[furniture:, int(size * 0.45):int(size * 0.85)] *= 0.30
    return _image("WindowGlassLit", size, _rgba(numpy.clip(rgb, 0.0, 1.0)))


def _normal_from_height(height, strength=1.0, name="Normal"):
    """A normal map from a height field, by the slope at each texel.

    What a diffuse texture cannot do is catch the light differently on the two
    sides of a brick, and that is most of what makes a wall read as brick
    rather than as a photograph of one. The height is the same field the colour
    was shaded from, so the mortar that is darker is also the mortar that is
    lower, and a lamp above the street lights the top of every course and
    leaves the underside of it dark.

    Slopes are taken with a wrap, because these tile: a seam in a normal map is
    a line of wrong lighting straight down a wall.
    """
    dx = (numpy.roll(height, -1, axis=1) - numpy.roll(height, 1, axis=1)) * strength
    dy = (numpy.roll(height, -1, axis=0) - numpy.roll(height, 1, axis=0)) * strength
    normal = numpy.empty(height.shape + (3,), dtype=numpy.float32)
    normal[:, :, 0] = -dx
    normal[:, :, 1] = -dy
    normal[:, :, 2] = 1.0
    length = numpy.sqrt((normal * normal).sum(axis=2))[:, :, None]
    normal /= numpy.maximum(length, 1e-6)
    return _image(name, height.shape[0], _rgba(normal * 0.5 + 0.5))


def brick_normal(size=1024, seed=11):
    """The courses standing proud and the mortar sunk between them."""
    rng = random.Random(seed)
    rows, columns = 16, 6
    row_height = size / rows
    column_width = size / columns
    mortar = max(1.0, size / 128.0)
    height = numpy.zeros((size, size), dtype=numpy.float32)
    for y in range(size):
        row = int(y / row_height)
        in_row = y - row * row_height
        offset = 0.5 * column_width if row % 2 else 0.0
        for x in range(size):
            u = (x + offset) % size
            in_column = u % column_width
            joint = (in_row < mortar or in_column < mortar or
                     in_row > row_height - mortar or in_column > column_width - mortar)
            height[y, x] = 0.0 if joint else 1.0
    height += _noise(rng, size, 5, 6) * 0.22
    return _normal_from_height(height, 2.6, "BrickWallNormal")


def concrete_normal(size=1024, seed=23):
    rng = random.Random(seed)
    height = _noise(rng, size, 5, 6) * 0.7 + _noise(rng, size, 4, 3) * 0.3
    # The panel joints are recesses and the tie holes deeper pits, so a lamp
    # raking across the wall catches every panel edge -- which is most of what
    # makes cast concrete read as cast rather than as flat grey.
    seam, tie = _concrete_panels(size)
    height = height - 0.9 * seam - 1.0 * tie
    return _normal_from_height(height, 2.4, "ConcreteWallNormal")


def tarmac_normal(size=1024, seed=37):
    rng = random.Random(seed)
    height = _noise(rng, size, 6, 10) * 0.8 + _noise(rng, size, 3, 5) * 0.2
    return _normal_from_height(height, 1.9, "RoadTarmacNormal")


def paving_normal(size=1024, seed=53):
    """Slabs proud of the grooves between them."""
    slabs = 2
    pitch = size / slabs
    groove = max(1.0, size / 96.0)
    rng = random.Random(seed)
    height = numpy.ones((size, size), dtype=numpy.float32)
    for y in range(size):
        in_row = y % pitch
        for x in range(size):
            in_column = x % pitch
            if in_row < groove or in_column < groove:
                height[y, x] = 0.0
    height += _noise(rng, size, 5, 4) * 0.25
    return _normal_from_height(height, 2.2, "PathPavingNormal")


def stone_normal(size=512, seed=113):
    rng = random.Random(seed)
    height = _noise(rng, size, 5, 5) * 0.6 + _noise(rng, size, 3, 3) * 0.4
    return _normal_from_height(height, 1.1, "TrimStoneNormal")


def skin_normal(size=1024, seed=71):
    """The skin's relief, from the same fields as its colour: the bruises
    swell a little, the rot sinks, the veins are grooves, and the pores pit
    the surface -- so a lamp rakes the same marks the colour shows."""
    blotch, mottle, vein, pore = _skin_fields(size, seed)
    bruise, rot, lines, pore = _skin_marks(blotch, vein, pore)
    height = mottle * 0.30 + bruise * 0.20 - rot * 0.25 - lines * 0.25 - pore * 0.12
    return _normal_from_height(height, 2.4, "ZombieSkinNormal")


def cloth_normal(size=1024, seed=89):
    """The cloth's relief, from the same fields as its colour: the weave
    stands proud thread by thread at the scale the colour draws it, the worn
    patches lie flatter, and the frays are pits; broad creases over the lot.
    The old map wove at a period twenty times the colour's -- relief and
    colour disagreed about where the threads were."""
    grime, dust, wear, blood, fray, weave = _cloth_fields(size, seed)
    threads = numpy.clip((fray - 0.75) * 8.0, 0.0, 1.0)
    height = weave * 0.35 + wear * 0.18 + grime * 0.30 - threads * 0.40
    return _normal_from_height(height, 1.8, "ZombieClothNormal")


def _planks(size, rng, boards, gap):
    """Which texels are the gap between boards, and which board each is on.

    Boards run along y, so a crate or a bench is built with its length along
    the grain, which is the way timber is used.
    """
    pitch = size / boards
    field = numpy.zeros((size, size), dtype=numpy.float32)
    board = numpy.zeros((size, size), dtype=numpy.float32)
    tones = [rng.uniform(-0.09, 0.09) for _ in range(boards)]
    for x in range(size):
        which = int(x / pitch)
        in_board = x - which * pitch
        field[:, x] = 1.0 if (in_board < gap or in_board > pitch - gap) else 0.0
        board[:, x] = tones[which]
    return field, board


def wood(size=512, seed=139):
    """Rough sawn boards, four across a tile of sixty centimetres, so a board
    is 150mm and the grain runs the length of whatever is built from it."""
    rng = random.Random(seed)
    field, board = _planks(size, rng, 4, max(1.0, size / 160.0))
    grain = _noise(rng, size, 6, 3)
    streak = _noise(rng, size, 2, 24)
    rgb = _tint((0.44, 0.31, 0.19), (0.19, 0.13, 0.08), field)
    rgb *= (0.84 + board[:, :, None] + (grain[:, :, None] - 0.5) * 0.30
            + (streak[:, :, None] - 0.5) * 0.14)
    return _image("PropWood", size, _rgba(rgb))


def wood_normal(size=512, seed=139):
    rng = random.Random(seed)
    field, _board = _planks(size, rng, 4, max(1.0, size / 160.0))
    height = (1.0 - field) + _noise(rng, size, 6, 3) * 0.18
    return _normal_from_height(height, 2.2, "PropWoodNormal")


def painted_metal(size=512, seed=149):
    """A wheelie bin or a bollard: paint over pressed steel, worn through at
    the edges and dented where it has been knocked about."""
    rng = random.Random(seed)
    dents = _noise(rng, size, 3, 4)
    wear = _noise(rng, size, 5, 7)
    rgb = _tint((0.16, 0.22, 0.18), (0.30, 0.28, 0.24),
                numpy.clip(wear * 1.6 - 0.75, 0.0, 1.0))
    rgb *= (0.86 + (dents[:, :, None] - 0.5) * 0.26)
    return _image("PropPaint", size, _rgba(rgb))


def painted_metal_normal(size=512, seed=149):
    rng = random.Random(seed)
    height = _noise(rng, size, 3, 4) * 0.8 + _noise(rng, size, 5, 7) * 0.2
    return _normal_from_height(height, 1.6, "PropPaintNormal")


def sacking(size=512, seed=151):
    """A refuse sack: black polythene, creased, with a sheen where it is
    stretched. The creases are the normal map's; the colour is nearly one."""
    rng = random.Random(seed)
    crease = _noise(rng, size, 4, 6)
    rgb = _tint((0.045, 0.045, 0.05), (0.10, 0.10, 0.11),
                numpy.clip(crease * 1.5 - 0.5, 0.0, 1.0))
    return _image("PropSack", size, _rgba(rgb))


def sacking_normal(size=512, seed=151):
    rng = random.Random(seed)
    height = _noise(rng, size, 4, 6) * 0.7 + _noise(rng, size, 6, 14) * 0.3
    return _normal_from_height(height, 2.8, "PropSackNormal")


def wet_tarmac(size=1024, seed=37):
    """The road after rain. The same grit and the same patches as the dry
    surface -- it is the same road -- but darker, because a film of water lets
    less light back out, and cooler, because what it does let out has been
    through the sky. What makes it read as wet is not here: it is the
    roughness, which is the material's, and the lamps it then reflects."""
    rng = random.Random(seed)
    grit = _noise(rng, size, 6, 10)
    patch = _noise(rng, size, 3, 5)
    rgb = _tint((0.040, 0.042, 0.050), (0.064, 0.066, 0.076),
                numpy.clip(patch * 1.4 - 0.35, 0.0, 1.0))
    rgb *= (0.80 + grit[:, :, None] * 0.40)
    return _image("RoadTarmac", size, _rgba(rgb))


def car_paint(size=512, seed=163):
    """A car's paint: a deep red under a clear coat, the metallic flake in
    it a fine grain that catches the light, and the odd scuff where the
    street has had it."""
    rng = random.Random(seed)
    flake = _noise(rng, size, 6, 24)
    scuff = _noise(rng, size, 3, 3)
    rgb = _tint((0.52, 0.045, 0.05), (0.62, 0.08, 0.07),
                numpy.clip(flake * 1.8 - 0.6, 0.0, 1.0))
    rgb *= (0.94 + (scuff[:, :, None] - 0.5) * 0.10)
    return _image("CarPaint", size, _rgba(rgb))


def car_paint_normal(size=512, seed=163):
    rng = random.Random(seed)
    height = _noise(rng, size, 2, 3)
    return _normal_from_height(height, 0.35, "CarPaintNormal")


def rubber(size=512, seed=167):
    """A tyre: black rubber, its tread in the normal map, dusted grey at
    the shoulders where the road has worn it."""
    rng = random.Random(seed)
    wear = _noise(rng, size, 4, 6)
    rgb = _tint((0.035, 0.035, 0.037), (0.09, 0.09, 0.09),
                numpy.clip(wear * 1.5 - 0.6, 0.0, 1.0))
    return _image("CarTyre", size, _rgba(rgb))


def rubber_normal(size=512, seed=167):
    rng = random.Random(seed)
    # Tread: ridges across the image, the way a tyre's run around it.
    rows = numpy.linspace(0.0, 1.0, size, endpoint=False, dtype=numpy.float32)
    ridges = 0.5 + 0.5 * numpy.sin(rows * math.pi * 2.0 * 36.0)
    height = ridges[:, None] * numpy.ones((1, size), dtype=numpy.float32) * 0.7 + _noise(rng, size, 5, 9) * 0.3
    return _normal_from_height(height, 2.2, "CarTyreNormal")

