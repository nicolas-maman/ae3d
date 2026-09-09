#include "ae3d.h"
#include "ae3d_glapi.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *pixels;
    int width;
    int height;
    size_t capacity;
} ae3d_capture_buffer;

static ae3d_capture_buffer g_frame;
static ae3d_capture_buffer g_reference;

static int ae3d_capture_reserve(ae3d_capture_buffer *buffer, int width, int height) {
    size_t wanted = (size_t)width * (size_t)height * 4u;
    if (wanted == 0) return 0;
    if (wanted > buffer->capacity) {
        unsigned char *grown = (unsigned char *)realloc(buffer->pixels, wanted);
        if (!grown) return 0;
        buffer->pixels = grown;
        buffer->capacity = wanted;
    }
    buffer->width = width;
    buffer->height = height;
    return 1;
}

// Rows arrive bottom-up from GL; flipping here means every reader downstream
// works in the same coordinates the rest of the channel reports.
static void ae3d_capture_flip(ae3d_capture_buffer *buffer) {
    size_t stride = (size_t)buffer->width * 4u;
    unsigned char *scratch = (unsigned char *)malloc(stride);
    int top = 0;
    int bottom = buffer->height - 1;

    if (!scratch) return;
    while (top < bottom) {
        unsigned char *a = buffer->pixels + (size_t)top * stride;
        unsigned char *b = buffer->pixels + (size_t)bottom * stride;
        memcpy(scratch, a, stride);
        memcpy(a, b, stride);
        memcpy(b, scratch, stride);
        top++;
        bottom--;
    }
    free(scratch);
}

int ae3d_capture_frame(int width, int height) {
    if (!ae3d_capture_reserve(&g_frame, width, height)) return 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, g_frame.pixels);
    ae3d_capture_flip(&g_frame);
    return 1;
}

int ae3d_capture_width(void) { return g_frame.width; }
int ae3d_capture_height(void) { return g_frame.height; }

int ae3d_capture_pixel(int x, int y, double *out) {
    size_t offset;
    if (!g_frame.pixels || x < 0 || y < 0 || x >= g_frame.width || y >= g_frame.height) {
        return 0;
    }
    offset = ((size_t)y * (size_t)g_frame.width + (size_t)x) * 4u;
    out[0] = (double)g_frame.pixels[offset];
    out[1] = (double)g_frame.pixels[offset + 1];
    out[2] = (double)g_frame.pixels[offset + 2];
    out[3] = (double)g_frame.pixels[offset + 3];
    return 1;
}

static void ae3d_capture_clamp(int *x, int *y, int *width, int *height) {
    if (*x < 0) { *width += *x; *x = 0; }
    if (*y < 0) { *height += *y; *y = 0; }
    if (*x + *width > g_frame.width) *width = g_frame.width - *x;
    if (*y + *height > g_frame.height) *height = g_frame.height - *y;
    if (*width < 0) *width = 0;
    if (*height < 0) *height = 0;
}

// Summarised in C. A 1280x720 region is 3.7MB of pixels and about twenty bytes
// of answer, and the answer is what an agent is asking for.
int ae3d_capture_region(int x, int y, int width, int height,
                        int background, int tolerance, double *out) {
    long long sum[4] = {0, 0, 0, 0};
    long long covered = 0;
    long long total;
    int row, column;
    int br = (background >> 24) & 0xFF;
    int bg = (background >> 16) & 0xFF;
    int bb = (background >> 8) & 0xFF;

    if (!g_frame.pixels) return 0;
    ae3d_capture_clamp(&x, &y, &width, &height);
    total = (long long)width * (long long)height;
    if (total <= 0) return 0;

    for (row = 0; row < height; row++) {
        const unsigned char *line =
            g_frame.pixels + ((size_t)(y + row) * (size_t)g_frame.width + (size_t)x) * 4u;
        for (column = 0; column < width; column++) {
            const unsigned char *p = line + (size_t)column * 4u;
            int dr = (int)p[0] - br;
            int dg = (int)p[1] - bg;
            int db = (int)p[2] - bb;
            if (dr < 0) dr = -dr;
            if (dg < 0) dg = -dg;
            if (db < 0) db = -db;
            sum[0] += p[0];
            sum[1] += p[1];
            sum[2] += p[2];
            sum[3] += p[3];
            if (dr > tolerance || dg > tolerance || db > tolerance) covered++;
        }
    }

    out[0] = (double)sum[0] / (double)total / 255.0;
    out[1] = (double)sum[1] / (double)total / 255.0;
    out[2] = (double)sum[2] / (double)total / 255.0;
    out[3] = (double)sum[3] / (double)total / 255.0;
    out[4] = (double)covered / (double)total;
    out[5] = (double)total;
    return 1;
}

int ae3d_capture_hold_reference(void) {
    if (!g_frame.pixels) return 0;
    if (!ae3d_capture_reserve(&g_reference, g_frame.width, g_frame.height)) return 0;
    memcpy(g_reference.pixels, g_frame.pixels,
           (size_t)g_frame.width * (size_t)g_frame.height * 4u);
    return 1;
}

int ae3d_capture_diff(int tolerance, double *out) {
    long long changed = 0;
    long long total;
    long long worst = 0;
    size_t i;
    size_t count;

    if (!g_frame.pixels || !g_reference.pixels) return 0;
    if (g_frame.width != g_reference.width || g_frame.height != g_reference.height) return 0;

    total = (long long)g_frame.width * (long long)g_frame.height;
    count = (size_t)total * 4u;
    for (i = 0; i < count; i += 4) {
        int delta = 0;
        int channel;
        for (channel = 0; channel < 3; channel++) {
            int d = (int)g_frame.pixels[i + channel] - (int)g_reference.pixels[i + channel];
            if (d < 0) d = -d;
            if (d > delta) delta = d;
        }
        if (delta > tolerance) changed++;
        if (delta > worst) worst = delta;
    }

    out[0] = (double)changed;
    out[1] = (double)changed / (double)total;
    out[2] = (double)worst / 255.0;
    out[3] = (double)total;
    return 1;
}

void ae3d_capture_release(void) {
    free(g_frame.pixels);
    free(g_reference.pixels);
    memset(&g_frame, 0, sizeof(g_frame));
    memset(&g_reference, 0, sizeof(g_reference));
}

double ae3d_capture_slot(const double *block, int index) {
    if (!block || index < 0 || index >= 8) return 0.0;
    return block[index];
}

int ae3d_capture_adopt(const unsigned char *pixels, int width, int height) {
    if (!pixels) return 0;
    if (!ae3d_capture_reserve(&g_frame, width, height)) return 0;
    memcpy(g_frame.pixels, pixels, (size_t)width * (size_t)height * 4u);
    return 1;
}
