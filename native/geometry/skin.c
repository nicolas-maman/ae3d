/* The skin weights an OBJ's positions carry, on the way into the loader.
 *
 * Bone palettes and pose banks are ae3d.posing (Aether); these rows stay
 * beside the OBJ builder in mesh.c, which writes them onto vertices as it
 * makes them, until that moves too (#398).
 */

#include "ae3d.h"

#include <stdlib.h>
#include <string.h>

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
