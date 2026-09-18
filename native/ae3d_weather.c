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
#include "ae3d.h"
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
typedef struct {
    double *xyz; const double *phases;
    double dt, fall, wx, wy, wz, sway, time, cx, cy, cz, hx, hy, hz;
} ae3d_weather_job;

static void ae3d_weather_step_run(void *ctx, int start, int end) {
    ae3d_weather_job *j = (ae3d_weather_job *)ctx;
    double dt = j->dt, time = j->time;
    int i;
    for (i = start; i < end; i++) {
        double *p = j->xyz + i * 3;
        double phase = j->phases ? j->phases[i] : 0.0;
        double sx = j->sway * cos(time * 1.7 + phase) * dt;
        double sz = j->sway * sin(time * 1.3 + phase * 1.31) * dt;
        p[0] += j->wx * dt + sx;
        p[1] += (j->wy - j->fall) * dt;
        p[2] += j->wz * dt + sz;
        if (p[1] < j->cy - j->hy) {
            unsigned n = (unsigned)i * 2246822519u + (unsigned)(time * 1000.0);
            p[1] += 2.0 * j->hy;
            p[0] = j->cx + (ae3d_weather_hash(n) * 2.0 - 1.0) * j->hx;
            p[2] = j->cz + (ae3d_weather_hash(n + 1u) * 2.0 - 1.0) * j->hz;
        } else if (p[1] > j->cy + j->hy) {
            p[1] -= 2.0 * j->hy;
        }
        if (p[0] < j->cx - j->hx) p[0] += 2.0 * j->hx; else if (p[0] > j->cx + j->hx) p[0] -= 2.0 * j->hx;
        if (p[2] < j->cz - j->hz) p[2] += 2.0 * j->hz; else if (p[2] > j->cz + j->hz) p[2] -= 2.0 * j->hz;
    }
}

/* Over the job pool: a hundred thousand drops are a sine and a cosine
   each, which is a millisecond on one core and a fraction over them all. */
int ae3d_weather_step(double *xyz, const double *phases, int count, double dt,
                      double fall, double wx, double wy, double wz, double sway, double time,
                      double cx, double cy, double cz, double hx, double hy, double hz) {
    ae3d_weather_job j;
    if (!xyz || count <= 0) return 0;
    j.xyz = xyz; j.phases = phases;
    j.dt = dt; j.fall = fall; j.wx = wx; j.wy = wy; j.wz = wz; j.sway = sway; j.time = time;
    j.cx = cx; j.cy = cy; j.cz = cz; j.hx = hx; j.hy = hy; j.hz = hz;
    ae3d_jobs_for(count, 8192, ae3d_weather_step_run, &j);
    return count;
}
