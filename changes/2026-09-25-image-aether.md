### Image files decoded in Aether

- Reading an image file leaves C (#398): PNG, JPEG, TGA and BMP decode in
  `ae3d.picture`, with the JPEG decoder in `ae3d.jpeg` and the PNG's
  deflate in `ae3d.inflate` (stored, fixed and dynamic blocks, in Aether
  rather than through `std.zlib`, which exists only where the toolchain
  was built with zlib). `native/image/` -- `image.c` and the vendored
  7,988-line `stb_image.h` -- is gone from the engine's native library.
  Every caller (both renderers, the scene's parallel preload, glTF's
  embedded images, the voxel palette, the weather's particle,
  `examples/smooth_terrain`, `tools/crop`, `tools/montage`,
  `tools/probe_image`) calls `picture.load`, `from_memory`, `solid`,
  `register_rgba` / `unregister`, `fit` and `dispose` where it called the
  C, and gets what it got: RGBA, bottom row first.
- Each decoder is stb_image 2.30's followed step for step, down to its
  integer IDCT, its upsampling and its reduced-precision YCbCr (whose SSE2
  paths stb wrote to agree with the scalar ones), so a texture decodes to
  the bytes it did. `tests/test_image_decode` holds that against the
  width, height and RGBA hash stb gave, taken before it was removed, for
  every PNG and JPEG in the repository, Fox.glb's embedded PNG and 87 new
  fixtures in `tests/fixtures/images` (PNG in every colour type and bit
  depth, interlaced or not, with tRNS, all five filters, stored, fixed
  and dynamic deflate; JPEG baseline and progressive at 4:4:4, 4:2:2,
  4:2:0, 4:4:0, 4:1:1 and mixed factors, greyscale, CMYK, named RGB, with
  restart markers; TGA plain and RLE at 8/15/16/24/32 bits, colour-mapped,
  either origin; BMP at 1/4/8/16/24/32 bits, bitfields, V4/V5 and OS/2
  headers, either direction): 101 PNG, 19 JPEG, 12 TGA, 14 BMP and the
  embedded one, all identical. Then every fixture broken 200 ways (cut
  short, bytes overwritten): 17,400 decodes, each refused with a reason or
  decoded, none crashing.
- A broken file behaves differently only where stb read memory it had not
  written: a TGA or BMP too short for the pixels its header claims is
  refused instead of filled from the heap, a PNG's stream is inflated as
  far as its rows go and no further, and a JPEG that ends before its scans
  comes back zeroed rather than uninitialized.
- Measured on one machine in one run, best of seven, against stb as the
  engine built it: a 2048 x 2048 PNG 62 ms (stb 63), `Clouds.png`
  (4096 x 2048) 83 ms (78); a 2048 x 2048 4:2:0 JPEG 34 ms (stb's SSE2
  path 20, its scalar path 37), progressive 69 (52), 4:4:4 55 (36),
  `Albedo.jpg` (8192 x 4096) 192 (108), `Bump.jpg` (21,600 x 10,800) 583
  (454). The JPEG gap is stb's SIMD IDCT and colour conversion, which
  `std.lanes` has no integer loads to write yet.
