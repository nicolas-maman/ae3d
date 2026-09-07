#include "ae3d.h"
#include "ae3d_internal.h"
#include "ae3d_glapi.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define AE3D_STRIDE_BYTES (8 * (int)sizeof(float))
#define AE3D_MATRIX_BYTES (16 * (int)sizeof(float))
#define AE3D_COLOR_BYTES  (3 * (int)sizeof(float))

typedef struct {
    float *values;
    int    count;
} ae3d_farr;

static char g_program_log[4096];
static char g_gl_version[128];
static char g_gl_renderer[128];
static int  g_loaded;

int ae3d_gl_load(void) {
    int missing;
    if (g_loaded) return 0;
    missing = ae3d_glapi_load();
    if (missing == 0) {
        const GLubyte *version = glGetString(GL_VERSION);
        const GLubyte *renderer = glGetString(GL_RENDERER);
        snprintf(g_gl_version, sizeof(g_gl_version), "%s", version ? (const char *)version : "");
        snprintf(g_gl_renderer, sizeof(g_gl_renderer), "%s", renderer ? (const char *)renderer : "");
        g_loaded = 1;
    }
    return missing;
}

const char *ae3d_gl_version(void) { return g_gl_version; }
const char *ae3d_gl_renderer(void) { return g_gl_renderer; }
int ae3d_gl_error(void) { return (int)glGetError(); }

void ae3d_gl_viewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }

void ae3d_gl_clear_color(double r, double g, double b, double a) {
    glClearColor((GLfloat)r, (GLfloat)g, (GLfloat)b, (GLfloat)a);
}

void ae3d_gl_clear(int color, int depth) {
    GLbitfield mask = 0;
    if (color) mask |= GL_COLOR_BUFFER_BIT;
    if (depth) mask |= GL_DEPTH_BUFFER_BIT;
    if (mask) glClear(mask);
}

void ae3d_gl_clear_depth(double d) { glClearDepth(d); }

void ae3d_gl_set_depth_test(int on) {
    if (on) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void ae3d_gl_set_depth_mask(int on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }

void ae3d_gl_set_face_culling(int on) {
    if (on) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void ae3d_gl_set_blend(int on) {
    if (on) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
}

void ae3d_gl_set_multisample(int on) {
    if (on) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
}

void ae3d_gl_set_wireframe(int on) {
    glPolygonMode(GL_FRONT_AND_BACK, on ? GL_LINE : GL_FILL);
}

int ae3d_gl_viewport_width(void) {
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    return viewport[2];
}

int ae3d_gl_viewport_height(void) {
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    return viewport[3];
}

int ae3d_gl_vao_create(void) {
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    return (int)vao;
}

void ae3d_gl_vao_bind(int vao) { glBindVertexArray((GLuint)vao); }

void ae3d_gl_vao_delete(int vao) {
    GLuint id = (GLuint)vao;
    if (id) glDeleteVertexArrays(1, &id);
}

int ae3d_gl_buffer_create(void) {
    GLuint buffer = 0;
    glGenBuffers(1, &buffer);
    return (int)buffer;
}

void ae3d_gl_buffer_delete(int buffer) {
    GLuint id = (GLuint)buffer;
    if (id) glDeleteBuffers(1, &id);
}

// Geometry that appears many times is uploaded once and drawn from one set of
// buffers, which is what lets the renderer merge those models into a single
// instanced draw. A cache entry keeps its own copy of the bytes it was built
// from, so a match is an exact comparison rather than a hash that could collide
// and hand back the wrong mesh.
typedef struct {
    float    *vertices;
    unsigned *indices;
    int       vertex_count;
    int       index_count;
    int       vao;
    int       vbo;
    int       ebo;
    int       instance_vbo;
    int       instance_capacity;
    int       refs;
} ae3d_gl_geometry;

static ae3d_gl_geometry *g_geometry;
static int g_geometry_count;
static int g_geometry_capacity;

static int ae3d_gl_geometry_matches(const ae3d_gl_geometry *entry, const float *vertices,
                                    const unsigned *indices, int vertex_count, int index_count) {
    if (entry->refs <= 0) return 0;
    if (entry->vertex_count != vertex_count || entry->index_count != index_count) return 0;
    if (memcmp(entry->vertices, vertices, (size_t)vertex_count * AE3D_STRIDE_BYTES) != 0) return 0;
    return memcmp(entry->indices, indices, (size_t)index_count * sizeof(unsigned)) == 0;
}

static ae3d_gl_geometry *ae3d_gl_geometry_slot(void) {
    int i;
    for (i = 0; i < g_geometry_count; i++) {
        if (g_geometry[i].refs <= 0 && g_geometry[i].vao == 0) return &g_geometry[i];
    }
    if (g_geometry_count == g_geometry_capacity) {
        int grown = g_geometry_capacity ? g_geometry_capacity * 2 : 32;
        ae3d_gl_geometry *moved = (ae3d_gl_geometry *)realloc(g_geometry,
                                                              (size_t)grown * sizeof(*moved));
        if (!moved) return NULL;
        memset(moved + g_geometry_capacity, 0,
               (size_t)(grown - g_geometry_capacity) * sizeof(*moved));
        g_geometry = moved;
        g_geometry_capacity = grown;
    }
    return &g_geometry[g_geometry_count++];
}

// Returns the VAO the caller should draw with, and reports whether the buffers
// were already there. A miss uploads and sets the vertex attributes up once.
int ae3d_gl_geometry_acquire(void *mesh) {
    const float *vertices = ae3d_mesh_vertex_data(mesh);
    const unsigned *indices = ae3d_mesh_index_data(mesh);
    int vertex_count = ae3d_mesh_vertex_count(mesh);
    int index_count = ae3d_mesh_index_count(mesh);
    ae3d_gl_geometry *entry;
    int i;

    if (!vertices || !indices || vertex_count <= 0 || index_count <= 0) return 0;

    for (i = 0; i < g_geometry_count; i++) {
        if (!ae3d_gl_geometry_matches(&g_geometry[i], vertices, indices,
                                      vertex_count, index_count)) {
            continue;
        }
        g_geometry[i].refs++;
        return g_geometry[i].vao;
    }

    entry = ae3d_gl_geometry_slot();
    if (!entry) return 0;

    entry->vertices = (float *)malloc((size_t)vertex_count * AE3D_STRIDE_BYTES);
    entry->indices = (unsigned *)malloc((size_t)index_count * sizeof(unsigned));
    if (!entry->vertices || !entry->indices) {
        free(entry->vertices);
        free(entry->indices);
        memset(entry, 0, sizeof(*entry));
        return 0;
    }
    memcpy(entry->vertices, vertices, (size_t)vertex_count * AE3D_STRIDE_BYTES);
    memcpy(entry->indices, indices, (size_t)index_count * sizeof(unsigned));
    entry->vertex_count = vertex_count;
    entry->index_count = index_count;

    entry->vao = ae3d_gl_vao_create();
    entry->vbo = ae3d_gl_buffer_create();
    entry->ebo = ae3d_gl_buffer_create();
    entry->instance_vbo = ae3d_gl_buffer_create();
    entry->instance_capacity = 0;
    entry->refs = 1;

    glBindVertexArray((GLuint)entry->vao);
    ae3d_gl_upload_mesh(mesh, entry->vbo, entry->ebo);
    ae3d_gl_setup_vertex_attribs();
    glBindVertexArray(0);

    return entry->vao;
}

int ae3d_gl_geometry_instance_vbo(int vao) {
    int i;
    for (i = 0; i < g_geometry_count; i++) {
        if (g_geometry[i].vao == vao && g_geometry[i].refs > 0) return g_geometry[i].instance_vbo;
    }
    return 0;
}

void ae3d_gl_geometry_release(int vao) {
    int i;
    for (i = 0; i < g_geometry_count; i++) {
        if (g_geometry[i].vao != vao || g_geometry[i].refs <= 0) continue;
        if (--g_geometry[i].refs > 0) return;
        ae3d_gl_vao_delete(g_geometry[i].vao);
        ae3d_gl_buffer_delete(g_geometry[i].vbo);
        ae3d_gl_buffer_delete(g_geometry[i].ebo);
        ae3d_gl_buffer_delete(g_geometry[i].instance_vbo);
        free(g_geometry[i].vertices);
        free(g_geometry[i].indices);
        memset(&g_geometry[i], 0, sizeof(g_geometry[i]));
        return;
    }
}

// Only when nothing is holding geometry any more, so a second renderer in the
// same process does not lose the buffers it is still drawing from.
void ae3d_gl_geometry_shutdown(void) {
    int i;
    for (i = 0; i < g_geometry_count; i++) {
        if (g_geometry[i].refs > 0) return;
    }
    free(g_geometry);
    g_geometry = NULL;
    g_geometry_count = 0;
    g_geometry_capacity = 0;
}

int ae3d_gl_geometry_instance_capacity(int vao) {
    int i;
    for (i = 0; i < g_geometry_count; i++) {
        if (g_geometry[i].vao == vao && g_geometry[i].refs > 0) {
            return g_geometry[i].instance_capacity;
        }
    }
    return 0;
}

void ae3d_gl_geometry_set_instance_capacity(int vao, int capacity) {
    int i;
    for (i = 0; i < g_geometry_count; i++) {
        if (g_geometry[i].vao == vao && g_geometry[i].refs > 0) {
            g_geometry[i].instance_capacity = capacity;
            return;
        }
    }
}

void ae3d_gl_upload_mesh(void *mesh, int vbo, int ebo) {
    const float *vertices = ae3d_mesh_vertex_data(mesh);
    const unsigned *indices = ae3d_mesh_index_data(mesh);
    int vertex_count = ae3d_mesh_vertex_count(mesh);
    int index_count = ae3d_mesh_index_count(mesh);

    if (vbo && vertices && vertex_count > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)vertex_count * AE3D_STRIDE_BYTES,
                     vertices, GL_STATIC_DRAW);
    }
    if (ebo && indices && index_count > 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     (GLsizeiptr)index_count * (GLsizeiptr)sizeof(unsigned),
                     indices, GL_STATIC_DRAW);
    }
}

void ae3d_gl_update_mesh_vertices(void *mesh, int vbo) {
    const float *vertices = ae3d_mesh_vertex_data(mesh);
    int vertex_count = ae3d_mesh_vertex_count(mesh);
    if (!vbo || !vertices || vertex_count <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)vertex_count * AE3D_STRIDE_BYTES, vertices);
}

void ae3d_gl_setup_vertex_attribs(void) {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, AE3D_STRIDE_BYTES, (const void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, AE3D_STRIDE_BYTES, (const void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, AE3D_STRIDE_BYTES, (const void *)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
}

void ae3d_gl_setup_instance_attribs(void *inst, int matrix_vbo, int color_vbo) {
    const float *matrices = ae3d_inst_matrix_data(inst);
    const float *colors = ae3d_inst_color_data(inst);
    int count = ae3d_inst_count(inst);
    int i;

    if (count <= 0 || !matrix_vbo || !matrices) return;

    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)matrix_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * AE3D_MATRIX_BYTES,
                 matrices, GL_DYNAMIC_DRAW);
    for (i = 0; i < 4; i++) {
        glEnableVertexAttribArray((GLuint)(3 + i));
        glVertexAttribPointer((GLuint)(3 + i), 4, GL_FLOAT, GL_FALSE, AE3D_MATRIX_BYTES,
                              (const void *)(size_t)(i * 4 * sizeof(float)));
        glVertexAttribDivisor((GLuint)(3 + i), 1);
    }

    if (color_vbo && colors && ae3d_inst_has_colors(inst)) {
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)color_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * AE3D_COLOR_BYTES,
                     colors, GL_STATIC_DRAW);
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, AE3D_COLOR_BYTES, (const void *)0);
        glVertexAttribDivisor(7, 1);
    }
}

// A batch draws through the shared VAO with the matrices of the models it
// merged. The colour attribute stays disabled, so the generic value set at
// startup supplies white and the shader's per-instance tint is a no-op.
int ae3d_gl_batch_upload(int vao, int instance_vbo, void *inst, int capacity_bytes) {
    const float *matrices = ae3d_inst_matrix_data(inst);
    int count = ae3d_inst_count(inst);
    GLsizeiptr wanted;
    int i;

    if (!vao || !instance_vbo || count <= 0 || !matrices) return capacity_bytes;

    glBindVertexArray((GLuint)vao);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)instance_vbo);

    wanted = (GLsizeiptr)count * AE3D_MATRIX_BYTES;
    if ((GLsizeiptr)capacity_bytes < wanted) {
        glBufferData(GL_ARRAY_BUFFER, wanted, matrices, GL_DYNAMIC_DRAW);
        capacity_bytes = (int)wanted;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, wanted, matrices);
    }

    for (i = 0; i < 4; i++) {
        glEnableVertexAttribArray((GLuint)(3 + i));
        glVertexAttribPointer((GLuint)(3 + i), 4, GL_FLOAT, GL_FALSE, AE3D_MATRIX_BYTES,
                              (const void *)(size_t)(i * 4 * sizeof(float)));
        glVertexAttribDivisor((GLuint)(3 + i), 1);
    }
    // Set with the batch's own vertex array bound, so it holds whether the
    // driver treats a generic attribute value as context or per-array state.
    glDisableVertexAttribArray(7);
    glVertexAttrib3f(7, 1.0f, 1.0f, 1.0f);
    return capacity_bytes;
}

// Generic attribute values are context state, so one call covers every VAO that
// leaves the per-instance colour array disabled.
void ae3d_gl_set_default_instance_color(void) {
    glVertexAttrib3f(7, 1.0f, 1.0f, 1.0f);
}

int ae3d_gl_update_instances(void *inst, int matrix_vbo, int capacity_bytes) {
    const float *matrices = ae3d_inst_matrix_data(inst);
    int count = ae3d_inst_count(inst);
    int needed;

    if (count <= 0 || !matrix_vbo || !matrices) return capacity_bytes;
    needed = count * AE3D_MATRIX_BYTES;

    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)matrix_vbo);
    if (needed > capacity_bytes) {
        int grown = needed + needed / 2;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)grown, NULL, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)needed, matrices);
        return grown;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)needed, matrices);
    return capacity_bytes;
}

void ae3d_gl_draw_elements(int count, int byte_offset) {
    glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT,
                   (const void *)(size_t)byte_offset);
}

void ae3d_gl_draw_elements_instanced(int count, int byte_offset, int instances) {
    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT,
                            (const void *)(size_t)byte_offset, (GLsizei)instances);
}

void ae3d_gl_draw_arrays(int first, int count) {
    glDrawArrays(GL_TRIANGLES, first, (GLsizei)count);
}

static GLuint ae3d_gl_compile(GLenum type, const char *source, const char *label) {
    GLuint shader = glCreateShader(type);
    GLint status = GL_FALSE;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        if (length > (GLint)sizeof(g_program_log) - 64) length = (GLint)sizeof(g_program_log) - 64;
        snprintf(g_program_log, sizeof(g_program_log), "%s shader: ", label);
        if (length > 0) {
            size_t used = strlen(g_program_log);
            glGetShaderInfoLog(shader, length, NULL, g_program_log + used);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int ae3d_gl_program_create(const char *vertex_src, const char *fragment_src) {
    GLuint vertex, fragment, program;
    GLint status = GL_FALSE;

    g_program_log[0] = '\0';
    if (!vertex_src || !fragment_src) {
        snprintf(g_program_log, sizeof(g_program_log), "missing shader source");
        return 0;
    }

    vertex = ae3d_gl_compile(GL_VERTEX_SHADER, vertex_src, "vertex");
    if (!vertex) return 0;
    fragment = ae3d_gl_compile(GL_FRAGMENT_SHADER, fragment_src, "fragment");
    if (!fragment) {
        glDeleteShader(vertex);
        return 0;
    }

    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);

    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    if (status == GL_FALSE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        if (length > (GLint)sizeof(g_program_log) - 8) length = (GLint)sizeof(g_program_log) - 8;
        snprintf(g_program_log, sizeof(g_program_log), "link: ");
        if (length > 0) {
            size_t used = strlen(g_program_log);
            glGetProgramInfoLog(program, length, NULL, g_program_log + used);
        }
        glDeleteProgram(program);
        return 0;
    }
    return (int)program;
}

void ae3d_gl_program_delete(int program) {
    if (program) glDeleteProgram((GLuint)program);
}

void ae3d_gl_program_use(int program) { glUseProgram((GLuint)program); }

const char *ae3d_gl_program_log(void) { return g_program_log; }

int ae3d_gl_uniform_location(int program, const char *name) {
    if (!program || !name) return -1;
    return glGetUniformLocation((GLuint)program, name);
}

void ae3d_gl_uniform_int(int loc, int v) { if (loc >= 0) glUniform1i(loc, v); }
void ae3d_gl_uniform_float(int loc, double v) { if (loc >= 0) glUniform1f(loc, (GLfloat)v); }

void ae3d_gl_uniform_vec2(int loc, double x, double y) {
    if (loc >= 0) glUniform2f(loc, (GLfloat)x, (GLfloat)y);
}

void ae3d_gl_uniform_vec3(int loc, double x, double y, double z) {
    if (loc >= 0) glUniform3f(loc, (GLfloat)x, (GLfloat)y, (GLfloat)z);
}

void ae3d_gl_uniform_vec4(int loc, double x, double y, double z, double w) {
    if (loc >= 0) glUniform4f(loc, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);
}

void ae3d_gl_uniform_mat4(int loc, const double *m) {
    GLfloat values[16];
    int i;
    if (loc < 0 || !m) return;
    for (i = 0; i < 16; i++) values[i] = (GLfloat)m[i];
    glUniformMatrix4fv(loc, 1, GL_FALSE, values);
}

void *ae3d_farr_create(int count) {
    ae3d_farr *arr;
    if (count <= 0) return NULL;
    arr = (ae3d_farr *)calloc(1, sizeof(ae3d_farr));
    if (!arr) return NULL;
    arr->values = (float *)calloc((size_t)count, sizeof(float));
    if (!arr->values) { free(arr); return NULL; }
    arr->count = count;
    return arr;
}

void ae3d_farr_destroy(void *handle) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (!arr) return;
    free(arr->values);
    free(arr);
}

void ae3d_farr_set(void *handle, int i, double v) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (!arr || i < 0 || i >= arr->count) return;
    arr->values[i] = (float)v;
}

double ae3d_farr_get(void *handle, int i) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (!arr || i < 0 || i >= arr->count) return 0.0;
    return arr->values[i];
}

int ae3d_farr_count(void *handle) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    return arr ? arr->count : 0;
}

void ae3d_gl_uniform_float_array(int loc, void *handle) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (loc < 0 || !arr) return;
    glUniform1fv(loc, (GLsizei)arr->count, arr->values);
}

void ae3d_gl_uniform_vec3_array(int loc, void *handle) {
    ae3d_farr *arr = (ae3d_farr *)handle;
    if (loc < 0 || !arr) return;
    glUniform3fv(loc, (GLsizei)(arr->count / 3), arr->values);
}

int ae3d_gl_texture_from_image(void *img, int srgb, int mipmap) {
    const unsigned char *pixels = ae3d_image_pixels(img);
    int width = ae3d_image_width(img);
    int height = ae3d_image_height(img);
    GLuint texture = 0;

    if (!pixels || width <= 0 || height <= 0) return 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA,
                 width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mipmap) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return (int)texture;
}

int ae3d_gl_texture_cubemap_from_image(void *img) {
    const unsigned char *pixels = ae3d_image_pixels(img);
    int width = ae3d_image_width(img);
    int height = ae3d_image_height(img);
    GLuint texture = 0;
    int face;

    if (!pixels || width <= 0 || height <= 0) return 0;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (face = 0; face < 6; face++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face, 0, GL_RGBA,
                     width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return (int)texture;
}

void ae3d_gl_texture_delete(int texture) {
    GLuint id = (GLuint)texture;
    if (id) glDeleteTextures(1, &id);
}

void ae3d_gl_texture_bind(int unit, int texture) {
    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_2D, (GLuint)texture);
}

void ae3d_gl_texture_bind_cubemap(int unit, int texture) {
    glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, (GLuint)texture);
}

int ae3d_gl_fbo_create(void) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    return (int)fbo;
}

void ae3d_gl_fbo_bind(int fbo) { glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo); }

void ae3d_gl_fbo_delete(int fbo) {
    GLuint id = (GLuint)fbo;
    if (id) glDeleteFramebuffers(1, &id);
}

int ae3d_gl_fbo_attach_color(int fbo, int width, int height, int hdr) {
    GLuint texture = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA, width, height, 0,
                 GL_RGBA, hdr ? GL_FLOAT : GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    return (int)texture;
}

// A shadow map wants different sampling from a post-processing colour buffer:
// point sampling, because the comparison is done per texel with explicit
// offsets, and an edge clamp so a fragment projecting outside the light's view
// reads the cleared far value rather than wrapping.
int ae3d_gl_fbo_attach_shadow_map(int fbo, int size) {
    GLuint texture = 0;
    if (size < 1) size = 1;

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return (int)texture;
}

// The scene pass renders into multisampled attachments and resolves down into
// the single-sample texture the composite samples. Without it the one path that
// runs an effect is also the one path with no antialiasing.
int ae3d_gl_max_samples(void) {
    GLint most = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &most);
    if (most > 4) most = 4;
    return most > 1 ? (int)most : 0;
}

int ae3d_gl_fbo_attach_color_multisample(int fbo, int width, int height, int samples) {
    GLuint rbo = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    return (int)rbo;
}

int ae3d_gl_fbo_attach_depth_multisample(int fbo, int width, int height, int samples) {
    GLuint rbo = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
    return (int)rbo;
}

void ae3d_gl_fbo_resolve(int source, int destination, int width, int height) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)destination);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

int ae3d_gl_fbo_attach_depth(int fbo, int width, int height) {
    GLuint rbo = 0;
    if (width < 1) width = 1;
    if (height < 1) height = 1;

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
    return (int)rbo;
}

void ae3d_gl_renderbuffer_delete(int rbo) {
    GLuint id = (GLuint)rbo;
    if (id) glDeleteRenderbuffers(1, &id);
}

// Packed 0xRRGGBB of one framebuffer pixel, for tests that must prove the scene
// was drawn rather than merely cleared.
int ae3d_gl_read_pixel(int x, int y) {
    unsigned char rgba[4] = {0, 0, 0, 0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return ((int)rgba[0] << 16) | ((int)rgba[1] << 8) | (int)rgba[2];
}

int ae3d_gl_fbo_complete(void) {
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}
