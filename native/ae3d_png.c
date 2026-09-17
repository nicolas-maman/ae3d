// Writing an RGBA frame out as a PNG.
//
// A program that renders can already be checked for not crashing and for the
// number of draws it issued. Neither says what it drew, and the difference
// matters: an example that renders a flat wash of one colour exits zero and
// counts its draws exactly like one that renders the scene it is named after.
//
// zlib does the compression, which the build already links for the toolchain's
// own use, so what is here is the chunk framing PNG puts around it.

#include "ae3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put_be32(unsigned char *out, unsigned value) {
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
}

static int write_chunk(FILE *f, const char *type, const unsigned char *data,
                       unsigned length) {
    unsigned char header[4];
    unsigned char trailer[4];
    uLong crc;

    put_be32(header, length);
    if (fwrite(header, 1, 4, f) != 4) return 0;
    if (fwrite(type, 1, 4, f) != 4) return 0;
    if (length > 0 && fwrite(data, 1, length, f) != length) return 0;

    crc = crc32(0, (const Bytef *)type, 4);
    if (length > 0) crc = crc32(crc, (const Bytef *)data, length);
    put_be32(trailer, (unsigned)crc);
    return fwrite(trailer, 1, 4, f) == 4;
}

// `rgba` is width*height*4 bytes, top row first. Returns 1 on success.
int ae3d_png_write(const char *path, const void *rgba, int width, int height) {
    static const unsigned char SIGNATURE[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    const unsigned char *pixels = (const unsigned char *)rgba;
    unsigned char ihdr[13];
    unsigned char *raw = NULL;
    unsigned char *packed = NULL;
    uLongf packed_size;
    size_t row_bytes, raw_size;
    FILE *f = NULL;
    int y, ok = 0;

    if (!path || !pixels || width <= 0 || height <= 0) return 0;

    row_bytes = (size_t)width * 4;
    // One filter byte per row, and filter 0 throughout: the rows come off a
    // framebuffer rather than out of a paint program, so choosing a filter per
    // row would cost a pass over the image to save space nothing here needs.
    raw_size = (row_bytes + 1) * (size_t)height;
    raw = (unsigned char *)malloc(raw_size);
    if (!raw) return 0;
    for (y = 0; y < height; y++) {
        raw[(row_bytes + 1) * (size_t)y] = 0;
        memcpy(raw + (row_bytes + 1) * (size_t)y + 1,
               pixels + row_bytes * (size_t)y, row_bytes);
    }

    packed_size = compressBound((uLong)raw_size);
    packed = (unsigned char *)malloc(packed_size);
    if (!packed) { free(raw); return 0; }
    if (compress2(packed, &packed_size, raw, (uLong)raw_size, 6) != Z_OK) {
        free(raw);
        free(packed);
        return 0;
    }
    free(raw);

    f = fopen(path, "wb");
    if (!f) { free(packed); return 0; }

    put_be32(ihdr, (unsigned)width);
    put_be32(ihdr + 4, (unsigned)height);
    ihdr[8] = 8;    // bits per channel
    ihdr[9] = 6;    // colour type: RGBA
    ihdr[10] = 0;   // deflate
    ihdr[11] = 0;   // adaptive filtering
    ihdr[12] = 0;   // no interlace

    ok = fwrite(SIGNATURE, 1, 8, f) == 8 &&
         write_chunk(f, "IHDR", ihdr, 13) &&
         write_chunk(f, "IDAT", packed, (unsigned)packed_size) &&
         write_chunk(f, "IEND", NULL, 0);

    if (fclose(f) != 0) ok = 0;
    free(packed);
    return ok;
}

/* One pixel of an RGBA buffer, from the language that cannot store a byte
 * itself: Aether's arrays are of its own numeric types, so a tool that
 * builds an image writes each pixel through here. Channels are clamped. */
static unsigned char channel_byte(double v) {
    if (v <= 0.0) return 0;
    if (v >= 1.0) return 255;
    return (unsigned char)(v * 255.0 + 0.5);
}

void ae3d_rgba_set(void *rgba, int index, double r, double g, double b, double a) {
    unsigned char *p = (unsigned char *)rgba + (size_t)index * 4;
    p[0] = channel_byte(r);
    p[1] = channel_byte(g);
    p[2] = channel_byte(b);
    p[3] = channel_byte(a);
}
