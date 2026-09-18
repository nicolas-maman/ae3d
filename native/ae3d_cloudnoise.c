/* The noise the clouds are made of, baked once into textures.
 *
 * A cloud marched in the sky shader used to evaluate its noise in the shader,
 * a fractal of value noise at every step and again at every step of the
 * march toward the sun: forty hashes a sample, thousands a pixel, six
 * milliseconds of a seven-millisecond frame. The same noise as textures is
 * one fetch a sample, which is how every production sky does it (Schneider,
 * "The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn").
 *
 * Two textures. The SHAPE is a tileable 3D texture of four channels: R is
 * Perlin-Worley -- Perlin noise remapped by Worley cells, which is what gives
 * a cloud both a billowing body and cellular edges -- and G, B, A are Worley
 * noise at three rising frequencies, read as a fractal that erodes the body
 * into detail at its edges. The WEATHER map is a 2D texture over the world:
 * R is where cloud is, the same fractal the ground's cloud shadow computes in
 * its shader, so the shadow under a cloud is the cloud; G is the cloud's
 * kind, from a low flat stratus to a tall cumulus. Every function here is
 * periodic in its texture, so a texture repeats across the sky without a
 * seam. */
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* A hash on an integer lattice point, the same bit mixing the shaders use
   on theirs, so a value here is exact and not a driver's sine. */
static unsigned ae3d_cn_mix(unsigned n) {
    n ^= n >> 16u; n *= 0x7feb352du; n ^= n >> 15u; n *= 0x846ca68bu; n ^= n >> 16u;
    return n;
}

/* Exactly the shaders' cloudHash(vec3(x, y, z)): the seed of a field rides
   in z, which is where the shaders put it too. */
static float ae3d_cn_hash3(int x, int y, int z, unsigned seed) {
    unsigned n = ((unsigned)x * 1597334677u ^ (unsigned)y * 3812015801u ^ (unsigned)(z + (int)seed) * 2798796415u);
    return (float)ae3d_cn_mix(n * 1597334677u) * (1.0f / 4294967296.0f);
}

static int ae3d_cn_wrap(int v, int period) {
    v %= period;
    return v < 0 ? v + period : v;
}

/* Perlin noise on a lattice of `period` cells, tileable, in -1..1. */
static float ae3d_cn_perlin(float x, float y, float z, int period, unsigned seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float fx = x - (float)ix, fy = y - (float)iy, fz = z - (float)iz;
    float ux = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    float uy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
    float uz = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);
    float corner[8];
    int c;
    for (c = 0; c < 8; c++) {
        int cx = c & 1, cy = (c >> 1) & 1, cz = (c >> 2) & 1;
        int px = ae3d_cn_wrap(ix + cx, period), py = ae3d_cn_wrap(iy + cy, period), pz = ae3d_cn_wrap(iz + cz, period);
        /* A gradient from the corner's hash, one of twelve directions. */
        unsigned h = ae3d_cn_mix((unsigned)px * 1597334677u ^ (unsigned)py * 3812015801u ^ (unsigned)pz * 2798796415u ^ seed) % 12u;
        float gx = (h < 8u) ? ((h & 1u) ? -1.0f : 1.0f) : 0.0f;
        float gy = (h < 4u) ? ((h & 2u) ? -1.0f : 1.0f) : ((h >= 8u) ? ((h & 1u) ? -1.0f : 1.0f) : 0.0f);
        float gz = (h >= 4u && h < 8u) ? ((h & 2u) ? -1.0f : 1.0f) : ((h >= 8u) ? ((h & 2u) ? -1.0f : 1.0f) : 0.0f);
        float dx = fx - (float)cx, dy = fy - (float)cy, dz = fz - (float)cz;
        corner[c] = gx * dx + gy * dy + gz * dz;
    }
    {
        float x00 = corner[0] + (corner[1] - corner[0]) * ux;
        float x10 = corner[2] + (corner[3] - corner[2]) * ux;
        float x01 = corner[4] + (corner[5] - corner[4]) * ux;
        float x11 = corner[6] + (corner[7] - corner[6]) * ux;
        float y0 = x00 + (x10 - x00) * uy;
        float y1 = x01 + (x11 - x01) * uy;
        return y0 + (y1 - y0) * uz;
    }
}

/* Worley (cellular) noise, tileable: the distance to the nearest of one
   feature point a cell, inverted so a cell's centre is 1 and its edge 0,
   which is the shape a cloud puff has. */
static float ae3d_cn_worley(float x, float y, float z, int period, unsigned seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float best = 1e9f;
    int dx, dy, dz;
    for (dz = -1; dz <= 1; dz++) for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
        int cx = ix + dx, cy = iy + dy, cz = iz + dz;
        int px = ae3d_cn_wrap(cx, period), py = ae3d_cn_wrap(cy, period), pz = ae3d_cn_wrap(cz, period);
        float fx = (float)cx + ae3d_cn_hash3(px, py, pz, seed);
        float fy = (float)cy + ae3d_cn_hash3(px, py, pz, seed + 7u);
        float fz = (float)cz + ae3d_cn_hash3(px, py, pz, seed + 13u);
        float ex = fx - x, ey = fy - y, ez = fz - z;
        float d = ex * ex + ey * ey + ez * ez;
        if (d < best) best = d;
    }
    best = sqrtf(best);
    return 1.0f - (best > 1.0f ? 1.0f : best);
}

static float ae3d_cn_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

/* The shape texture: `size` cubed texels, four bytes each. */
int ae3d_cloudnoise_shape(int size, unsigned char *out) {
    int x, y, z;
    if (size < 8 || !out) return 0;
    for (z = 0; z < size; z++) {
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                float u = (float)x / (float)size, v = (float)y / (float)size, w = (float)z / (float)size;
                unsigned char *texel = out + ((size_t)z * size * size + (size_t)y * size + x) * 4;
                /* Perlin as a fractal of three octaves, tileable at 4, 8, 16
                   cells; the Worley cells at 4, 8, 16 as well. */
                float perlin = ae3d_cn_perlin(u * 4.0f, v * 4.0f, w * 4.0f, 4, 11u) * 0.625f
                             + ae3d_cn_perlin(u * 8.0f, v * 8.0f, w * 8.0f, 8, 23u) * 0.25f
                             + ae3d_cn_perlin(u * 16.0f, v * 16.0f, w * 16.0f, 16, 37u) * 0.125f;
                float worley_low = ae3d_cn_worley(u * 4.0f, v * 4.0f, w * 4.0f, 4, 101u);
                float worley_mid = ae3d_cn_worley(u * 8.0f, v * 8.0f, w * 8.0f, 8, 203u);
                float worley_high = ae3d_cn_worley(u * 16.0f, v * 16.0f, w * 16.0f, 16, 307u);
                float worley_fbm = worley_low * 0.625f + worley_mid * 0.25f + worley_high * 0.125f;
                /* Perlin-Worley: Perlin spread over 0..1 (a gradient fractal
                   sits within about -0.6..0.6), its low end lifted by the
                   Worley fbm, so the noise billows where Perlin does and is
                   cellular where it does not. */
                float perlin01 = ae3d_cn_clamp01(perlin * 0.8f + 0.5f);
                float pw = ae3d_cn_clamp01((perlin01 - (worley_fbm - 1.0f)) / (2.0f - worley_fbm));
                texel[0] = (unsigned char)(ae3d_cn_clamp01(pw) * 255.0f + 0.5f);
                texel[1] = (unsigned char)(worley_low * 255.0f + 0.5f);
                texel[2] = (unsigned char)(worley_mid * 255.0f + 0.5f);
                texel[3] = (unsigned char)(worley_high * 255.0f + 0.5f);
            }
        }
    }
    return 1;
}

/* Value noise on a 2D lattice, tileable at `period`, the way the shaders'
   cloudNoise is on its 3D one with z fixed -- the weather map has to be the
   field the ground shader computes for its cloud shadow. */
static float ae3d_cn_value2(float x, float y, float zslice, int period, unsigned seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float ux = fx * fx * (3.0f - 2.0f * fx), uy = fy * fy * (3.0f - 2.0f * fy);
    float a = ae3d_cn_hash3(ae3d_cn_wrap(ix, period), ae3d_cn_wrap(iy, period), (int)zslice, seed);
    float b = ae3d_cn_hash3(ae3d_cn_wrap(ix + 1, period), ae3d_cn_wrap(iy, period), (int)zslice, seed);
    float c = ae3d_cn_hash3(ae3d_cn_wrap(ix, period), ae3d_cn_wrap(iy + 1, period), (int)zslice, seed);
    float d = ae3d_cn_hash3(ae3d_cn_wrap(ix + 1, period), ae3d_cn_wrap(iy + 1, period), (int)zslice, seed);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

/* The weather map: `size` squared texels, four bytes each. R is the cloud
   field before the cover threshold, in 0..1, as a stretched fractal of value
   noise with slow banks; G the cloud kind. Tileable across the world at
   `size` texels to one weather tile. */
int ae3d_cloudnoise_weather(int size, unsigned char *out) {
    int x, y;
    if (size < 8 || !out) return 0;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            float u = (float)x / (float)size, v = (float)y / (float)size;
            unsigned char *texel = out + ((size_t)y * size + x) * 4;
            /* The same five octaves the scene shader's cloudField sums for
               the ground's cloud shadow: value noise on lattices of 6, 12,
               24, 48 and 96 cells to the tile, each octave's seed in z. */
            float shape = 0.0f, a = 0.5f, freq = 6.0f;
            int o;
            for (o = 0; o < 5; o++) {
                shape += a * ae3d_cn_value2(u * freq, v * freq, (float)(41 + o * 17), (int)freq, 0u);
                a *= 0.5f;
                freq *= 2.0f;
            }
            shape = ae3d_cn_clamp01((shape - 0.3f) / 0.4f);
            {
                float bank = ae3d_cn_value2(u * 3.0f, v * 3.0f, 8.0f, 3, 0u);
                float kind = ae3d_cn_value2(u * 4.0f, v * 4.0f, 5.0f, 4, 0u);
                texel[0] = (unsigned char)(shape * 255.0f + 0.5f);
                texel[1] = (unsigned char)(ae3d_cn_clamp01(kind) * 255.0f + 0.5f);
                texel[2] = (unsigned char)(ae3d_cn_clamp01(bank) * 255.0f + 0.5f);
                texel[3] = 255;
            }
        }
    }
    return 1;
}
