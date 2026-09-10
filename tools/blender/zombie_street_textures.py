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
    """Two courses of two windows, so a wall repeats every four of them.

    Windows are what makes a wall read as a building rather than a slab, and at
    a third of a repeat per metre one texture tile is about three metres, which
    is one storey. A few are lit: a street at dusk is not a street where
    everybody is out.
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


def brick(size=256, seed=11):
    """Courses of brick, offset every other row, with mortar between."""
    rng = random.Random(seed)
    rows, columns = 16, 8
    grain = _noise(rng, size, 4, 4)
    field = numpy.zeros((size, size), dtype=numpy.float32)
    shade = numpy.zeros((size, size), dtype=numpy.float32)

    row_height = size / rows
    column_width = size / columns
    mortar = max(1.0, size / 128.0)

    for y in range(size):
        row = int(y / row_height)
        in_row = y - row * row_height
        offset = 0.5 * column_width if row % 2 else 0.0
        row_shade = rng.uniform(-0.06, 0.06)
        for x in range(size):
            u = (x + offset) % size
            in_column = u % column_width
            joint = (in_row < mortar or in_column < mortar or
                     in_row > row_height - mortar or in_column > column_width - mortar)
            field[y, x] = 1.0 if joint else 0.0
            shade[y, x] = row_shade

    face = numpy.array((0.42, 0.20, 0.15), dtype=numpy.float32)
    joint_colour = numpy.array((0.52, 0.50, 0.47), dtype=numpy.float32)
    rgb = face[None, None, :] * (1.0 + shade[:, :, None] + (grain[:, :, None] - 0.5) * 0.35)
    rgb = rgb * (1.0 - field[:, :, None]) + joint_colour[None, None, :] * field[:, :, None]
    rgb = _windows(rgb, size, rng, (0.30, 0.29, 0.27))
    return _image("BrickWall", size, _rgba(rgb))


def concrete(size=256, seed=23):
    rng = random.Random(seed)
    grain = _noise(rng, size, 5, 6)
    streak = _noise(rng, size, 4, 3)
    stain = numpy.clip((streak - 0.5) * 2.0, 0.0, 1.0)
    rgb = _tint((0.40, 0.40, 0.39), (0.21, 0.21, 0.22), stain * 0.8)
    rgb *= (0.84 + grain[:, :, None] * 0.32)
    rgb = _windows(rgb, size, rng, (0.26, 0.26, 0.25))
    return _image("ConcreteWall", size, _rgba(rgb))


def tarmac(size=256, seed=37):
    rng = random.Random(seed)
    grit = _noise(rng, size, 6, 10)
    patch = _noise(rng, size, 3, 5)
    rgb = _tint((0.070, 0.070, 0.076), (0.105, 0.104, 0.110),
                numpy.clip(patch * 1.4 - 0.35, 0.0, 1.0))
    rgb *= (0.76 + grit[:, :, None] * 0.48)
    return _image("RoadTarmac", size, _rgba(rgb))


def paving(size=256, seed=53):
    """Slabs, four across, with a groove between them."""
    rng = random.Random(seed)
    grain = _noise(rng, size, 5, 4)
    slabs = 4
    pitch = size / slabs
    groove = max(1.0, size / 96.0)
    field = numpy.zeros((size, size), dtype=numpy.float32)
    shade = numpy.zeros((size, size), dtype=numpy.float32)
    for y in range(size):
        row = int(y / pitch)
        in_row = y % pitch
        for x in range(size):
            column = int(x / pitch)
            in_column = x % pitch
            edge = (in_row < groove or in_column < groove)
            field[y, x] = 1.0 if edge else 0.0
            shade[y, x] = ((row * 7 + column * 13) % 5) * 0.012
    rgb = _tint((0.46, 0.45, 0.43), (0.22, 0.22, 0.22), field)
    rgb *= (0.86 + grain[:, :, None] * 0.28 + shade[:, :, None])
    return _image("PavingSlab", size, _rgba(rgb))


def skin(size=128, seed=71):
    """Mottled, bruised, and not well."""
    rng = random.Random(seed)
    blotch = _noise(rng, size, 4, 3)
    veins = _noise(rng, size, 5, 6)
    rot = numpy.clip((blotch - 0.5) * 2.0, 0.0, 1.0)
    rgb = _tint((0.21, 0.25, 0.17), (0.12, 0.15, 0.10), rot)
    bruise = numpy.clip((veins - 0.62) * 3.0, 0.0, 1.0)
    rgb = rgb * (1.0 - bruise[:, :, None] * 0.6) + \
        numpy.array((0.30, 0.16, 0.20), dtype=numpy.float32)[None, None, :] * \
        bruise[:, :, None] * 0.6
    return _image("ZombieSkin", size, _rgba(rgb))


def cloth(size=128, seed=89):
    """Torn, filthy, and woven closely enough to read as fabric."""
    rng = random.Random(seed)
    grime = _noise(rng, size, 4, 3)
    weave = numpy.zeros((size, size), dtype=numpy.float32)
    for y in range(size):
        for x in range(size):
            weave[y, x] = 0.5 + 0.5 * math.sin(x * math.pi / 2.0) * math.sin(y * math.pi / 2.0)
    rgb = _tint((0.20, 0.19, 0.22), (0.09, 0.09, 0.11), numpy.clip(grime * 1.3 - 0.25, 0.0, 1.0))
    rgb *= (0.88 + weave[:, :, None] * 0.14)
    return _image("ZombieCloth", size, _rgba(rgb))


def gore(size=64, seed=101):
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


def metal(size=128, seed=97):
    rng = random.Random(seed)
    grain = _noise(rng, size, 5, 5)
    rgb = _tint((0.20, 0.21, 0.23), (0.11, 0.11, 0.13), numpy.clip(grain * 1.4 - 0.3, 0.0, 1.0))
    return _image("LampMetal", size, _rgba(rgb))
