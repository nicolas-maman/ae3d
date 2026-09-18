/* A file as bytes, and the little-endian numbers in it.
 *
 * Aether's string is a C string, so a file read through it stops at the first
 * zero byte; a glTF buffer is nothing but zero bytes. This reads a whole file
 * into memory the caller owns and decodes the numbers a glTF accessor is made
 * of -- float32, uint8/16/32 -- at a byte offset, little-endian as glTF
 * requires whatever the machine is. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long ae3d_blob_size_last = 0;

/* The size of the last file ae3d_blob_read read, for a caller that has no
   out-parameter to hand it. */
long ae3d_blob_last_size(void) { return ae3d_blob_size_last; }

void *ae3d_blob_read(const char *path) {
    FILE *f;
    long size;
    unsigned char *bytes;
    ae3d_blob_size_last = 0;
    if (!path) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);
    bytes = (unsigned char *)malloc((size_t)size + 1);
    if (!bytes) { fclose(f); return NULL; }
    if (size > 0 && fread(bytes, 1, (size_t)size, f) != (size_t)size) {
        free(bytes);
        fclose(f);
        return NULL;
    }
    bytes[size] = 0;
    fclose(f);
    ae3d_blob_size_last = size;
    return bytes;
}

void ae3d_blob_free(void *blob) { free(blob); }

/* The bytes from `offset` on, as a pointer into the same allocation: a
   .glb's binary chunk inside the file's bytes. */
void *ae3d_blob_slice(void *blob, long offset) { return (unsigned char *)blob + offset; }

static unsigned ae3d_blob_le32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

double ae3d_blob_f32(const void *blob, long offset) {
    unsigned bits = ae3d_blob_le32((const unsigned char *)blob + offset);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return (double)value;
}

int ae3d_blob_u32(const void *blob, long offset) {
    return (int)ae3d_blob_le32((const unsigned char *)blob + offset);
}

int ae3d_blob_u16(const void *blob, long offset) {
    const unsigned char *p = (const unsigned char *)blob + offset;
    return (int)p[0] | ((int)p[1] << 8);
}

int ae3d_blob_u8(const void *blob, long offset) {
    return (int)((const unsigned char *)blob)[offset];
}

int ae3d_blob_i16(const void *blob, long offset) {
    int v = ae3d_blob_u16(blob, offset);
    return v >= 32768 ? v - 65536 : v;
}

int ae3d_blob_i8(const void *blob, long offset) {
    int v = ae3d_blob_u8(blob, offset);
    return v >= 128 ? v - 256 : v;
}

/* `length` bytes from `offset` as a C string the caller owns, returned to
   ae3d_blob_text_free: the JSON chunk of a .glb, which is not
   zero-terminated where it sits. */
char *ae3d_blob_text(const void *blob, long offset, long length) {
    char *text;
    if (length < 0) return NULL;
    text = (char *)malloc((size_t)length + 1);
    if (!text) return NULL;
    memcpy(text, (const unsigned char *)blob + offset, (size_t)length);
    text[length] = 0;
    return text;
}

void ae3d_blob_text_free(char *text) { free(text); }
