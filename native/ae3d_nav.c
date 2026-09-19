/* A flow field for the horde: where to walk, from anywhere, to close on
 * one target.
 *
 * A path per zombie is a search per zombie; a horde of a hundred thousand
 * wants one search for all of them. A flow field is that: the ground as a
 * grid of cells, the cost from every cell to the target found once by a
 * Dijkstra flood from the target outward over the cells nothing stands in,
 * and a direction per cell, toward the cheapest neighbour. A zombie reads the
 * direction under its feet and turns toward it; the search is paid once per
 * target move, the read is a lookup. What blocks a cell is a footprint --
 * a building, a bin, a lamp post -- given as a rectangle on the ground.
 *
 * Aether's float is a C double, so the position and yaw columns are doubles
 * and the field's world coordinates are too; the per-cell direction is a
 * float pair, since a horde-sized field is read every frame by every
 * zombie and half the bytes is half the cache. */
#include "ae3d.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int w, h;
    double x0, z0, cell;
    unsigned char *blocked;  /* w*h: 1 where nothing can walk */
    int *cost;               /* w*h: tenths of a cell to the target, -1 unreachable */
    float *dir;              /* w*h*2: the way to walk, unit, or 0,0 */
    int *queue;              /* the flood's frontier, w*h, a ring */
    unsigned char *queued;   /* w*h: whether a cell waits in the frontier */
} ae3d_flow;

#define AE3D_NAV_PI 3.14159265358979323846

void *ae3d_flow_new(int w, int h, double x0, double z0, double cell) {
    ae3d_flow *f;
    if (w <= 0 || h <= 0 || cell <= 0.0) return NULL;
    f = (ae3d_flow *)calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->w = w; f->h = h; f->x0 = x0; f->z0 = z0; f->cell = cell;
    f->blocked = (unsigned char *)calloc((size_t)w * h, 1);
    f->cost = (int *)malloc(sizeof(int) * (size_t)w * h);
    f->dir = (float *)calloc((size_t)w * h * 2, sizeof(float));
    f->queue = (int *)malloc(sizeof(int) * ((size_t)w * h + 1));  /* one over: a full ring is not an empty one */
    f->queued = (unsigned char *)calloc((size_t)w * h, 1);
    if (!f->blocked || !f->cost || !f->dir || !f->queue || !f->queued) {
        free(f->blocked); free(f->cost); free(f->dir); free(f->queue); free(f->queued); free(f);
        return NULL;
    }
    {
        size_t i;
        for (i = 0; i < (size_t)w * h; i++) f->cost[i] = -1;
    }
    return f;
}

void ae3d_flow_free(void *field) {
    ae3d_flow *f = (ae3d_flow *)field;
    if (!f) return;
    free(f->blocked); free(f->cost); free(f->dir); free(f->queue); free(f->queued); free(f);
}

int ae3d_flow_width(void *field)  { return field ? ((ae3d_flow *)field)->w : 0; }
int ae3d_flow_height(void *field) { return field ? ((ae3d_flow *)field)->h : 0; }

static int flow_col(const ae3d_flow *f, double x) { return (int)floor((x - f->x0) / f->cell); }
static int flow_row(const ae3d_flow *f, double z) { return (int)floor((z - f->z0) / f->cell); }

/* A footprint on the ground, world metres, blocks every cell it touches. */
void ae3d_flow_block(void *field, double bx0, double bz0, double bx1, double bz1) {
    ae3d_flow *f = (ae3d_flow *)field;
    int c0, c1, r0, r1, c, r;
    if (!f) return;
    if (bx1 < bx0) { double t = bx0; bx0 = bx1; bx1 = t; }
    if (bz1 < bz0) { double t = bz0; bz0 = bz1; bz1 = t; }
    c0 = flow_col(f, bx0); c1 = flow_col(f, bx1);
    r0 = flow_row(f, bz0); r1 = flow_row(f, bz1);
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= f->w) c1 = f->w - 1;
    if (r1 >= f->h) r1 = f->h - 1;
    for (r = r0; r <= r1; r++)
        for (c = c0; c <= c1; c++)
            f->blocked[r * f->w + c] = 1;
}

void ae3d_flow_clear(void *field) {
    ae3d_flow *f = (ae3d_flow *)field;
    if (!f) return;
    memset(f->blocked, 0, (size_t)f->w * f->h);
}

int ae3d_flow_blocked_at(void *field, double x, double z) {
    ae3d_flow *f = (ae3d_flow *)field;
    int c, r;
    if (!f) return 1;
    c = flow_col(f, x); r = flow_row(f, z);
    if (c < 0 || r < 0 || c >= f->w || r >= f->h) return 1;
    return f->blocked[r * f->w + c];
}

/* The flood: a Dijkstra from the target over the open cells, eight ways,
 * a straight step 10 and a diagonal 14 (tenths of a cell, integers so the
 * frontier is a bucket queue and not a heap), no diagonal past a blocked
 * corner so nothing is told to walk through the edge of a wall. Every open
 * cell then points at its cheapest neighbour; the target's own cell points
 * nowhere. Returns how many cells can reach the target. */
int ae3d_flow_build(void *field, double tx, double tz) {
    ae3d_flow *f = (ae3d_flow *)field;
    static const int dc[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static const int dr[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    static const int step[8] = { 10, 10, 10, 10, 14, 14, 14, 14 };
    int n, c, r, i, k, head, tail, reached = 0, target;
    if (!f) return 0;
    n = f->w * f->h;
    for (i = 0; i < n; i++) f->cost[i] = -1;
    memset(f->dir, 0, sizeof(float) * (size_t)n * 2);
    c = flow_col(f, tx); r = flow_row(f, tz);
    if (c < 0 || r < 0 || c >= f->w || r >= f->h) return 0;
    target = r * f->w + c;
    if (f->blocked[target]) return 0;

    /* Two step weights on a FIFO settle a cell more than once in the worst
     * case; a cell already waiting in the frontier is not queued twice, so
     * the ring never holds more than every cell once, and the relaxations
     * are bounded since a cost only falls. A field of a few hundred thousand
     * cells floods in a few milliseconds, once per target move. */
    memset(f->queued, 0, (size_t)n);
    f->cost[target] = 0;
    head = 0; tail = 0;
    f->queue[tail++] = target;
    f->queued[target] = 1;
    while (head != tail) {
        int at = f->queue[head++];
        int ar = at / f->w, ac = at % f->w;
        int base = f->cost[at];
        if (head == n + 1) head = 0;
        f->queued[at] = 0;
        for (k = 0; k < 8; k++) {
            int nc = ac + dc[k], nr = ar + dr[k], to, cost;
            if (nc < 0 || nr < 0 || nc >= f->w || nr >= f->h) continue;
            to = nr * f->w + nc;
            if (f->blocked[to]) continue;
            if (k >= 4) {
                /* The two cells the diagonal passes between. */
                if (f->blocked[ar * f->w + nc] || f->blocked[nr * f->w + ac]) continue;
            }
            cost = base + step[k];
            if (f->cost[to] >= 0 && f->cost[to] <= cost) continue;
            f->cost[to] = cost;
            if (f->queued[to]) continue;
            f->queued[to] = 1;
            f->queue[tail++] = to;
            if (tail == n + 1) tail = 0;
        }
    }

    for (r = 0; r < f->h; r++) {
        for (c = 0; c < f->w; c++) {
            int at = r * f->w + c, best = -1, best_cost, dx = 0, dz = 0;
            double len;
            if (f->cost[at] < 0 || at == target) continue;
            reached++;
            best_cost = f->cost[at];
            for (k = 0; k < 8; k++) {
                int nc = c + dc[k], nr = r + dr[k], to;
                if (nc < 0 || nr < 0 || nc >= f->w || nr >= f->h) continue;
                to = nr * f->w + nc;
                if (f->cost[to] < 0) continue;
                if (k >= 4 && (f->blocked[r * f->w + nc] || f->blocked[nr * f->w + c])) continue;
                if (f->cost[to] < best_cost) { best_cost = f->cost[to]; best = k; }
            }
            if (best < 0) continue;
            dx = dc[best]; dz = dr[best];
            len = sqrt((double)(dx * dx + dz * dz));
            f->dir[at * 2]     = (float)(dx / len);
            f->dir[at * 2 + 1] = (float)(dz / len);
        }
    }
    reached++;  /* the target's cell */
    return reached;
}

/* The cost from a point to the target in metres, or -1 where it cannot
 * be reached. */
double ae3d_flow_cost(void *field, double x, double z) {
    ae3d_flow *f = (ae3d_flow *)field;
    int c, r;
    if (!f) return -1.0;
    c = flow_col(f, x); r = flow_row(f, z);
    if (c < 0 || r < 0 || c >= f->w || r >= f->h) return -1.0;
    if (f->cost[r * f->w + c] < 0) return -1.0;
    return f->cost[r * f->w + c] * 0.1 * f->cell;
}

/* The way to walk from a point: 1 with the unit direction in (dx, dz), 0
 * where there is none (off the field, blocked, unreachable, or on the
 * target). */
int ae3d_flow_direction(void *field, double x, double z, double *dx, double *dz) {
    /* Also read one component at a time from Aether, which passes no
     * out-parameters: ae3d_flow_dir_x / _z below. */
    ae3d_flow *f = (ae3d_flow *)field;
    int c, r, at;
    if (dx) *dx = 0.0;
    if (dz) *dz = 0.0;
    if (!f) return 0;
    c = flow_col(f, x); r = flow_row(f, z);
    if (c < 0 || r < 0 || c >= f->w || r >= f->h) return 0;
    at = r * f->w + c;
    if (f->dir[at * 2] == 0.0f && f->dir[at * 2 + 1] == 0.0f) return 0;
    if (dx) *dx = f->dir[at * 2];
    if (dz) *dz = f->dir[at * 2 + 1];
    return 1;
}

/* Every zombie's heading turned toward the field's direction under it, by
 * at most `turn` radians this step, so a horde swings round rather than
 * snapping: the yaw the wander pass reads next (cos a, -sin a). A zombie
 * on a cell with no direction keeps its heading. */
typedef struct { const double *pos; double *yaw; ae3d_flow *f; double turn; } ae3d_flow_steer_job;

static void ae3d_flow_steer_run(void *ctx, int start, int end) {
    ae3d_flow_steer_job *job = (ae3d_flow_steer_job *)ctx;
    ae3d_flow *f = job->f;
    int i;
    for (i = start; i < end; i++) {
        double x = job->pos[i * 3], z = job->pos[i * 3 + 2];
        int c = flow_col(f, x), r = flow_row(f, z), at;
        double want, have, diff;
        if (c < 0 || r < 0 || c >= f->w || r >= f->h) continue;
        at = r * f->w + c;
        if (f->dir[at * 2] == 0.0f && f->dir[at * 2 + 1] == 0.0f) continue;
        /* The yaw convention: velocity (cos a, 0, -sin a), so a = atan2(-dz, dx).
         * The + 0.0 turns a negated zero back into a positive one: atan2(-0, -1)
         * is -pi, the same heading as +pi but the long way round for the turn. */
        want = atan2(-(double)f->dir[at * 2 + 1] + 0.0, (double)f->dir[at * 2]);
        have = job->yaw[i];
        diff = want - have;
        while (diff > AE3D_NAV_PI) diff -= 2.0 * AE3D_NAV_PI;
        while (diff < -AE3D_NAV_PI) diff += 2.0 * AE3D_NAV_PI;
        if (diff > job->turn) diff = job->turn;
        if (diff < -job->turn) diff = -job->turn;
        job->yaw[i] = have + diff;
    }
}

void ae3d_flow_steer(void *field, const double *pos, double *yaw, int n, double turn) {
    ae3d_flow_steer_job job;
    if (!field || !pos || !yaw || n <= 0) return;
    job.pos = pos; job.yaw = yaw; job.f = (ae3d_flow *)field; job.turn = turn;
    ae3d_jobs_for(n, 4096, ae3d_flow_steer_run, &job);
}

double ae3d_flow_dir_x(void *field, double x, double z) {
    double dx = 0.0, dz = 0.0;
    ae3d_flow_direction(field, x, z, &dx, &dz);
    return dx;
}

double ae3d_flow_dir_z(void *field, double x, double z) {
    double dx = 0.0, dz = 0.0;
    ae3d_flow_direction(field, x, z, &dx, &dz);
    return dz;
}
