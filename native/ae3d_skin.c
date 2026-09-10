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
