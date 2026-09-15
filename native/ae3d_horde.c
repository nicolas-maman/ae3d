/* The horde's separation, in C.
 *
 * Pushing a crowd apart is the one super-linear pass in the horde update: every
 * entity against the neighbours in the nine cells around it. At a few thousand
 * that is nothing, but at half a million the tens of millions of neighbour
 * tests a frame are too much for a per-element language loop -- the same wall
 * the component columns hit before they were indexed as raw arrays.
 *
 * The wall is not arithmetic, though; it is memory. A grid that stores only
 * entity indices still has to reach back into the global position buffer for
 * every neighbour, and those reads land all over a twelve-megabyte array -- a
 * cache miss each, tens of millions a frame. That is why a first C port, index
 * grid and all, ran no faster than the Aether it replaced: both were bound by
 * the same scattered gather, not by the language.
 *
 * So this grid is a counting sort that packs the positions themselves cell by
 * cell (a CSR layout: a per-cell offset table over four packed arrays). The
 * neighbour scan then walks a cell's entities as one contiguous run, and the
 * hot loop reads straight down memory instead of chasing indices. The only
 * scattered access left is one write of the accumulated push back to each
 * entity's velocity, once. The grid's arrays are C longs, matching Aether's
 * 64-bit int, and are reused across frames, grown only if the crowd does. The
 * crowd is taken as the compact range [0, n) -- a crowd that only ever spawns,
 * which is what a horde is; a store that recycles slots would pass its live
 * count and compaction instead.
 */

#include "ae3d.h"

#include <stdlib.h>
#include <math.h>

typedef struct {
    long long *off;   /* cells+1 cell offsets into the packed arrays (CSR)  */
    long long *cur;   /* cells within-cell write cursor during the scatter  */
    double *px;       /* packed positions, cell-major: x, y and z of every  */
    double *py;       /* entity laid out so a cell's entities are one run   */
    double *pz;
    double *pux;      /* accumulated push per packed entity, scattered to    */
    double *puy;      /* velocity once at the end; kept packed so a pair's   */
    double *puz;      /* two equal-and-opposite writes both stay local       */
    long long *pidx;  /* the entity's original index, packed the same way   */
    int cols;
    int per_cell;     /* neighbours considered per cell, so cost is bounded  */
    long long cap;    /* capacity of the packed arrays, in entities         */
} ae3d_horde_grid;

void *ae3d_horde_grid_create(int cols, int per_cell) {
    ae3d_horde_grid *g;
    long long cells;
    if (cols <= 0) return NULL;
    g = (ae3d_horde_grid *)calloc(1, sizeof(ae3d_horde_grid));
    if (!g) return NULL;
    cells = (long long)cols * (long long)cols;
    g->off = (long long *)calloc((size_t)(cells + 1), sizeof(long long));
    g->cur = (long long *)calloc((size_t)cells, sizeof(long long));
    if (!g->off || !g->cur) {
        free(g->off);
        free(g->cur);
        free(g);
        return NULL;
    }
    g->cols = cols;
    g->per_cell = per_cell > 0 ? per_cell : 24;
    return g;
}

void ae3d_horde_grid_destroy(void *handle) {
    ae3d_horde_grid *g = (ae3d_horde_grid *)handle;
    if (!g) return;
    free(g->off);
    free(g->cur);
    free(g->px);
    free(g->py);
    free(g->pz);
    free(g->pux);
    free(g->puy);
    free(g->puz);
    free(g->pidx);
    free(g);
}

/* Grow the packed arrays to hold at least n entities, keeping nothing (they
 * are rewritten in full each frame). Returns 0 on an allocation failure. */
static int ae3d_horde_reserve(ae3d_horde_grid *g, long long n) {
    if (n <= g->cap) return 1;
    free(g->px);
    free(g->py);
    free(g->pz);
    free(g->pux);
    free(g->puy);
    free(g->puz);
    free(g->pidx);
    g->px = (double *)malloc((size_t)n * sizeof(double));
    g->py = (double *)malloc((size_t)n * sizeof(double));
    g->pz = (double *)malloc((size_t)n * sizeof(double));
    g->pux = (double *)malloc((size_t)n * sizeof(double));
    g->puy = (double *)malloc((size_t)n * sizeof(double));
    g->puz = (double *)malloc((size_t)n * sizeof(double));
    g->pidx = (long long *)malloc((size_t)n * sizeof(long long));
    if (!g->px || !g->py || !g->pz || !g->pux || !g->puy || !g->puz || !g->pidx) {
        g->cap = 0;
        return 0;
    }
    g->cap = n;
    return 1;
}

void ae3d_horde_separate(void *handle, double *pos, double *vel, int n,
                         double ox, double oz, double cell_size,
                         double radius, double strength) {
    ae3d_horde_grid *g = (ae3d_horde_grid *)handle;
    int cols, i;
    long long cells, c, p;
    double inv, r2;

    if (!g || !pos || !vel || n <= 0 || cell_size <= 0.0) return;
    cols = g->cols;
    cells = (long long)cols * (long long)cols;
    inv = 1.0 / cell_size;
    r2 = radius * radius;
    if (!ae3d_horde_reserve(g, n)) return;

    /* Histogram: how many entities fall in each cell. off[cell+1] is used as
     * the counter so the prefix sum below turns it straight into the start. */
    for (c = 0; c <= cells; c++) g->off[c] = 0;
    for (i = 0; i < n; i++) {
        int cx = (int)((pos[i * 3] - ox) * inv);
        int cz = (int)((pos[i * 3 + 2] - oz) * inv);
        if (cx < 0) cx = 0;
        if (cx >= cols) cx = cols - 1;
        if (cz < 0) cz = 0;
        if (cz >= cols) cz = cols - 1;
        g->off[(long long)cz * cols + cx + 1]++;
    }
    /* Prefix sum, capped: a cell keeps at most per_cell of its entities, so a
     * jammed cell -- half a million zombies in a street pack tens into one --
     * costs a bounded number of neighbour tests instead of quadratic. The rest
     * are simply not pushed this frame; the sort order shifts next frame, so it
     * is a different few each time and the crowd still spreads. off[c+1] holds
     * cell c's full count from the histogram; clamp it to the cap. */
    for (c = 0; c < cells; c++) {
        long long full = g->off[c + 1];
        long long cap = full < (long long)g->per_cell ? full : (long long)g->per_cell;
        g->off[c + 1] = g->off[c] + cap;
        g->cur[c] = g->off[c];
    }

    /* Scatter every entity's position and index into its cell's run, and clear
     * its push accumulator. */
    for (i = 0; i < n; i++) {
        int cx = (int)((pos[i * 3] - ox) * inv);
        int cz = (int)((pos[i * 3 + 2] - oz) * inv);
        long long cell;
        if (cx < 0) cx = 0;
        if (cx >= cols) cx = cols - 1;
        if (cz < 0) cz = 0;
        if (cz >= cols) cz = cols - 1;
        cell = (long long)cz * cols + cx;
        p = g->cur[cell];
        if (p < g->off[cell + 1]) {   /* room in this cell, up to the cap */
            g->cur[cell] = p + 1;
            g->px[p] = pos[i * 3];
            g->py[p] = pos[i * 3 + 1];
            g->pz[p] = pos[i * 3 + 2];
            g->pidx[p] = i;
            g->pux[p] = 0.0;
            g->puy[p] = 0.0;
            g->puz[p] = 0.0;
        }
    }

    /* Push apart every pair of entities closer than the radius, each pair once.
     * A pair pushes both members equally and oppositely (Newton's third law),
     * so scanning only the forward half of the neighbourhood -- entries later
     * in the same cell, and the four cells that sort after this one -- visits
     * every unordered pair exactly once and halves the work. Both the read of
     * the neighbour and the equal-and-opposite write land in the packed arrays,
     * so a cell's run stays a sequential sweep; the accumulated push is
     * scattered back to velocity once, afterwards. The four forward cells are
     * (+1,0), (-1,+1), (0,+1), (+1,+1): the rest of this row and the whole next
     * row, which are exactly the neighbours whose cell index exceeds c. */
    {
        static const int FDX[4] = { 1, -1, 0, 1 };
        static const int FDZ[4] = { 0, 1, 1, 1 };
        for (c = 0; c < cells; c++) {
            int cx = (int)(c % cols);
            int cz = (int)(c / cols);
            long long a = g->off[c];
            long long b = g->off[c + 1];
            long long s, ns;
            int f;
            for (s = a; s < b; s++) {
                double px = g->px[s];
                double py = g->py[s];
                double pz = g->pz[s];
                double sx = 0.0, sy = 0.0, sz = 0.0;
                /* Later entries in this same cell. */
                for (ns = s + 1; ns < b; ns++) {
                    double ddx = px - g->px[ns];
                    double ddy = py - g->py[ns];
                    double ddz = pz - g->pz[ns];
                    double dsq = ddx * ddx + ddy * ddy + ddz * ddz;
                    if (dsq > 0.00000001 && dsq < r2) {
                        double dist = sqrt(dsq);
                        double fc = (radius - dist) / (dist * radius);
                        double fx = ddx * fc, fy = ddy * fc, fz = ddz * fc;
                        sx += fx; sy += fy; sz += fz;
                        g->pux[ns] -= fx; g->puy[ns] -= fy; g->puz[ns] -= fz;
                    }
                }
                /* The four cells that sort after this one. */
                for (f = 0; f < 4; f++) {
                    int ncx = cx + FDX[f];
                    int ncz = cz + FDZ[f];
                    long long nc, ne;
                    if (ncx < 0 || ncx >= cols || ncz < 0 || ncz >= cols) continue;
                    nc = (long long)ncz * cols + ncx;
                    ne = g->off[nc + 1];
                    for (ns = g->off[nc]; ns < ne; ns++) {
                        double ddx = px - g->px[ns];
                        double ddy = py - g->py[ns];
                        double ddz = pz - g->pz[ns];
                        double dsq = ddx * ddx + ddy * ddy + ddz * ddz;
                        if (dsq > 0.00000001 && dsq < r2) {
                            double dist = sqrt(dsq);
                            double fc = (radius - dist) / (dist * radius);
                            double fx = ddx * fc, fy = ddy * fc, fz = ddz * fc;
                            sx += fx; sy += fy; sz += fz;
                            g->pux[ns] -= fx; g->puy[ns] -= fy; g->puz[ns] -= fz;
                        }
                    }
                }
                g->pux[s] += sx;
                g->puy[s] += sy;
                g->puz[s] += sz;
            }
        }
    }

    /* Scatter the accumulated push back to each entity's velocity. */
    for (p = 0; p < (long long)n; p++) {
        long long self = g->pidx[p];
        vel[self * 3] += g->pux[p] * strength;
        vel[self * 3 + 1] += g->puy[p] * strength;
        vel[self * 3 + 2] += g->puz[p] * strength;
    }
}

/* The crowd's motion and its distance sort, in C.
 *
 * At tens of thousands the per-frame walk over the crowd -- set a velocity from
 * each zombie's heading, step it, bounce it off the street, advance its walk,
 * and sort it into the near and far draw buffers -- was a per-element language
 * loop, and it, not the GPU, was the wall a city-sized crowd hit. So it lives
 * here, straight over the packed xyz position, velocity, and the per-zombie yaw
 * and phase the ECS columns already hold. Aether's float is a C double, so the
 * buffers are doubles; a heading and a phase are one each, a position and a
 * velocity three.
 */

/* Point each zombie's velocity along its heading at `speed`. Separation adds to
 * this afterwards; the step below reads the sum. */
void ae3d_crowd_wander(double *vel, const double *yaw, int n, double speed) {
    int i;
    if (!vel || !yaw) return;
    for (i = 0; i < n; i++) {
        double a = yaw[i];
        vel[i * 3]     = cos(a) * speed;
        vel[i * 3 + 1] = 0.0;
        vel[i * 3 + 2] = -sin(a) * speed;
    }
}

/* Cap the speed, step along the velocity, bounce off the street's edges (which
 * turns the heading so the zombie walks back in and the crowd stays in the
 * city), keep it on the road, face it along its heading, and advance its walk
 * with its pace. One pass. */
void ae3d_crowd_step(double *pos, const double *vel, double *yaw, double *phase,
                     int n, double dt, double max_speed,
                     double x0, double x1, double z0, double z1,
                     double road_y, double walk) {
    int i;
    double pi = 3.14159265358979323846;
    double step_scale = walk > 0.0 ? dt / walk : dt;
    if (!pos || !vel || !yaw || !phase) return;
    for (i = 0; i < n; i++) {
        double vx = vel[i * 3];
        double vz = vel[i * 3 + 2];
        double sp = sqrt(vx * vx + vz * vz);
        double a = yaw[i];
        double nx, nz, ph;
        int bounced = 0;
        if (sp > max_speed && sp > 1e-9) {
            double s = max_speed / sp;
            vx *= s; vz *= s; sp = max_speed;
        }
        nx = pos[i * 3] + vx * dt;
        nz = pos[i * 3 + 2] + vz * dt;
        if (nx < x0) { nx = x0; a = pi - a; bounced = 1; }
        if (nx > x1) { nx = x1; a = pi - a; bounced = 1; }
        if (nz < z0) { nz = z0; a = -a; bounced = 1; }
        if (nz > z1) { nz = z1; a = -a; bounced = 1; }
        pos[i * 3]     = nx;
        pos[i * 3 + 1] = road_y;
        pos[i * 3 + 2] = nz;
        if (bounced) yaw[i] = a;
        else if (sp > 0.1) yaw[i] = atan2(-vz, vx);
        ph = phase[i] + step_scale * (0.35 + sp * 0.3);
        while (ph >= 1.0) ph -= 1.0;
        phase[i] = ph;
    }
}

/* Sort the crowd into the near and far draw buffers by distance to the camera,
 * compacting each into its own contiguous run of position, yaw and phase.
 * Returns the near count; the far count is n minus it. */
int ae3d_crowd_bucket(const double *pos, const double *yaw, const double *phase, int n,
                      double cx, double cz, double near_dist,
                      double *np, double *ny, double *nph,
                      double *fp, double *fy, double *fph) {
    int i, nn = 0, nf = 0;
    double nd2 = near_dist * near_dist;
    if (!pos || !yaw || !phase) return 0;
    for (i = 0; i < n; i++) {
        double x = pos[i * 3];
        double y = pos[i * 3 + 1];
        double z = pos[i * 3 + 2];
        double dx = x - cx;
        double dz = z - cz;
        if (dx * dx + dz * dz < nd2) {
            np[nn * 3] = x; np[nn * 3 + 1] = y; np[nn * 3 + 2] = z;
            ny[nn] = yaw[i]; nph[nn] = phase[i]; nn++;
        } else {
            fp[nf * 3] = x; fp[nf * 3 + 1] = y; fp[nf * 3 + 2] = z;
            fy[nf] = yaw[i]; fph[nf] = phase[i]; nf++;
        }
    }
    return nn;
}
