#include "ae3d.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Position, texture coordinate, normal, and how much of the sky this vertex
   can see. Occlusion belongs to a vertex the way a normal does -- it is a
   property of where the surface sits among the rest of the scene -- so it
   rides in the vertex rather than in a buffer beside it, and every mesh has
   one whether or not anything baked it. */
#define AE3D_STRIDE 9
#define AE3D_SKIN_STRIDE 8

typedef struct {
    float   *vertices;
    unsigned *indices;
    /* Four joints and four weights per vertex, in their own array rather than
       interleaved with the rest: a mesh that is not skinned never allocates
       it, and the stride every unskinned draw reads stays what it was. */
    float   *skin;
    int      skin_dirty;
    int      vertex_count;
    int      vertex_capacity;
    int      index_count;
    int      index_capacity;
    int      dirty;
    float    bound[3];
    float    radius;
    float    local_bound[3];
    float    local_radius;
    int      local_known;
} ae3d_mesh;

typedef struct {
    float *matrices;
    float *colors;
    float *phases;      /* per-instance animation phase 0..1, for a crowd's skinning */
    int    count;
    int    capacity;
    int    has_colors;
    int    has_phases;
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

void *ae3d_mesh_clone(void *handle) {
    const ae3d_mesh *src = (const ae3d_mesh *)handle;
    ae3d_mesh *copy;

    if (!src) return NULL;
    copy = (ae3d_mesh *)calloc(1, sizeof(ae3d_mesh));
    if (!copy) return NULL;

    if (src->vertex_count > 0) {
        size_t bytes = (size_t)src->vertex_count * AE3D_STRIDE * sizeof(float);
        copy->vertices = (float *)malloc(bytes);
        if (!copy->vertices) { free(copy); return NULL; }
        memcpy(copy->vertices, src->vertices, bytes);
        copy->vertex_count = src->vertex_count;
        copy->vertex_capacity = src->vertex_count;
    }

    if (src->index_count > 0) {
        size_t bytes = (size_t)src->index_count * sizeof(unsigned);
        copy->indices = (unsigned *)malloc(bytes);
        if (!copy->indices) { free(copy->vertices); free(copy); return NULL; }
        memcpy(copy->indices, src->indices, bytes);
        copy->index_count = src->index_count;
        copy->index_capacity = src->index_count;
    }

    if (src->skin && src->vertex_count > 0) {
        size_t bytes = (size_t)src->vertex_count * AE3D_SKIN_STRIDE * sizeof(float);
        copy->skin = (float *)calloc((size_t)src->vertex_count * AE3D_SKIN_STRIDE,
                                     sizeof(float));
        if (!copy->skin) { free(copy->indices); free(copy->vertices); free(copy); return NULL; }
        memcpy(copy->skin, src->skin, bytes);
        copy->skin_dirty = 1;
    }

    copy->dirty = src->dirty;
    copy->bound[0] = src->bound[0];
    copy->bound[1] = src->bound[1];
    copy->bound[2] = src->bound[2];
    copy->radius = src->radius;
    return copy;
}

void ae3d_mesh_destroy(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    if (!m) return;
    free(m->vertices);
    free(m->indices);
    free(m->skin);
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
    if (m->skin) {
        float *skin = (float *)realloc(m->skin,
                                       (size_t)capacity * AE3D_SKIN_STRIDE * sizeof(float));
        if (!skin) return 0;
        memset(skin + (size_t)m->vertex_capacity * AE3D_SKIN_STRIDE, 0,
               (size_t)(capacity - m->vertex_capacity) * AE3D_SKIN_STRIDE * sizeof(float));
        m->skin = skin;
    }
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
    /* Unoccluded until something says otherwise, so a mesh nobody baked is
       lit exactly as it was before this existed. */
    slot[8] = 1.0f;
    m->dirty = 1;
    m->local_known = 0;
    return m->vertex_count++;
}

/* Weights arrive as authored and are normalised here, because a renderer that
   trusts them cannot tell a mesh whose weights sum to 0.98 from one that is
   meant to shrink. A vertex with no weight at all is bound rigidly to joint 0,
   which is what an unweighted vertex on a skinned mesh means. */
int ae3d_mesh_set_skin(void *handle, int i, int j0, int j1, int j2, int j3,
                       double w0, double w1, double w2, double w3) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    float *slot;
    double total;

    if (!m || i < 0 || i >= m->vertex_count) return 0;
    if (!m->skin) {
        m->skin = (float *)calloc((size_t)m->vertex_capacity * AE3D_SKIN_STRIDE,
                                  sizeof(float));
        if (!m->skin) return 0;
    }
    slot = m->skin + (size_t)i * AE3D_SKIN_STRIDE;
    slot[0] = (float)j0; slot[1] = (float)j1;
    slot[2] = (float)j2; slot[3] = (float)j3;
    total = w0 + w1 + w2 + w3;
    if (total > 1e-6) {
        slot[4] = (float)(w0 / total); slot[5] = (float)(w1 / total);
        slot[6] = (float)(w2 / total); slot[7] = (float)(w3 / total);
    } else {
        slot[4] = 1.0f; slot[5] = 0.0f; slot[6] = 0.0f; slot[7] = 0.0f;
        slot[0] = 0.0f;
    }
    m->skin_dirty = 1;
    return 1;
}

int ae3d_mesh_is_skinned(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    return (m && m->skin) ? 1 : 0;
}

const float *ae3d_mesh_skin_data(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    return m ? m->skin : NULL;
}

double ae3d_mesh_skin_joint(void *handle, int i, int slot) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    if (!m || !m->skin || i < 0 || i >= m->vertex_count || slot < 0 || slot > 3) return 0.0;
    return m->skin[(size_t)i * AE3D_SKIN_STRIDE + slot];
}

double ae3d_mesh_skin_weight(void *handle, int i, int slot) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    if (!m || !m->skin || i < 0 || i >= m->vertex_count || slot < 0 || slot > 3) return 0.0;
    return m->skin[(size_t)i * AE3D_SKIN_STRIDE + 4 + slot];
}

static float *ae3d_mesh_slot(ae3d_mesh *m, int i);

/* How much of the sky a vertex can see: one is open, zero is buried. Baked
   against the whole scene rather than against the mesh it belongs to, because
   what darkens the foot of a wall is the pavement, which is a different
   object. */
void ae3d_mesh_set_occlusion(void *handle, int i, double value) {
    float *slot = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    if (!slot) return;
    if (value < 0.0) value = 0.0;
    if (value > 1.0) value = 1.0;
    slot[8] = (float)value;
    ((ae3d_mesh *)handle)->dirty = 1;
}

double ae3d_mesh_occlusion(void *handle, int i) {
    float *slot = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    return slot ? slot[8] : 1.0;
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

// The twin of ae3d_mesh_set_normal. A mesh built by generated geometry, a
// surface out of a field most of all, can only be checked for facing the right
// way if what it wrote can be read back.
double ae3d_mesh_norm_x(void *handle, int i) {
    float *s = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    return s ? s[5] : 0.0;
}

double ae3d_mesh_norm_y(void *handle, int i) {
    float *s = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    return s ? s[6] : 0.0;
}

double ae3d_mesh_norm_z(void *handle, int i) {
    float *s = ae3d_mesh_slot((ae3d_mesh *)handle, i);
    return s ? s[7] : 0.0;
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

// The mesh's own centre and the furthest vertex from it, worked out once. A
// transform cannot change what this says, so it survives every move the model
// makes and is redone only when the vertices themselves change.
static void ae3d_mesh_local_bounds(ae3d_mesh *m) {
    double cx = 0.0, cy = 0.0, cz = 0.0, worst = 0.0;
    int i;

    if (m->local_known) return;
    m->local_known = 1;

    if (m->vertex_count == 0) {
        m->local_bound[0] = 0.0f;
        m->local_bound[1] = 0.0f;
        m->local_bound[2] = 0.0f;
        m->local_radius = 0.0f;
        return;
    }

    for (i = 0; i < m->vertex_count; i++) {
        const float *s = m->vertices + (size_t)i * AE3D_STRIDE;
        cx += s[0]; cy += s[1]; cz += s[2];
    }
    cx /= m->vertex_count; cy /= m->vertex_count; cz /= m->vertex_count;

    for (i = 0; i < m->vertex_count; i++) {
        const float *s = m->vertices + (size_t)i * AE3D_STRIDE;
        double dx = s[0] - cx, dy = s[1] - cy, dz = s[2] - cz;
        double distance = dx * dx + dy * dy + dz * dz;
        if (distance > worst) worst = distance;
    }

    m->local_bound[0] = (float)cx;
    m->local_bound[1] = (float)cy;
    m->local_bound[2] = (float)cz;
    m->local_radius = (float)sqrt(worst);
}

// An affine transform commutes with averaging, so the world centre is the local
// centre put through the same transform. Rotation moves every vertex the same
// distance around that centre, so only scale can change the radius, and the
// largest scale factor is the one that has to fit.
void ae3d_mesh_compute_bounds(void *handle,
                              double px, double py, double pz,
                              double sx, double sy, double sz,
                              double qx, double qy, double qz, double qw) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    double quat[4], largest;
    float scaled[3], rotated[3];

    if (!m) return;
    if (m->vertex_count == 0) {
        m->bound[0] = (float)px; m->bound[1] = (float)py; m->bound[2] = (float)pz;
        m->radius = 1.0f;
        return;
    }

    ae3d_mesh_local_bounds(m);

    quat[0] = qx; quat[1] = qy; quat[2] = qz; quat[3] = qw;
    scaled[0] = (float)(m->local_bound[0] * sx);
    scaled[1] = (float)(m->local_bound[1] * sy);
    scaled[2] = (float)(m->local_bound[2] * sz);
    ae3d_quat_rotate(quat, scaled, rotated);

    m->bound[0] = (float)(rotated[0] + px);
    m->bound[1] = (float)(rotated[1] + py);
    m->bound[2] = (float)(rotated[2] + pz);

    largest = fabs(sx);
    if (fabs(sy) > largest) largest = fabs(sy);
    if (fabs(sz) > largest) largest = fabs(sz);
    m->radius = (float)(m->local_radius * largest);
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
    free(inst->phases);
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
        if (inst->has_phases) {
            float *grown = (float *)realloc(inst->phases, (size_t)capacity * sizeof(float));
            if (!grown) return 0;
            inst->phases = grown;
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
        if (inst->has_phases) { inst->phases[i] = 0.0f; }
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

/* Per-instance animation phase, the crowd's counterpart of per-instance colour:
 * one float an instance saying where in the baked walk it is, so the skinning
 * shader poses each from a different frame of the pose bank. */
int ae3d_inst_enable_phases(void *handle, int count) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    int capacity, i;
    if (!inst) return 0;
    capacity = inst->capacity > count ? inst->capacity : count;
    if (capacity <= 0) capacity = 16;
    {
        float *grown = (float *)realloc(inst->phases, (size_t)capacity * sizeof(float));
        if (!grown) return 0;
        inst->phases = grown;
    }
    for (i = inst->has_phases ? inst->count : 0; i < capacity; i++) {
        inst->phases[i] = 0.0f;
    }
    inst->has_phases = 1;
    return 1;
}

int ae3d_inst_has_phases(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    return inst ? inst->has_phases : 0;
}

const float *ae3d_inst_phase_data(void *handle) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    return inst ? inst->phases : NULL;
}

void ae3d_inst_set_phases(void *handle, const double *phases, int count) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    int i, limit;

    if (!inst || !phases || !inst->has_phases || count <= 0) return;
    limit = count < inst->count ? count : inst->count;
    for (i = 0; i < limit; i++) {
        inst->phases[i] = (float)phases[i];
    }
}

void ae3d_inst_set_phase(void *handle, int i, double phase) {
    ae3d_inst *inst = (ae3d_inst *)handle;
    if (!inst || !inst->has_phases || i < 0 || i >= inst->count) return;
    inst->phases[i] = (float)phase;
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
    /* Borrowed for the length of one load: the loader is the only thing that
       knows which vertices a position turned into, and both of these are
       written per position. */
    void *skin;
    void *occlusion;
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

void ae3d_objbuild_set_skin(void *handle, void *rows) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    if (b) b->skin = rows;
}

void ae3d_objbuild_set_occlusion(void *handle, void *values) {
    ae3d_objbuild *b = (ae3d_objbuild *)handle;
    if (b) b->occlusion = values;
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
    if (b->skin) ae3d_skinrows_apply(b->skin, mesh, unified, v);
    if (b->occlusion && v >= 0 && v < ae3d_farr_count(b->occlusion)) {
        ae3d_mesh_set_occlusion(mesh, unified, ae3d_farr_get(b->occlusion, v));
    }

    b->table[slot].v = v;
    b->table[slot].vt = vt;
    b->table[slot].vn = vn;
    b->table[slot].unified = unified;
    b->table_used++;
    return unified;
}

/* What a surface is made of, in numbers a scene can be judged by.
 *
 * Texel density is the one that decides whether a wall reads as brick or as a
 * photograph of brick seen from an inch away: it is how many times the texture
 * repeats across a metre of the surface, and it is a property of the UVs and
 * the geometry together, so neither the image nor the mesh can be asked about
 * it alone. Distinct face normals stand in for silhouette -- a box has six
 * whatever its triangle count, and a figure that reads as a figure has
 * hundreds.
 *
 * Rotation does not change an area and translation does not either, so the
 * world transform enters here only as its scale.
 */

static void ae3d_tri_of(const float *v, int i0, int i1, int i2,
                        double sx, double sy, double sz, double out[9]) {
    const float *a = v + (size_t)i0 * AE3D_STRIDE;
    const float *b = v + (size_t)i1 * AE3D_STRIDE;
    const float *c = v + (size_t)i2 * AE3D_STRIDE;
    out[0] = a[0] * sx; out[1] = a[1] * sy; out[2] = a[2] * sz;
    out[3] = b[0] * sx; out[4] = b[1] * sy; out[5] = b[2] * sz;
    out[6] = c[0] * sx; out[7] = c[1] * sy; out[8] = c[2] * sz;
}

double ae3d_mesh_surface_area(void *handle, double sx, double sy, double sz) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    double total = 0.0;
    int i;
    if (!m || !m->vertices || !m->indices) return 0.0;
    for (i = 0; i + 2 < m->index_count; i += 3) {
        double t[9], ux, uy, uz, vx, vy, vz, cx, cy, cz;
        ae3d_tri_of(m->vertices, (int)m->indices[i], (int)m->indices[i + 1],
                    (int)m->indices[i + 2], sx, sy, sz, t);
        ux = t[3] - t[0]; uy = t[4] - t[1]; uz = t[5] - t[2];
        vx = t[6] - t[0]; vy = t[7] - t[1]; vz = t[8] - t[2];
        cx = uy * vz - uz * vy;
        cy = uz * vx - ux * vz;
        cz = ux * vy - uy * vx;
        total += 0.5 * sqrt(cx * cx + cy * cy + cz * cz);
    }
    return total;
}

double ae3d_mesh_uv_area(void *handle) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    double total = 0.0;
    int i;
    if (!m || !m->vertices || !m->indices) return 0.0;
    for (i = 0; i + 2 < m->index_count; i += 3) {
        const float *a = m->vertices + (size_t)m->indices[i] * AE3D_STRIDE;
        const float *b = m->vertices + (size_t)m->indices[i + 1] * AE3D_STRIDE;
        const float *c = m->vertices + (size_t)m->indices[i + 2] * AE3D_STRIDE;
        double ux = b[3] - a[3], uy = b[4] - a[4];
        double vx = c[3] - a[3], vy = c[4] - a[4];
        total += 0.5 * fabs(ux * vy - uy * vx);
    }
    return total;
}

#define AE3D_NORMAL_SLOTS 8192

/* Distinct planes, which is what relief is.
 *
 * Counting directions alone cannot tell a box from a facade: a wall with
 * recessed windows, sills and a cornice is built entirely from faces pointing
 * the same six ways as the box it started as. What separates them is that the
 * facade's faces lie in many planes and the box's lie in six. So the key is
 * the normal and the distance along it, quantised together, and a shape that
 * has been given depth counts higher than one that has only been painted. */
int ae3d_mesh_distinct_planes(void *handle, double tolerance) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    int *keys;
    int distinct = 0, i;
    double step;

    if (!m || !m->vertices || !m->indices) return 0;
    if (tolerance < 1e-4) tolerance = 1e-4;
    step = 1.0 / tolerance;
    keys = (int *)calloc(AE3D_NORMAL_SLOTS, sizeof(int));
    if (!keys) return 0;

    for (i = 0; i + 2 < m->index_count; i += 3) {
        double t[9], ux, uy, uz, vx, vy, vz, cx, cy, cz, length, offset;
        int qx, qy, qz, qd;
        unsigned slot, guard;
        int key;

        ae3d_tri_of(m->vertices, (int)m->indices[i], (int)m->indices[i + 1],
                    (int)m->indices[i + 2], 1.0, 1.0, 1.0, t);
        ux = t[3] - t[0]; uy = t[4] - t[1]; uz = t[5] - t[2];
        vx = t[6] - t[0]; vy = t[7] - t[1]; vz = t[8] - t[2];
        cx = uy * vz - uz * vy;
        cy = uz * vx - ux * vz;
        cz = ux * vy - uy * vx;
        length = sqrt(cx * cx + cy * cy + cz * cz);
        if (length < 1e-9) continue;
        cx /= length; cy /= length; cz /= length;
        qx = (int)floor(cx * step + 0.5);
        qy = (int)floor(cy * step + 0.5);
        qz = (int)floor(cz * step + 0.5);
        /* How far the plane stands off the origin along its own normal, in
           centimetres: two faces pointing the same way a window's depth apart
           are two planes, and that depth is what a facade is made of. */
        offset = t[0] * cx + t[1] * cy + t[2] * cz;
        qd = (int)floor(offset * 100.0 + 0.5);
        if (qd < -32768) qd = -32768;
        if (qd > 32767) qd = 32767;
        /* Never zero, so a filled slot is distinguishable from an empty one. */
        key = (((qx + 4096) * 8209 + (qy + 4096)) * 8209 + (qz + 4096)) * 65537
              + qd + 1;
        slot = ((unsigned)key * 2654435761u) % AE3D_NORMAL_SLOTS;
        for (guard = 0; guard < AE3D_NORMAL_SLOTS; guard++) {
            if (keys[slot] == 0) { keys[slot] = key; distinct++; break; }
            if (keys[slot] == key) break;
            slot = (slot + 1) % AE3D_NORMAL_SLOTS;
        }
    }
    free(keys);
    return distinct;
}

/* How far a mesh reaches along one of its own axes, in its own space.
 *
 * The bounding sphere says how big something is and nothing about its shape,
 * which is the wrong question to ask of a figure: proportion is a ratio of
 * extents, and a head is judged against a height rather than against a radius.
 */
double ae3d_mesh_extent(void *handle, int axis) {
    ae3d_mesh *m = (ae3d_mesh *)handle;
    double low, high;
    int i;
    if (!m || !m->vertices || m->vertex_count <= 0 || axis < 0 || axis > 2) return 0.0;
    low = m->vertices[axis];
    high = low;
    for (i = 1; i < m->vertex_count; i++) {
        double v = m->vertices[(size_t)i * AE3D_STRIDE + axis];
        if (v < low) low = v;
        if (v > high) high = v;
    }
    return high - low;
}

/* Runtime mesh decimation, by vertex clustering, for the crowd LOD.
 *
 * A crowd cannot draw the hero mesh: half a million times twenty-six thousand
 * triangles is thirteen billion a frame. It draws a decimated stand-in, and the
 * reason to build it here rather than in a DCC tool is that any mesh the engine
 * loads then gets a crowd LOD with no round-trip -- the figure that walks in the
 * demo is the one the crowd is made of, only coarser.
 *
 * Clustering rather than edge-collapse: bucket the vertices into a uniform grid
 * of `cell_size`, collapse every vertex sharing a cell to one representative
 * (its cell's averaged position, normal and texcoord), and rewrite the triangles
 * onto the representatives, dropping the ones that fold to a line. It is O(n), it
 * never opens a hole, and -- the part that matters for a skinned crowd -- it
 * carries the skin: a representative's weights are the sum of its cell's, the
 * four heaviest joints kept and (by set_skin) renormalised, so the coarse figure
 * bends on the same skeleton as the fine one. Larger cells, fewer triangles.
 *
 * Occlusion (vertex slot 8) is left at its default: a crowd is lit by the scene,
 * not by the per-vertex bake meant for the hero figure seen up close. Returns a
 * new mesh the caller owns, or null on a bad argument or an allocation failure.
 */
#define AE3D_DECIMATE_MAXJ 16
void *ae3d_mesh_decimate(void *handle, double cell_size) {
    ae3d_mesh *src = (ae3d_mesh *)handle;
    void *out;
    int n, ic, i, k, r, reps = 0, cap, skinned;
    double inv;
    int *remap = NULL, *cnt = NULL, *sj = NULL, *nj = NULL, *hval = NULL;
    double *acc = NULL, *sw = NULL;
    char *hused = NULL;
    long long *hkey = NULL;

    if (!src || src->vertex_count <= 0 || cell_size <= 0.0) return NULL;
    n = src->vertex_count;
    ic = src->index_count;
    inv = 1.0 / cell_size;
    skinned = src->skin ? 1 : 0;

    cap = 64;
    while (cap < n * 2) cap *= 2;

    remap = (int *)malloc((size_t)n * sizeof(int));
    acc   = (double *)calloc((size_t)n * 8, sizeof(double));
    cnt   = (int *)calloc((size_t)n, sizeof(int));
    hkey  = (long long *)malloc((size_t)cap * sizeof(long long));
    hval  = (int *)malloc((size_t)cap * sizeof(int));
    hused = (char *)calloc((size_t)cap, sizeof(char));
    if (skinned) {
        sj = (int *)malloc((size_t)n * AE3D_DECIMATE_MAXJ * sizeof(int));
        sw = (double *)calloc((size_t)n * AE3D_DECIMATE_MAXJ, sizeof(double));
        nj = (int *)calloc((size_t)n, sizeof(int));
    }
    if (!remap || !acc || !cnt || !hkey || !hval || !hused ||
        (skinned && (!sj || !sw || !nj))) {
        free(remap); free(acc); free(cnt); free(hkey); free(hval); free(hused);
        free(sj); free(sw); free(nj);
        return NULL;
    }

    /* Assign each vertex to its cell's representative, accumulating as we go.
       The cell key packs three 21-bit signed cell coordinates, which covers a
       million cells an axis -- far more than any figure at a sane cell size. */
    for (i = 0; i < n; i++) {
        const float *vs = src->vertices + (size_t)i * AE3D_STRIDE;
        long long cx = (long long)floor((double)vs[0] * inv);
        long long cy = (long long)floor((double)vs[1] * inv);
        long long cz = (long long)floor((double)vs[2] * inv);
        long long key = ((cx & 0x1FFFFFLL) << 42) | ((cy & 0x1FFFFFLL) << 21)
                        | (cz & 0x1FFFFFLL);
        unsigned h = (unsigned)((unsigned long long)key * 1103515245ULL + 12345ULL)
                     & (unsigned)(cap - 1);
        double *a;
        while (hused[h] && hkey[h] != key) h = (h + 1) & (unsigned)(cap - 1);
        if (!hused[h]) { hused[h] = 1; hkey[h] = key; hval[h] = reps; r = reps; reps++; }
        else r = hval[h];
        remap[i] = r;
        a = acc + (size_t)r * 8;
        a[0] += vs[0]; a[1] += vs[1]; a[2] += vs[2];
        a[3] += vs[3]; a[4] += vs[4];
        a[5] += vs[5]; a[6] += vs[6]; a[7] += vs[7];
        cnt[r]++;
        if (skinned) {
            const float *sk = src->skin + (size_t)i * AE3D_SKIN_STRIDE;
            int *rj = sj + (size_t)r * AE3D_DECIMATE_MAXJ;
            double *rw = sw + (size_t)r * AE3D_DECIMATE_MAXJ;
            for (k = 0; k < 4; k++) {
                int j = (int)sk[k];
                double w = (double)sk[4 + k];
                int m, found = 0;
                if (w <= 0.0) continue;
                for (m = 0; m < nj[r]; m++) {
                    if (rj[m] == j) { rw[m] += w; found = 1; break; }
                }
                if (!found && nj[r] < AE3D_DECIMATE_MAXJ) {
                    rj[nj[r]] = j; rw[nj[r]] = w; nj[r]++;
                }
            }
        }
    }

    out = ae3d_mesh_create();
    if (!out || !ae3d_mesh_reserve(out, reps, ic)) {
        if (out) ae3d_mesh_destroy(out);
        free(remap); free(acc); free(cnt); free(hkey); free(hval); free(hused);
        free(sj); free(sw); free(nj);
        return NULL;
    }

    /* One vertex per representative: the cell's average, normal renormalised. */
    for (r = 0; r < reps; r++) {
        double *a = acc + (size_t)r * 8;
        double c = cnt[r] > 0 ? (double)cnt[r] : 1.0;
        double nx = a[5], ny = a[6], nz = a[7];
        double nl = sqrt(nx * nx + ny * ny + nz * nz);
        if (nl > 1e-8) { nx /= nl; ny /= nl; nz /= nl; }
        else { nx = 0.0; ny = 1.0; nz = 0.0; }
        ae3d_mesh_push_vertex(out, a[0] / c, a[1] / c, a[2] / c,
                              a[3] / c, a[4] / c, nx, ny, nz);
    }

    /* Carry the skin: the four heaviest joints of each representative's cell,
       handed to set_skin, which renormalises them. */
    if (skinned) {
        for (r = 0; r < reps; r++) {
            int *rj = sj + (size_t)r * AE3D_DECIMATE_MAXJ;
            double *rw = sw + (size_t)r * AE3D_DECIMATE_MAXJ;
            int top[4]; double tw[4]; int t, m;
            for (t = 0; t < 4; t++) { top[t] = 0; tw[t] = 0.0; }
            for (m = 0; m < nj[r]; m++) {
                int minidx = 0;
                for (t = 1; t < 4; t++) if (tw[t] < tw[minidx]) minidx = t;
                if (rw[m] > tw[minidx]) { tw[minidx] = rw[m]; top[minidx] = rj[m]; }
            }
            ae3d_mesh_set_skin(out, r, top[0], top[1], top[2], top[3],
                               tw[0], tw[1], tw[2], tw[3]);
        }
    }

    /* Rewrite the triangles onto the representatives, dropping any that fold to
       a line (two corners in one cell). */
    for (i = 0; i + 2 < ic; i += 3) {
        int r0 = remap[src->indices[i]];
        int r1 = remap[src->indices[i + 1]];
        int r2 = remap[src->indices[i + 2]];
        if (r0 == r1 || r1 == r2 || r0 == r2) continue;
        ae3d_mesh_push_index(out, r0);
        ae3d_mesh_push_index(out, r1);
        ae3d_mesh_push_index(out, r2);
    }

    free(remap); free(acc); free(cnt); free(hkey); free(hval); free(hused);
    free(sj); free(sw); free(nj);
    return out;
}
