# The game UI layer

A game needs a HUD and menus: health and ammo, a crosshair, a prompt, a
pause menu, a lobby. The engine drew no text and nothing in screen space,
and the editor's panels are aether-ui, which does not belong in a game's
frame. #449 builds the layer in four slices; this page describes what is
built.

| Slice | What | State |
|---|---|---|
| 1. Glyphs | A TrueType reader and a signed-distance-field atlas baked from it | `ae3d.glyphs`, this page |
| 2. The overlay pass | Screen-space quads batched into one draw after post, on both renderers | not built |
| 3. Layout | Anchors, DPI-scaled offsets, alignment, wrapping, clipping panels | not built |
| 4. Input | Focus and click through `ae3d.input`, a menu stack | not built |

## Glyphs

```aether
import ae3d.glyphs

face = glyphs.load("resources/fonts/PT_Sans-Web-Regular.ttf")
atlas = glyphs.bake(face, glyphs.DEFAULT_SIZE, glyphs.DEFAULT_SPREAD)
width, height = glyphs.measure(atlas, "E to get in", 28.0)
quads = glyphs.layout(atlas, "E to get in", 28.0)
// quads.count quads of 8 floats in quads.data: x0 y0 x1 y1 in pixels,
// y down from the text's top-left, then u0 v0 u1 v1 in the atlas
glyphs.quads_free(quads)
glyphs.atlas_free(atlas)
glyphs.typeface_free(face)
```

Text is drawn from a texture, and a texture of coverage -- the glyph as a
rasteriser fills it -- is sharp at the one size it was made at and
blurred or jagged at every other. A texture of distance is not: each texel
holds how far its centre is from the outline, signed, and the outline is
wherever the filtered value crosses the middle, at any scale (Green,
SIGGRAPH 2007). The same field gives an outline or a glow for a threshold
in the shader.

### The font

`resources/fonts/PT_Sans-Web-Regular.ttf` is PT Sans Regular (ParaType,
SIL Open Font License 1.1, the licence beside it in
`resources/fonts/PT_Sans-OFL.txt`): 443 KB, 720 glyphs, Latin and
Cyrillic, 1000 units to the em. It is a static TrueType font with its
kerning in a `kern` table, 24,030 pairs, which is what this reader reads.

### What is read

The table directory (a `.ttc` collection's first font), `head`, `maxp`,
`hhea`, `hmtx`, `cmap` formats 4 and 12, `loca`, `glyf` and `kern` format
0. A simple glyph is its quadratic contours, on-curve and control points,
with the on-curve points implied between two controls; a compound glyph is
its components, each through its 2x2 transform and placed by an offset or
by matching a point of its own to one already placed, nested up to eight
deep. GPOS is not read (an OpenType layout engine is not what a HUD needs),
nor CFF outlines (`.otf`).

Every read is bounds-checked against the file and its table. A font that
fails loads as null, and `glyphs.last_error()` says which table: "head
table missing or short", "table directory runs past the end of the file",
"not a TrueType font (unknown sfnt version)". A glyph that fails is left
blank and counted in the atlas's `broken`. A format-0 `kern` subtable
with more than 10,921 pairs overflows its u16 length (PT Sans's does), so
its size is taken from its pair count.

### The field

Each glyph's contours are flattened to segments: a quadratic is cut into
as many equal-parameter chords as its bend needs to stay within 0.02 atlas
pixels of the curve (a chord over a step h strays at most
|P0 - 2 P1 + P2| h² / 4). Each texel takes the distance to the nearest
segment, and its sign from the winding of the row's crossings left of it,
by the non-zero rule: that is the rule TrueType fills by, and a compound
glyph whose components overlap is solid where they overlap under it and
a hole under even-odd. Values are 8 bits: 0.5 is the outline, 0 and 1 are
the spread outside and in.

The default bake is 64 pixels to the em, the field 2.5 pixels either side
of the outline. Drawn at 12 px that is still half a screen pixel either
side, what antialiasing an edge needs. A regular weight's stem is about 80
units of 1000, 5.1 pixels at 64, and the texel nearest its middle is at
least 2 pixels in: it reads 0.9 or more. A game that wants thick outlines
or glows bakes with a wider spread, and the middle of a stroke then reads
less.

### The atlas

Printable ASCII, Latin-1 (U+00A0 to U+00FF: é ñ ü ¿ ¡ ° and the rest) and
the font's missing-glyph box, 192 glyphs, each in a rectangle of its own:
the outline's box in whole pixels, grown by the spread and a texel more so
the border reads 0 and a filtered sample off the edge blends with nothing.
Rectangles are packed in shelves, tallest first, into a power-of-two
texture: 1024 x 512 for PT Sans at the default bake, one byte a texel.

A glyph's metrics (`AtlasGlyph`) are in em units as the font gives them
(advance, left side bearing, box) and in atlas pixels as the quads use
them (the rectangle, its left edge right of the pen and its top above the
baseline, the advance). The font's kerning between the atlas's glyphs
comes with it, 2,645 pairs, so an atlas lays text out without the font.
A codepoint the atlas lacks draws as the missing glyph.

### Layout

`measure` and `layout` take UTF-8 (a malformed sequence is U+FFFD, one
byte) at a pixel size: each glyph's advance, the kerning between it and
the one before, and a newline returns to the left one line lower, a line
being the font's ascender to descender plus its line gap. The first
baseline is the ascender below the top. `measure` gives the widest line
and the lines' height; `layout` a quad for each glyph that has something
to draw.

### The cache

Baking costs milliseconds a program would pay every start, so `bake`
keeps the atlas like the OBJ parse cache keeps a parse: a file under
`build/cache/fonts/` (or `AE3D_FONT_CACHE`; `off` turns it off), headed by
the font's absolute path, size and modification time and the bake's size
and spread. A bake whose font and parameters match reads it. The time is to
the second, so a font rewritten at the same size within the second of a
bake reads the older atlas. `bake_uncached` always bakes; `load_bytes`
takes a font already in memory, which is baked afresh.

### Measured

`tests/test_font.ae`, no window or GPU. The font's numbers are checked
against a separate reader of the same tables (a Python script over the
file's bytes). On the Windows machine of [performance.md](performance.md):

| What | Number |
|---|---|
| Font | 720 glyphs, 1000 units per em, ascender 1018, descender -276, 24,030 kern pairs |
| 'A' 'g' 'é' '0' | glyphs 36, 74, 171, 19; advances 585, 537, 508, 545 |
| 'A' / 'é' | 14 points in 2 contours / 40 in 3 ('e''s 35 and the accent's 5) |
| Bake, 64 px, spread 2.5 | 1024 x 512, 192 glyphs, 6.2 ms; read back from the cache 0.4 ms (4 ms the first time after it is written) |
| Stem of 'l', middle / 3 px outside it / counter of 'o' | 0.93 / 0 / 0 |
| The 0.5 contour against the outline, 41,658 crossings in every glyph | 0.40 px at most, 0.011 px on average |
| A texel's distance against the outline's, within the spread | 0.028 px at most |
| "Hello, world" at 32 px | 164.224 px wide: 5233 units of advance, 101 of kerning, exactly |
| "AV" at 100 px | 110.8 px, kerned 4.5 px closer |
| 200 copies of the font with 1-16 bytes overwritten | 139 loaded, 61 refused, 641 glyphs refused, no crash |

The worst of the 0.5 contour is at corners and joins (where the bar of
'H' meets its stems, the join of '4'): the field bends between two texels
there, and a line between their values cuts across the bend. Of the
23,662 horizontal crossings, 287 (1.2%) are more than 0.1 px off.
