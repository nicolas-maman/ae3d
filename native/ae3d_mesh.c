#include "ae3d.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define AE3D_STRIDE 8

typedef struct {
    float   *vertices;
    unsigned *indices;
    int      vertex_count;
    int      vertex_capacity;
    int      index_count;
    int      index_capacity;
    int      dirty;
    float    bound[3];
    float    radius;
} ae3d_mesh;

typedef struct {
    float *matrices;
    float *colors;
    int    count;
    int    capacity;
    int    has_colors;
    float  bound[3];
    float  radius;
} ae3d_inst;

static void ae3d_quat_rotate(const double q[4], const float in[3], float out[3]) {
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double vx = in[0], vy = in[1], vz = in[2];
    double tx = 2.0 * (y * vz - z * vy);
    double ty = 2.0 * (z * vx - x * vz);
    double tz = 2.0 * (x * vy - y * vx);
    out[0] = (float)(vx + w * tx + (y * tz - z * ty));
    out[1] = (float)(vy + w * ty + (z * tx - x * tz));
    out[2] = (float)(vz + w * tz + (x * ty - y * tx));
}

void *ae3d_mesh_create(void) {
    return calloc(1, sizeof(ae3d_mesh));
}

void ae3d_mesh_destroy(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    if (!m) return;
    free(m->vertices);
    free(m->indices);
    free(m);
}

static int ae3d_mesh_grow_vertices(ae3d_mesh *m, int needed) {
    int capacity;
    float *grown;
    if (needed <= m->vertex_capacity) return 1;
    capacity = m->vertex_capacity ? m->vertex_capacity : 64;
    while (capacity < needed) capacity *= 2;
    grown = (float *)realloc(m->vertices, (size_t)capacity * AE3D_STRIDE * sizeof(float));
    if (!grown) return 0;
    m->vertices = grown;
    m->vertex_capacity = capacity;
    return 1;
}

static int ae3d_mesh_grow_indices(ae3d_mesh *m, int needed) {
    int capacity;
    unsigned *grown;
    if (needed <= m->index_capacity) return 1;
    capacity = m->index_capacity ? m->index_capacity : 64;
    while (capacity < needed) capacity *= 2;
    grown = (unsigned *)realloc(m->indices, (size_t)capacity * sizeof(unsigned));
    if (!grown) return 0;
    m->indices = grown;
    m->index_capacity = capacity;
    return 1;
}

int ae3d_mesh_reserve(void *handle, int vertices, int indices) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    if (!m) return 0;
    if (vertices > 0 && !ae3d_mesh_grow_vertices(m, vertices)) return 0;
    if (indices > 0 && !ae3d_mesh_grow_indices(m, indices)) return 0;
    return 1;
}

int ae3d_mesh_push_vertex(void *handle, double px, double py, double pz,
                          double u, double v, double nx, double ny, double nz) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    float *slot;
    if (!m || !ae3d_mesh_grow_vertices(m, m->vertex_count + 1)) return -1;
    slot = m->vertices + (size_t)m->vertex_count * AE3D_STRIDE;
    slot[0] = (float)px; slot[1] = (float)py; slot[2] = (float)pz;
    slot[3] = (float)u;  slot[4] = (float)v;
    slot[5] = (float)nx; slot[6] = (float)ny; slot[7] = (float)nz;
    m->dirty = 1;
    return m->vertex_count++;
}

int ae3d_mesh_push_index(void *handle, int index) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    if (!m || !ae3d_mesh_grow_indices(m, m->index_count + 1)) return -1;
    m->indices[m->index_count] = (unsigned)index;
    return m->index_count++;
}

int ae3d_mesh_vertex_count(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    return m ? m->vertex_count : 0;
}

int ae3d_mesh_index_count(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    return m ? m->index_count : 0;
}

static float *ae3d_mesh_slot(ae3d_mesh *m, int i) {
    if (!m || i < 0 || i >= m->vertex_count) return NULL;
    return m->vertices + (size_t)i * AE3D_STRIDE;
}

double ae3d_mesh_pos_x(void *handle, int i) {
    float *s = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    return s ? s[0] : 0.0;
}

double ae3d_mesh_pos_y(void *handle, int i) {
    float *s = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    return s ? s[1] : 0.0;
}

double ae3d_mesh_pos_z(void *handle, int i) {
    float *s = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    return s ? s[2] : 0.0;
}

void ae3d_mesh_set_pos(void *handle, int i, double x, double y, double z) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    float *s = ae3d_mesh_slot(m, i);
    if (!s) return;
    s[0] = (float)x; s[1] = (float)y; s[2] = (float)z;
    m->dirty = 1;
}

void ae3d_mesh_set_uv(void *handle, int i, double u, double v) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    float *s = ae3d_mesh_slot(m, i);
    if (!s) return;
    s[3] = (float)u; s[4] = (float)v;
    m->dirty = 1;
}

void ae3d_mesh_set_normal(void *handle, int i, double x, double y, double z) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    float *s = ae3d_mesh_slot(m, i);
    if (!s) return;
    s[5] = (float)x; s[6] = (float)y; s[7] = (float)z;
    m->dirty = 1;
}

int ae3d_mesh_index_at(void *handle, int i) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    if (!m || i < 0 || i >= m->index_count) return -1;
    return (int)m->indices[i];
}

void ae3d_mesh_recalc_normals(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    int i;
    if (!m || m->vertex_count == 0) return;

    for (i = 0; i < m->vertex_count; i++) {
        float *s = m->vertices + (size_t)i * AE3D_STRIDE;
        s[5] = 0.0f; s[6] = 0.0f; s[7] = 0.0f;
    }

    for (i = 0; i + 2 < m->index_count; i += 3) {
        unsigned ia = m->indices[i], ib = m->indices[i + 1], ic = m->indices[i + 2];
        float *a, *b, *c;
        float ux, uy, uz, vx, vy, vz, nx, ny, nz;
        int k;
        unsigned tri[3];

        if ((int)ia >= m->vertex_count || (int)ib >= m->vertex_count || (int)ic >= m->vertex_count)
            continue;

        a = m->vertices + (size_t)ia * AE3D_STRIDE;
        b = m->vertices + (size_t)ib * AE3D_STRIDE;
        c = m->vertices + (size_t)ic * AE3D_STRIDE;

        ux = b[0] - a[0]; uy = b[1] - a[1]; uz = b[2] - a[2];
        vx = c[0] - a[0]; vy = c[1] - a[1]; vz = c[2] - a[2];
        nx = uy * vz - uz * vy;
        ny = uz * vx - ux * vz;
        nz = ux * vy - uy * vx;

        tri[0] = ia; tri[1] = ib; tri[2] = ic;
        for (k = 0; k < 3; k++) {
            float *s = m->vertices + (size_t)tri[k] * AE3D_STRIDE;
            s[5] += nx; s[6] += ny; s[7] += nz;
        }
    }

    for (i = 0; i < m->vertex_count; i++) {
        float *s = m->vertices + (size_t)i * AE3D_STRIDE;
        float len = sqrtf(s[5] * s[5] + s[6] * s[6] + s[7] * s[7]);
        if (len > 1e-8f) {
            s[5] /= len; s[6] /= len; s[7] /= len;
        } else {
            s[5] = 0.0f; s[6] = 1.0f; s[7] = 0.0f;
        }
    }
    m->dirty = 1;
}

void ae3d_mesh_compute_bounds(void *handle,
                              double px, double py, double pz,
                              double sx, double sy, double sz,
                              double qx, double qy, double qz, double qw) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    double quat[4], cx = 0.0, cy = 0.0, cz = 0.0, worst = 0.0;
    int i;

    if (!m) return;
    if (m->vertex_count == 0) {
        m->bound[0] = (float)px; m->bound[1] = (float)py; m->bound[2] = (float)pz;
        m->radius = 1.0f;
        return;
    }

    quat[0] = qx; quat[1] = qy; quat[2] = qz; quat[3] = qw;

    for (i = 0; i < m->vertex_count; i++) {
        const float *s = m->vertices + (size_t)i * AE3D_STRIDE;
        float scaled[3], rotated[3];
        scaled[0] = (float)(s[0] * sx);
        scaled[1] = (float)(s[1] * sy);
        scaled[2] = (float)(s[2] * sz);
        ae3d_quat_rotate(quat, scaled, rotated);
        cx += rotated[0] + px;
        cy += rotated[1] + py;
        cz += rotated[2] + pz;
    }
    cx /= m->vertex_count; cy /= m->vertex_count; cz /= m->vertex_count;

    for (i = 0; i < m->vertex_count; i++) {
        const float *s = m->vertices + (size_t)i * AE3D_STRIDE;
        float scaled[3], rotated[3];
        double dx, dy, dz, distance;
        scaled[0] = (float)(s[0] * sx);
        scaled[1] = (float)(s[1] * sy);
        scaled[2] = (float)(s[2] * sz);
        ae3d_quat_rotate(quat, scaled, rotated);
        dx = rotated[0] + px - cx;
        dy = rotated[1] + py - cy;
        dz = rotated[2] + pz - cz;
        distance = dx * dx + dy * dy + dz * dz;
        if (distance > worst) worst = distance;
    }

    m->bound[0] = (float)cx; m->bound[1] = (float)cy; m->bound[2] = (float)cz;
    m->radius = (float)sqrt(worst);
}

double ae3d_mesh_bound_x(void *handle) { ae3d_mesh *m = handle; return m ? m->bound[0] : 0.0; }
double ae3d_mesh_bound_y(void *handle) { ae3d_mesh *m = handle; return m ? m->bound[1] : 0.0; }
double ae3d_mesh_bound_z(void *handle) { ae3d_mesh *m = handle; return m ? m->bound[2] : 0.0; }
double ae3d_mesh_bound_radius(void *handle) { ae3d_mesh *m = handle; return m ? m->radius : 0.0; }

int  ae3d_mesh_dirty(void *handle) { ae3d_mesh *m = handle; return m ? m->dirty : 0; }
void ae3d_mesh_clear_dirty(void *handle) { ae3d_mesh *m = handle; if (m) m->dirty = 0; }

const float *ae3d_mesh_vertex_data(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    return m ? m->vertices : NULL;
}

const unsigned *ae3d_mesh_index_data(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    return m ? m->indices : NULL;
}

void *ae3d_inst_create(void) {
    return calloc(1, sizeof(ae3d_inst));
}

void ae3d_inst_destroy(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    if (!inst) return;
    free(inst->matrices);
    free(inst->colors);
    free(inst);
}

int ae3d_inst_resize(void *handle, int count) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    int capacity, i;

    if (!inst || count < 0) return 0;
    if (count > inst->capacity) {
        capacity = inst->capacity ? inst->capacity : 16;
        while (capacity < count) capacity *= 2;
        {
            float *grown = (float *)realloc(inst->matrices, (size_t)capacity * 16 * sizeof(float));
            if (!grown) return 0;
            inst->matrices = grown;
        }
        if (inst->has_colors) {
            float *grown = (float *)realloc(inst->colors, (size_t)capacity * 3 * sizeof(float));
            if (!grown) return 0;
            inst->colors = grown;
        }
        inst->capacity = capacity;
    }

    for (i = inst->count; i < count; i++) {
        float *m = inst->matrices + (size_t)i * 16;
        memset(m, 0, 16 * sizeof(float));
        m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
        if (inst->has_colors) {
            float *c = inst->colors + (size_t)i * 3;
            c[0] = 1.0f; c[1] = 1.0f; c[2] = 1.0f;
        }
    }
    inst->count = count;
    return 1;
}

int ae3d_inst_count(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    return inst ? inst->count : 0;
}

void ae3d_inst_set_trs(void *handle, int i,
                       double px, double py, double pz,
                       double sx, double sy, double sz,
                       double qx, double qy, double qz, double qw) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    float *m;
    double xx, yy, zz, xy, xz, yz, wx, wy, wz;

    if (!inst || i < 0 || i >= inst->count) return;
    m = inst->matrices + (size_t)i * 16;

    xx = qx * qx; yy = qy * qy; zz = qz * qz;
    xy = qx * qy; xz = qx * qz; yz = qy * qz;
    wx = qw * qx; wy = qw * qy; wz = qw * qz;

    m[0]  = (float)((1.0 - 2.0 * (yy + zz)) * sx);
    m[1]  = (float)((2.0 * (xy + wz)) * sx);
    m[2]  = (float)((2.0 * (xz - wy)) * sx);
    m[3]  = 0.0f;
    m[4]  = (float)((2.0 * (xy - wz)) * sy);
    m[5]  = (float)((1.0 - 2.0 * (xx + zz)) * sy);
    m[6]  = (float)((2.0 * (yz + wx)) * sy);
    m[7]  = 0.0f;
    m[8]  = (float)((2.0 * (xz + wy)) * sz);
    m[9]  = (float)((2.0 * (yz - wx)) * sz);
    m[10] = (float)((1.0 - 2.0 * (xx + yy)) * sz);
    m[11] = 0.0f;
    m[12] = (float)px; m[13] = (float)py; m[14] = (float)pz; m[15] = 1.0f;
}

// One call per frame instead of one per instance: a particle system that moves
// every instance each frame would otherwise spend the frame in call overhead
// rather than in physics.
void ae3d_inst_set_positions(void *handle, const double *xyz, int count,
                             double sx, double sy, double sz,
                             double qx, double qy, double qz, double qw) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    double xx, yy, zz, xy, xz, yz, wx, wy, wz;
    float basis[9];
    int i, limit;

    if (!inst || !xyz || count <= 0) return;
    limit = count < inst->count ? count : inst->count;

    xx = qx * qx; yy = qy * qy; zz = qz * qz;
    xy = qx * qy; xz = qx * qz; yz = qy * qz;
    wx = qw * qx; wy = qw * qy; wz = qw * qz;

    basis[0] = (float)((1.0 - 2.0 * (yy + zz)) * sx);
    basis[1] = (float)((2.0 * (xy + wz)) * sx);
    basis[2] = (float)((2.0 * (xz - wy)) * sx);
    basis[3] = (float)((2.0 * (xy - wz)) * sy);
    basis[4] = (float)((1.0 - 2.0 * (xx + zz)) * sy);
    basis[5] = (float)((2.0 * (yz + wx)) * sy);
    basis[6] = (float)((2.0 * (xz + wy)) * sz);
    basis[7] = (float)((2.0 * (yz - wx)) * sz);
    basis[8] = (float)((1.0 - 2.0 * (xx + yy)) * sz);

    for (i = 0; i < limit; i++) {
        float *m = inst->matrices + (size_t)i * 16;
        m[0] = basis[0]; m[1] = basis[1]; m[2]  = basis[2]; m[3]  = 0.0f;
        m[4] = basis[3]; m[5] = basis[4]; m[6]  = basis[5]; m[7]  = 0.0f;
        m[8] = basis[6]; m[9] = basis[7]; m[10] = basis[8]; m[11] = 0.0f;
        m[12] = (float)xyz[i * 3];
        m[13] = (float)xyz[i * 3 + 1];
        m[14] = (float)xyz[i * 3 + 2];
        m[15] = 1.0f;
    }
}

void ae3d_inst_set_colors(void *handle, const double *rgb, int count) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    int i, limit;

    if (!inst || !rgb || !inst->has_colors || count <= 0) return;
    limit = count < inst->count ? count : inst->count;
    for (i = 0; i < limit; i++) {
        float *c = inst->colors + (size_t)i * 3;
        c[0] = (float)rgb[i * 3];
        c[1] = (float)rgb[i * 3 + 1];
        c[2] = (float)rgb[i * 3 + 2];
    }
}

void ae3d_inst_set_matrix(void *handle, int i, const double *src) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    float *m;
    int k;
    if (!inst || !src || i < 0 || i >= inst->count) return;
    m = inst->matrices + (size_t)i * 16;
    for (k = 0; k < 16; k++) m[k] = (float)src[k];
}

void ae3d_inst_remove(void *handle, int i) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    int tail;
    if (!inst || i < 0 || i >= inst->count) return;
    tail = inst->count - i - 1;
    if (tail > 0) {
        memmove(inst->matrices + (size_t)i * 16,
                inst->matrices + (size_t)(i + 1) * 16,
                (size_t)tail * 16 * sizeof(float));
        if (inst->has_colors)
            memmove(inst->colors + (size_t)i * 3,
                    inst->colors + (size_t)(i + 1) * 3,
                    (size_t)tail * 3 * sizeof(float));
    }
    inst->count--;
}

int ae3d_inst_enable_colors(void *handle, int count) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    int capacity, i;
    if (!inst) return 0;
    capacity = inst->capacity > count ? inst->capacity : count;
    if (capacity <= 0) capacity = 16;
    {
        float *grown = (float *)realloc(inst->colors, (size_t)capacity * 3 * sizeof(float));
        if (!grown) return 0;
        inst->colors = grown;
    }
    for (i = inst->has_colors ? inst->count : 0; i < capacity; i++) {
        float *c = inst->colors + (size_t)i * 3;
        c[0] = 1.0f; c[1] = 1.0f; c[2] = 1.0f;
    }
    inst->has_colors = 1;
    return 1;
}

int ae3d_inst_has_colors(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    return inst ? inst->has_colors : 0;
}

void ae3d_inst_set_color(void *handle, int i, double r, double g, double b) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    float *c;
    if (!inst || !inst->has_colors || i < 0 || i >= inst->count) return;
    c = inst->colors + (size_t)i * 3;
    c[0] = (float)r; c[1] = (float)g; c[2] = (float)b;
}

void ae3d_inst_compute_bounds(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    double cx = 0.0, cy = 0.0, cz = 0.0, worst = 0.0;
    int i;

    if (!inst || inst->count == 0) return;
    for (i = 0; i < inst->count; i++) {
        const float *m = inst->matrices + (size_t)i * 16;
        cx += m[12]; cy += m[13]; cz += m[14];
    }
    cx /= inst->count; cy /= inst->count; cz /= inst->count;

    for (i = 0; i < inst->count; i++) {
        const float *m = inst->matrices + (size_t)i * 16;
        double dx = m[12] - cx, dy = m[13] - cy, dz = m[14] - cz;
        double distance = dx * dx + dy * dy + dz * dz;
        if (distance > worst) worst = distance;
    }

    inst->bound[0] = (float)cx; inst->bound[1] = (float)cy; inst->bound[2] = (float)cz;
    inst->radius = (float)sqrt(worst);
}

double ae3d_inst_bound_x(void *handle) { ae3d_inst *i = handle; return i ? i->bound[0] : 0.0; }
double ae3d_inst_bound_y(void *handle) { ae3d_inst *i = handle; return i ? i->bound[1] : 0.0; }
double ae3d_inst_bound_z(void *handle) { ae3d_inst *i = handle; return i ? i->bound[2] : 0.0; }
double ae3d_inst_bound_radius(void *handle) { ae3d_inst *i = handle; return i ? i->radius : 0.0; }

const float *ae3d_inst_matrix_data(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    return inst ? inst->matrices : NULL;
}

const float *ae3d_inst_color_data(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    return inst ? inst->colors : NULL;
}

typedef struct {
    int v, vt, vn;
    int unified;
} ae3d_objkey;

typedef struct {
    float *positions;
    float *uvs;
    float *normals;
    int position_count, position_capacity;
    int uv_count, uv_capacity;
    int normal_count, normal_capacity;

    ae3d_objkey *table;
    int table_mask;
    int table_used;
} ae3d_objbuild;

static int ae3d_grow_floats(float **buffer, int *capacity, int needed) {
    int size;
    float *grown;
    if (needed <= *capacity) return 1;
    size = *capacity ? *capacity : 256;
    while (size < needed) size *= 2;
    grown = (float *)realloc(*buffer, (size_t)size * sizeof(float));
    if (!grown) return 0;
    *buffer = grown;
    *capacity = size;
    return 1;
}

static unsigned ae3d_objkey_hash(int v, int vt, int vn) {
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)v) * 16777619u;
    h = (h ^ (unsigned)vt) * 16777619u;
    h = (h ^ (unsigned)vn) * 16777619u;
    return h;
}

static int ae3d_objbuild_grow_table(ae3d_objbuild *b) {
    int old_capacity = b->table_mask + 1;
    int new_capacity = b->table ? old_capacity * 2 : 1024;
    ae3d_objkey *old = b->table;
    ae3d_objkey *fresh = (ae3d_objkey *)calloc((size_t)new_capacity, sizeof(ae3d_objkey));
    int i;

    if (!fresh) return 0;
    for (i = 0; i < new_capacity; i++) fresh[i].unified = -1;

    b->table = fresh;
    b->table_mask = new_capacity - 1;

    if (old) {
        for (i = 0; i < old_capacity; i++) {
            if (old[i].unified >= 0) {
                unsigned slot = ae3d_objkey_hash(old[i].v, old[i].vt, old[i].vn) & (unsigned)b->table_mask;
                while (fresh[slot].unified >= 0) slot = (slot + 1) & (unsigned)b->table_mask;
                fresh[slot] = old[i];
            }
        }
        free(old);
    }
    return 1;
}

void *ae3d_objbuild_create(void) {
    ae3d_objbuild *b = (ae3d_objbuild *)calloc(1, sizeof(ae3d_objbuild));
    if (!b) return NULL;
    if (!ae3d_objbuild_grow_table(b)) { free(b); return NULL; }
    return b;
}

void ae3d_objbuild_destroy(void *handle) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    if (!b) return;
    free(b->positions);
    free(b->uvs);
    free(b->normals);
    free(b->table);
    free(b);
}

int ae3d_objbuild_add_position(void *handle, double x, double y, double z) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    if (!b || !ae3d_grow_floats(&b->positions, &b->position_capacity, b->position_count + 3)) return -1;
    b->positions[b->position_count++] = (float)x;
    b->positions[b->position_count++] = (float)y;
    b->positions[b->position_count++] = (float)z;
    return b->position_count / 3 - 1;
}

int ae3d_objbuild_add_uv(void *handle, double u, double v) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    if (!b || !ae3d_grow_floats(&b->uvs, &b->uv_capacity, b->uv_count + 2)) return -1;
    b->uvs[b->uv_count++] = (float)u;
    b->uvs[b->uv_count++] = (float)v;
    return b->uv_count / 2 - 1;
}

int ae3d_objbuild_add_normal(void *handle, double x, double y, double z) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    if (!b || !ae3d_grow_floats(&b->normals, &b->normal_capacity, b->normal_count + 3)) return -1;
    b->normals[b->normal_count++] = (float)x;
    b->normals[b->normal_count++] = (float)y;
    b->normals[b->normal_count++] = (float)z;
    return b->normal_count / 3 - 1;
}

int ae3d_objbuild_position_count(void *handle) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    return b ? b->position_count / 3 : 0;
}

int ae3d_objbuild_uv_count(void *handle) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    return b ? b->uv_count / 2 : 0;
}

int ae3d_objbuild_normal_count(void *handle) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    return b ? b->normal_count / 3 : 0;
}

// Deduplicating emit: an OBJ face vertex is a (position, uv, normal) triplet, and
// the same triplet reaching this function twice must resolve to one GPU vertex or
// the index buffer grows without bound on large models.
int ae3d_objbuild_emit(void *handle, void *mesh, int v, int vt, int vn) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    unsigned slot;
    int unified;
    double px = 0.0, py = 0.0, pz = 0.0, u = 0.0, tv = 0.0, nx = 0.0, ny = 1.0, nz = 0.0;

    if (!b || !mesh) return -1;

    if ((b->table_used + 1) * 4 >= (b->table_mask + 1) * 3) {
        if (!ae3d_objbuild_grow_table(b)) return -1;
    }

    slot = ae3d_objkey_hash(v, vt, vn) & (unsigned)b->table_mask;
    while (b->table[slot].unified >= 0) {
        if (b->table[slot].v == v && b->table[slot].vt == vt && b->table[slot].vn == vn) {
            return b->table[slot].unified;
        }
        slot = (slot + 1) & (unsigned)b->table_mask;
    }

    if (v >= 0 && v * 3 + 2 < b->position_count) {
        px = b->positions[v * 3];
        py = b->positions[v * 3 + 1];
        pz = b->positions[v * 3 + 2];
    }
    if (vt >= 0 && vt * 2 + 1 < b->uv_count) {
        u  = b->uvs[vt * 2];
        tv = b->uvs[vt * 2 + 1];
    }
    if (vn >= 0 && vn * 3 + 2 < b->normal_count) {
        nx = b->normals[vn * 3];
        ny = b->normals[vn * 3 + 1];
        nz = b->normals[vn * 3 + 2];
    }

    unified = ae3d_mesh_push_vertex(mesh, px, py, pz, u, tv, nx, ny, nz);
    if (unified < 0) return -1;

    b->table[slot].v = v;
    b->table[slot].vt = vt;
    b->table[slot].vn = vn;
    b->table[slot].unified = unified;
    b->table_used++;
    return unified;
}
