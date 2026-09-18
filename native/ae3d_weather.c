/* The particles of the weather, stepped in C.
 *
 * Rain, snow and dust are point instances -- a position a particle, streamed
 * to the GPU every frame the way the sand's grains are -- and the step that
 * moves them is one pass over a packed xyz array: fall, drift with the wind,
 * a sway for the flakes, and a wrap that keeps every particle inside a box
 * that travels with the camera, so the weather is wherever the eye is and
 * costs the same however big the world. A hundred thousand drops are a
 * fraction of a millisecond here and would be a hundred thousand calls from
 * Aether. */
#include <math.h>
#include <stdlib.h>

/* A cheap, deterministic hash to 0..1 for scattering. */
static double ae3d_weather_hash(unsigned n) {
    n ^= n >> 16u; n *= 0x7feb352du; n ^= n >> 15u; n *= 0x846ca68bu; n ^= n >> 16u;
    return (double)n * (1.0 / 4294967296.0);
}

/* Scatters `count` particles through the box of half-extents (hx, hy, hz)
   around (cx, cy, cz); `phases` (may be NULL) get a phase each for the sway.
   `seed` tells one system's scatter from another's. */
void ae3d_weather_scatter(double *xyz, double *phases, int count,
                          double cx, double cy, double cz,
                          double hx, double hy, double hz, unsigned seed) {
    int i;
    if (!xyz || count <= 0) return;
    for (i = 0; i < count; i++) {
        unsigned n = (unsigned)i * 2654435761u + seed;
        xyz[i * 3 + 0] = cx + (ae3d_weather_hash(n) * 2.0 - 1.0) * hx;
        xyz[i * 3 + 1] = cy + (ae3d_weather_hash(n + 1u) * 2.0 - 1.0) * hy;
        xyz[i * 3 + 2] = cz + (ae3d_weather_hash(n + 2u) * 2.0 - 1.0) * hz;
        if (phases) phases[i] = ae3d_weather_hash(n + 3u) * 6.283185307;
    }
}

/* One step of `dt` seconds: every particle falls at `fall` metres a second,
   drifts with the wind (wx, wy, wz), sways sideways by `sway` metres at its
   own phase (a flake's flutter, a drop's none), and is wrapped back into the
   box around the camera at (cx, cy, cz) with half-extents (hx, hy, hz): one
   leaving the bottom comes in at the top at a fresh x and z, one leaving a
   side comes in at the other. `time` is the clock the sway runs on.
   Returns the count, for a caller that wants nothing else. */
int ae3d_weather_step(double *xyz, const double *phases, int count, double dt,
                      double fall, double wx, double wy, double wz, double sway, double time,
                      double cx, double cy, double cz, double hx, double hy, double hz) {
    int i;
    if (!xyz || count <= 0) return 0;
    for (i = 0; i < count; i++) {
        double *p = xyz + i * 3;
        double phase = phases ? phases[i] : 0.0;
        double sx = sway * cos(time * 1.7 + phase) * dt;
        double sz = sway * sin(time * 1.3 + phase * 1.31) * dt;
        p[0] += wx * dt + sx;
        p[1] += (wy - fall) * dt;
        p[2] += wz * dt + sz;
        if (p[1] < cy - hy) {
            unsigned n = (unsigned)i * 2246822519u + (unsigned)(time * 1000.0);
            p[1] += 2.0 * hy;
            p[0] = cx + (ae3d_weather_hash(n) * 2.0 - 1.0) * hx;
            p[2] = cz + (ae3d_weather_hash(n + 1u) * 2.0 - 1.0) * hz;
        } else if (p[1] > cy + hy) {
            p[1] -= 2.0 * hy;
        }
        if (p[0] < cx - hx) p[0] += 2.0 * hx; else if (p[0] > cx + hx) p[0] -= 2.0 * hx;
        if (p[2] < cz - hz) p[2] += 2.0 * hz; else if (p[2] > cz + hz) p[2] -= 2.0 * hz;
    }
    return count;
}
