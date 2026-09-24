/* The stores the renderers draw from, as C sees them.
 *
 * A mesh (interleaved float32 vertices, nine a vertex, and its indices) and a
 * model's instances (a matrix each, with colours and phases, or points of
 * eight floats) are made and written by ae3d.geometry, in Aether. The
 * renderers here read them in place: these two structs are that module's
 * GeoMesh and GeoInstances, field for field, and tests/test_geometry holds
 * the two layouts to each other through ae3d_store_field (stores.c). A field
 * added on one side and not the other fails that test, not a frame.
 *
 * The accessors are the ones the renderers always called; they read the
 * fields directly now instead of crossing into another translation unit.
 */
#ifndef AE3D_STORES_H
#define AE3D_STORES_H

#include <stddef.h>

#define AE3D_STRIDE 9
#define AE3D_SKIN_STRIDE 8
#define AE3D_POINT_FLOATS 8

typedef struct {
    float    *vertices;
    unsigned *indices;
    float    *skin;             /* four joints and four weights a vertex, or NULL */
    int       skin_dirty;
    int       vertex_count;
    int       vertex_capacity;
    int       index_count;
    int       index_capacity;
    int       dirty;
    float     bound[3];
    float     radius;
    float     local_bound[3];
    float     local_radius;
    int       local_known;
} ae3d_mesh;

typedef struct {
    float *matrices;
    float *colors;
    float *phases;
    float *points;              /* x, y, z, scale, r, g, b, phase a point, when is_points */
    int    is_points;
    int    count;
    int    capacity;
    int    has_colors;
    int    has_phases;
    float  bound[3];
    float  radius;
} ae3d_inst;

static inline const float *ae3d_mesh_vertex_data(void *h) { return h ? ((ae3d_mesh *)h)->vertices : NULL; }
static inline const unsigned *ae3d_mesh_index_data(void *h) { return h ? ((ae3d_mesh *)h)->indices : NULL; }
static inline const float *ae3d_mesh_skin_data(void *h) { return h ? ((ae3d_mesh *)h)->skin : NULL; }
static inline int ae3d_mesh_vertex_count(void *h) { return h ? ((ae3d_mesh *)h)->vertex_count : 0; }
static inline int ae3d_mesh_index_count(void *h) { return h ? ((ae3d_mesh *)h)->index_count : 0; }
static inline int ae3d_mesh_is_skinned(void *h) { return h && ((ae3d_mesh *)h)->skin ? 1 : 0; }

static inline const float *ae3d_inst_matrix_data(void *h) { return h ? ((ae3d_inst *)h)->matrices : NULL; }
static inline const float *ae3d_inst_color_data(void *h) { return h ? ((ae3d_inst *)h)->colors : NULL; }
static inline const float *ae3d_inst_phase_data(void *h) { return h ? ((ae3d_inst *)h)->phases : NULL; }
static inline const float *ae3d_inst_point_data(void *h) { return h ? ((ae3d_inst *)h)->points : NULL; }
static inline int ae3d_inst_count(void *h) { return h ? ((ae3d_inst *)h)->count : 0; }
static inline int ae3d_inst_is_points(void *h) { return h ? ((ae3d_inst *)h)->is_points : 0; }
static inline int ae3d_inst_has_colors(void *h) { return h ? ((ae3d_inst *)h)->has_colors : 0; }
static inline int ae3d_inst_has_phases(void *h) { return h ? ((ae3d_inst *)h)->has_phases : 0; }

#endif
