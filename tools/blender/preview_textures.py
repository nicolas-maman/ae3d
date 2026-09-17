"""Preview a generated street texture without opening Blender.

    py tools/blender/preview_textures.py brick out/brick.png
    py tools/blender/preview_textures.py brick brick_normal concrete --dir out

zombie_street_textures builds every image from numpy and only touches bpy to
wrap the pixels, so a stand-in bpy that keeps the pixel array is enough to run
any generator here and write what it made to a PNG. That is the difference
between tuning a texture by reading numbers and tuning it by looking at it --
and between a change an agent can verify and one it has to trust.

Only the standard library and numpy: the PNG is written by hand.

The preview is what the generator computed, not what Blender writes: the
exported PNGs pass through Blender's colour management and encoder and come
out within about 2% of these (at most 14/255 on a channel). Tune here; the
export is authoritative.
"""

import struct
import sys
import types
import zlib
from pathlib import Path

import numpy


class _Pixels:
    def __init__(self):
        self.data = None

    def foreach_set(self, flat):
        self.data = numpy.asarray(flat, dtype=numpy.float32)


class _Image:
    def __init__(self, name, width, height):
        self.name = name
        self.width = width
        self.height = height
        self.pixels = _Pixels()
        self.colorspace_settings = types.SimpleNamespace(name="sRGB")

    def pack(self):
        pass


class _Images:
    def new(self, name, width, height, alpha=False):
        return _Image(name, width, height)


def _install_stub():
    bpy = types.ModuleType("bpy")
    bpy.data = types.SimpleNamespace(images=_Images())
    sys.modules["bpy"] = bpy


def write_png(path, rgb):
    """rgb is (height, width, 3) uint8, top row first."""
    height, width, _ = rgb.shape
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(height))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    Path(path).write_bytes(png)


def render(name, path):
    import zombie_street_textures as textures
    generator = getattr(textures, name)
    image = generator()
    pixels = image.pixels.data.reshape(image.height, image.width, 4)
    # Blender images are stored bottom row first; a PNG is top row first.
    rgb = (numpy.clip(pixels[:, :, :3], 0.0, 1.0) * 255.0 + 0.5).astype(numpy.uint8)[::-1]
    write_png(path, rgb)
    print(f"{name}: {image.width}x{image.height} -> {path}")


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 2
    _install_stub()
    sys.path.insert(0, str(Path(__file__).parent))
    out_dir = None
    if "--dir" in argv:
        i = argv.index("--dir")
        out_dir = Path(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
        out_dir.mkdir(parents=True, exist_ok=True)
    if out_dir is None and len(argv) == 2 and argv[1].lower().endswith(".png"):
        render(argv[0], argv[1])
        return 0
    for name in argv:
        target = (out_dir or Path(".")) / f"{name}.png"
        render(name, target)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
