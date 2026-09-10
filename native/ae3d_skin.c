/* The bone palette a skinned draw is posed by.
 *
 * One matrix per bone: where that bone is now, times the inverse of where it
 * was when the mesh was bound to it. A vertex weighted to a bone is moved by
 * the difference between those two, which is why a mesh in its bind pose comes
 * out of a skinned draw exactly where it went in.
 *
 * Kept as floats here rather than assembled per draw: the caller writes a
 * matrix when a bone moves, and what the driver is handed is the array it
 * wants, with no conversion between the write and the upload.
 */

#include "ae3d.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    float *values;
    int    bones;
} ae3d_palette;

void *ae3d_palette_create(int bones) {
    ae3d_palette *p;
    int i;
    if (bones <= 0) return NULL;
    p = (ae3d_palette *)calloc(1, sizeof(ae3d_palette));
    if (!p) return NULL;
    p->values = (float *)calloc((size_t)bones * 16, sizeof(float));
    if (!p->values) { free(p); return NULL; }
    p->bones = bones;
    /* Identity, so a palette whose bones have not been written yet draws the
       mesh in its bind pose rather than collapsing it to the origin. */
    for (i = 0; i < bones; i++) {
        float *m = p->values + (size_t)i * 16;
        m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
    }
    return p;
}

void ae3d_palette_destroy(void *handle) {
    ae3d_palette *p = (ae3d_palette *)handle;
    if (!p) return;
    free(p->values);
    free(p);
}

int ae3d_palette_bones(void *handle) {
    ae3d_palette *p = (ae3d_palette *)handle;
    return p ? p->bones : 0;
}

void ae3d_palette_set(void *handle, int bone, const double *m) {
    ae3d_palette *p = (ae3d_palette *)handle;
    float *slot;
    int i;
    if (!p || !m || bone < 0 || bone >= p->bones) return;
    slot = p->values + (size_t)bone * 16;
    for (i = 0; i < 16; i++) slot[i] = (float)m[i];
}

double ae3d_palette_get(void *handle, int bone, int i) {
    ae3d_palette *p = (ae3d_palette *)handle;
    if (!p || bone < 0 || bone >= p->bones || i < 0 || i >= 16) return 0.0;
    return p->values[(size_t)bone * 16 + i];
}

const float *ae3d_palette_data(void *handle) {
    ae3d_palette *p = (ae3d_palette *)handle;
    return p ? p->values : NULL;
}

/* The weights an OBJ's positions carry, on the way in.
 *
 * A loaded mesh has more vertices than the OBJ has positions: a position with
 * two normals across a hard edge is two vertices, and only the loader knows
 * which. So the weights are handed to the loader keyed by position, and it
 * writes each one onto whatever vertices that position turned into. Nothing is
 * kept afterwards, and a mesh loaded without a skin allocates none of it.
 */

typedef struct {
    float *rows;
    int    count;
} ae3d_skinrows;

void *ae3d_skinrows_create(int count) {
    ae3d_skinrows *r;
    if (count <= 0) return NULL;
    r = (ae3d_skinrows *)calloc(1, sizeof(ae3d_skinrows));
    if (!r) return NULL;
    r->rows = (float *)calloc((size_t)count * 8, sizeof(float));
    if (!r->rows) { free(r); return NULL; }
    r->count = count;
    return r;
}

void ae3d_skinrows_destroy(void *handle) {
    ae3d_skinrows *r = (ae3d_skinrows *)handle;
    if (!r) return;
    free(r->rows);
    free(r);
}

int ae3d_skinrows_count(void *handle) {
    ae3d_skinrows *r = (ae3d_skinrows *)handle;
    return r ? r->count : 0;
}

void ae3d_skinrows_set(void *handle, int i, int j0, int j1, int j2, int j3,
                       double w0, double w1, double w2, double w3) {
    ae3d_skinrows *r = (ae3d_skinrows *)handle;
    float *slot;
    if (!r || i < 0 || i >= r->count) return;
    slot = r->rows + (size_t)i * 8;
    slot[0] = (float)j0; slot[1] = (float)j1; slot[2] = (float)j2; slot[3] = (float)j3;
    slot[4] = (float)w0; slot[5] = (float)w1; slot[6] = (float)w2; slot[7] = (float)w3;
}

void ae3d_skinrows_apply(void *handle, void *mesh, int vertex, int position) {
    ae3d_skinrows *r = (ae3d_skinrows *)handle;
    const float *slot;
    if (!r || !mesh || position < 0 || position >= r->count) return;
    slot = r->rows + (size_t)position * 8;
    ae3d_mesh_set_skin(mesh, vertex, (int)slot[0], (int)slot[1], (int)slot[2], (int)slot[3],
                       slot[4], slot[5], slot[6], slot[7]);
}
