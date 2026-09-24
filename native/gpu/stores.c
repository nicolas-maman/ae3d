/* The C half of the layout contract in stores.h: a store's size and any of
 * its fields, read the way the renderers read them, for tests/test_geometry
 * to set against what ae3d.geometry wrote through its own struct. */
#include "ae3d.h"
#include "stores.h"

int ae3d_store_size(int which) {
    return which == 0 ? (int)sizeof(ae3d_mesh) : (int)sizeof(ae3d_inst);
}

/* Field `field` of a mesh (which 0) or an instance store (which 1), in the
 * order the structs declare them, as a double: pointers as non-zero or zero,
 * the three-float bounds by their first float. */
double ae3d_store_field(void *handle, int which, int field) {
    if (!handle) return -1.0;
    if (which == 0) {
        ae3d_mesh *m = (ae3d_mesh *)handle;
        switch (field) {
        case 0: return m->vertices ? 1.0 : 0.0;
        case 1: return m->indices ? 1.0 : 0.0;
        case 2: return m->skin ? 1.0 : 0.0;
        case 3: return m->skin_dirty;
        case 4: return m->vertex_count;
        case 5: return m->vertex_capacity;
        case 6: return m->index_count;
        case 7: return m->index_capacity;
        case 8: return m->dirty;
        case 9: return m->bound[0];
        case 10: return m->bound[1];
        case 11: return m->bound[2];
        case 12: return m->radius;
        case 13: return m->local_bound[0];
        case 14: return m->local_radius;
        case 15: return m->local_known;
        case 16: return m->vertices ? m->vertices[0] : -1.0;
        case 17: return m->indices ? (double)m->indices[0] : -1.0;
        default: return -1.0;
        }
    }
    {
        ae3d_inst *n = (ae3d_inst *)handle;
        switch (field) {
        case 0: return n->matrices ? 1.0 : 0.0;
        case 1: return n->colors ? 1.0 : 0.0;
        case 2: return n->phases ? 1.0 : 0.0;
        case 3: return n->points ? 1.0 : 0.0;
        case 4: return n->is_points;
        case 5: return n->count;
        case 6: return n->capacity;
        case 7: return n->has_colors;
        case 8: return n->has_phases;
        case 9: return n->bound[0];
        case 10: return n->bound[1];
        case 11: return n->bound[2];
        case 12: return n->radius;
        case 13: return n->matrices ? n->matrices[12] : -1.0;
        default: return -1.0;
        }
    }
}
