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

/* A bank of poses baked from a clip: `frames` bone palettes laid end to end,
 * frame-major, so frame f's bones start at f * bones * 16. One animated crowd
 * shares it -- an instance picks a frame by its own phase and is posed from
 * that slice -- so the upload is fixed at `frames` poses however many entities
 * sample it. Floats, in the palette's own layout, ready to become a texture. */
typedef struct {
    float *values;
    int    frames;
    int    bones;
    /* How far the clip's root has travelled across the ground by each frame,
     * frames + 1 entries so the last spans the loop back to the first. Filled
     * by an in-place bake, which takes that travel out of the poses; the crowd
     * step puts it back as motion, so a zombie moves exactly as fast as its
     * pose is walking and its feet plant instead of skating. */
    double *travel;
} ae3d_posebank;

void *ae3d_posebank_create(int frames, int bones) {
    ae3d_posebank *b;
    size_t n, i;
    if (frames <= 0 || bones <= 0) return NULL;
    b = (ae3d_posebank *)calloc(1, sizeof(ae3d_posebank));
    if (!b) return NULL;
    n = (size_t)frames * (size_t)bones * 16;
    b->values = (float *)calloc(n, sizeof(float));
    if (!b->values) { free(b); return NULL; }
    b->travel = (double *)calloc((size_t)frames + 1, sizeof(double));
    if (!b->travel) { free(b->values); free(b); return NULL; }
    b->frames = frames;
    b->bones = bones;
    /* Identity per bone, so a frame never captured still draws the bind pose
       rather than collapsing the mesh. */
    for (i = 0; i < (size_t)frames * (size_t)bones; i++) {
        float *m = b->values + i * 16;
        m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
    }
    return b;
}

void ae3d_posebank_destroy(void *handle) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    if (!b) return;
    free(b->travel);
    free(b->values);
    free(b);
}

/* The root's travel at frame k (0..frames, the last being the loop point),
 * relative to wherever the bake chose as zero. */
void ae3d_posebank_set_travel(void *handle, int frame, double distance) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    if (!b || frame < 0 || frame > b->frames) return;
    b->travel[frame] = distance;
}

double ae3d_posebank_travel(void *handle, int frame) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    if (!b || frame < 0 || frame > b->frames) return 0.0;
    return b->travel[frame];
}

/* How fast the root is walking at `phase` (0..1 through the clip) when the
 * clip plays over `duration` seconds: the travel across the frame the phase
 * falls in, over that frame's time. A bank baked as authored has no travel
 * recorded and answers zero. */
double ae3d_posebank_speed(void *handle, double phase, double duration) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    int f;
    if (!b || duration <= 0.0 || b->frames <= 0) return 0.0;
    if (phase < 0.0) phase = 0.0;
    f = (int)(phase * b->frames);
    if (f >= b->frames) f = b->frames - 1;
    return (b->travel[f + 1] - b->travel[f]) * (double)b->frames / duration;
}

int ae3d_posebank_frames(void *handle) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    return b ? b->frames : 0;
}

int ae3d_posebank_bones(void *handle) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    return b ? b->bones : 0;
}

/* Copy a filled palette into one frame of the bank. The palette is the live
 * skeleton posed at that frame's time; the bank keeps a snapshot of it. */
void ae3d_posebank_capture(void *handle, int frame, void *palette_handle) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    const float *src = ae3d_palette_data(palette_handle);
    int pbones = ae3d_palette_bones(palette_handle);
    int bones;
    if (!b || !src || frame < 0 || frame >= b->frames) return;
    bones = pbones < b->bones ? pbones : b->bones;
    memcpy(b->values + (size_t)frame * (size_t)b->bones * 16,
           src, (size_t)bones * 16 * sizeof(float));
}

double ae3d_posebank_get(void *handle, int frame, int bone, int i) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    if (!b || frame < 0 || frame >= b->frames || bone < 0 || bone >= b->bones
        || i < 0 || i >= 16) return 0.0;
    return b->values[((size_t)frame * (size_t)b->bones + (size_t)bone) * 16 + i];
}

const float *ae3d_posebank_data(void *handle) {
    ae3d_posebank *b = (ae3d_posebank *)handle;
    return b ? b->values : NULL;
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
