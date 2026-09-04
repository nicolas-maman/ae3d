#include "ae3d.h"
#include "ae3d_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define AE3D_MESH_MAGIC   0x4D455348u
#define AE3D_MESH_VERSION 1u
#define AE3D_MESH_INSTANCED 1u
#define AE3D_STRIDE 8

typedef struct {
    void *mesh;
    void *instances;
} ae3d_meshfile;

static char g_meshfile_error[512];

const char *ae3d_meshfile_error(void) { return g_meshfile_error; }

static int ae3d_meshfile_fail(const char *reason) {
    snprintf(g_meshfile_error, sizeof(g_meshfile_error), "%s", reason);
    return 0;
}

static int ae3d_write_u32(gzFile file, unsigned value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFF);
    bytes[1] = (unsigned char)((value >> 8) & 0xFF);
    bytes[2] = (unsigned char)((value >> 16) & 0xFF);
    bytes[3] = (unsigned char)((value >> 24) & 0xFF);
    return gzwrite(file, bytes, 4) == 4;
}

static int ae3d_read_u32(gzFile file, unsigned *value) {
    unsigned char bytes[4];
    if (gzread(file, bytes, 4) != 4) return 0;
    *value = (unsigned)bytes[0] | ((unsigned)bytes[1] << 8) |
             ((unsigned)bytes[2] << 16) | ((unsigned)bytes[3] << 24);
    return 1;
}

// float32 is written as its IEEE-754 bit pattern in little-endian order rather
// than as raw memory, so a file written on one host reads back identically on
// another whatever its byte order.
static int ae3d_write_f32(gzFile file, float value) {
    unsigned bits;
    memcpy(&bits, &value, sizeof(bits));
    return ae3d_write_u32(file, bits);
}

static int ae3d_read_f32(gzFile file, float *value) {
    unsigned bits;
    if (!ae3d_read_u32(file, &bits)) return 0;
    memcpy(value, &bits, sizeof(*value));
    return 1;
}

int ae3d_meshfile_save(const char *path, void *mesh, void *instances) {
    gzFile file;
    const float *vertices = ae3d_mesh_vertex_data(mesh);
    const unsigned *indices = ae3d_mesh_index_data(mesh);
    int vertex_count = ae3d_mesh_vertex_count(mesh);
    int index_count = ae3d_mesh_index_count(mesh);
    int instance_count = instances ? ae3d_inst_count(instances) : 0;
    unsigned flags = instance_count > 0 ? AE3D_MESH_INSTANCED : 0u;
    int i;

    if (!path || !mesh) return ae3d_meshfile_fail("no mesh to write");
    if (vertex_count <= 0) return ae3d_meshfile_fail("mesh has no vertices");

    file = gzopen(path, "wb");
    if (!file) return ae3d_meshfile_fail("cannot open the file for writing");

    if (!ae3d_write_u32(file, AE3D_MESH_MAGIC) ||
        !ae3d_write_u32(file, AE3D_MESH_VERSION) ||
        !ae3d_write_u32(file, flags) ||
        !ae3d_write_u32(file, (unsigned)vertex_count) ||
        !ae3d_write_u32(file, (unsigned)index_count)) {
        gzclose(file);
        return ae3d_meshfile_fail("cannot write the header");
    }

    for (i = 0; i < vertex_count * AE3D_STRIDE; i++) {
        if (!ae3d_write_f32(file, vertices[i])) {
            gzclose(file);
            return ae3d_meshfile_fail("cannot write the vertices");
        }
    }
    for (i = 0; i < index_count; i++) {
        if (!ae3d_write_u32(file, indices[i])) {
            gzclose(file);
            return ae3d_meshfile_fail("cannot write the indices");
        }
    }

    if (flags & AE3D_MESH_INSTANCED) {
        const float *matrices = ae3d_inst_matrix_data(instances);
        const float *colors = ae3d_inst_color_data(instances);
        int has_colors = ae3d_inst_has_colors(instances);

        if (!ae3d_write_u32(file, (unsigned)instance_count) ||
            !ae3d_write_u32(file, has_colors ? 1u : 0u)) {
            gzclose(file);
            return ae3d_meshfile_fail("cannot write the instance header");
        }
        for (i = 0; i < instance_count * 16; i++) {
            if (!ae3d_write_f32(file, matrices[i])) {
                gzclose(file);
                return ae3d_meshfile_fail("cannot write the instance matrices");
            }
        }
        if (has_colors && colors) {
            for (i = 0; i < instance_count * 3; i++) {
                if (!ae3d_write_f32(file, colors[i])) {
                    gzclose(file);
                    return ae3d_meshfile_fail("cannot write the instance colours");
                }
            }
        }
    }

    if (gzclose(file) != Z_OK) return ae3d_meshfile_fail("cannot flush the file");
    g_meshfile_error[0] = '\0';
    return 1;
}

void *ae3d_meshfile_load(const char *path) {
    gzFile file;
    ae3d_meshfile *loaded;
    unsigned magic = 0, version = 0, flags = 0, vertex_count = 0, index_count = 0;
    unsigned i;

    if (!path) { ae3d_meshfile_fail("no path"); return NULL; }

    file = gzopen(path, "rb");
    if (!file) { ae3d_meshfile_fail("cannot open the file for reading"); return NULL; }

    if (!ae3d_read_u32(file, &magic) || !ae3d_read_u32(file, &version) ||
        !ae3d_read_u32(file, &flags) || !ae3d_read_u32(file, &vertex_count) ||
        !ae3d_read_u32(file, &index_count)) {
        gzclose(file);
        ae3d_meshfile_fail("truncated header");
        return NULL;
    }
    if (magic != AE3D_MESH_MAGIC) {
        gzclose(file);
        ae3d_meshfile_fail("not an ae3d mesh file");
        return NULL;
    }
    if (version != AE3D_MESH_VERSION) {
        gzclose(file);
        ae3d_meshfile_fail("unsupported mesh file version");
        return NULL;
    }

    loaded = (ae3d_meshfile *)calloc(1, sizeof(ae3d_meshfile));
    if (!loaded) { gzclose(file); ae3d_meshfile_fail("out of memory"); return NULL; }

    loaded->mesh = ae3d_mesh_create();
    if (!loaded->mesh || !ae3d_mesh_reserve(loaded->mesh, (int)vertex_count, (int)index_count)) {
        gzclose(file);
        ae3d_mesh_destroy(loaded->mesh);
        free(loaded);
        ae3d_meshfile_fail("out of memory");
        return NULL;
    }

    for (i = 0; i < vertex_count; i++) {
        float slot[AE3D_STRIDE];
        int k;
        for (k = 0; k < AE3D_STRIDE; k++) {
            if (!ae3d_read_f32(file, &slot[k])) {
                gzclose(file);
                ae3d_mesh_destroy(loaded->mesh);
                free(loaded);
                ae3d_meshfile_fail("truncated vertex data");
                return NULL;
            }
        }
        ae3d_mesh_push_vertex(loaded->mesh, slot[0], slot[1], slot[2],
                              slot[3], slot[4], slot[5], slot[6], slot[7]);
    }

    for (i = 0; i < index_count; i++) {
        unsigned index = 0;
        if (!ae3d_read_u32(file, &index)) {
            gzclose(file);
            ae3d_mesh_destroy(loaded->mesh);
            free(loaded);
            ae3d_meshfile_fail("truncated index data");
            return NULL;
        }
        ae3d_mesh_push_index(loaded->mesh, (int)index);
    }

    if (flags & AE3D_MESH_INSTANCED) {
        unsigned instance_count = 0, has_colors = 0;
        if (!ae3d_read_u32(file, &instance_count) || !ae3d_read_u32(file, &has_colors)) {
            gzclose(file);
            ae3d_mesh_destroy(loaded->mesh);
            free(loaded);
            ae3d_meshfile_fail("truncated instance header");
            return NULL;
        }

        loaded->instances = ae3d_inst_create();
        if (!loaded->instances || !ae3d_inst_resize(loaded->instances, (int)instance_count)) {
            gzclose(file);
            ae3d_inst_destroy(loaded->instances);
            ae3d_mesh_destroy(loaded->mesh);
            free(loaded);
            ae3d_meshfile_fail("out of memory");
            return NULL;
        }

        for (i = 0; i < instance_count; i++) {
            double matrix[16];
            int k;
            for (k = 0; k < 16; k++) {
                float value = 0.0f;
                if (!ae3d_read_f32(file, &value)) {
                    gzclose(file);
                    ae3d_inst_destroy(loaded->instances);
                    ae3d_mesh_destroy(loaded->mesh);
                    free(loaded);
                    ae3d_meshfile_fail("truncated instance matrices");
                    return NULL;
                }
                matrix[k] = value;
            }
            ae3d_inst_set_matrix(loaded->instances, (int)i, matrix);
        }

        if (has_colors) {
            ae3d_inst_enable_colors(loaded->instances, (int)instance_count);
            for (i = 0; i < instance_count; i++) {
                float rgb[3];
                int k;
                for (k = 0; k < 3; k++) {
                    if (!ae3d_read_f32(file, &rgb[k])) {
                        gzclose(file);
                        ae3d_inst_destroy(loaded->instances);
                        ae3d_mesh_destroy(loaded->mesh);
                        free(loaded);
                        ae3d_meshfile_fail("truncated instance colours");
                        return NULL;
                    }
                }
                ae3d_inst_set_color(loaded->instances, (int)i, rgb[0], rgb[1], rgb[2]);
            }
        }
    }

    gzclose(file);
    g_meshfile_error[0] = '\0';
    return loaded;
}

void *ae3d_meshfile_mesh(void *handle) {
    ae3d_meshfile *loaded = (ae3d_meshfile *)handle;
    return loaded ? loaded->mesh : NULL;
}

void *ae3d_meshfile_instances(void *handle) {
    ae3d_meshfile *loaded = (ae3d_meshfile *)handle;
    return loaded ? loaded->instances : NULL;
}

// Releases the wrapper only. The mesh and instance buffers it carried belong to
// whoever took them, so freeing them here would pull them out from under the
// model that just adopted them.
void ae3d_meshfile_release(void *handle) {
    free(handle);
}
