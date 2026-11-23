#ifndef DK2_READER_H
#define DK2_READER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DK2_OK = 0,
    DK2_ERR_ARGS = -1,
    DK2_ERR_IO = -2,
    DK2_ERR_MAGIC = -3,
    DK2_ERR_VERSION = -4,
    DK2_ERR_BOUNDS = -5,
    DK2_ERR_CORRUPT = -6,
    DK2_ERR_NOMEM = -7,
    DK2_ERR_SIZE = -8,
    DK2_ERR_UNSUPPORTED = -9
} Dk2Result;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} Dk2Reader;

static inline void dk2_reader_init(Dk2Reader *r, const uint8_t *data, size_t size) {
    r->data = data;
    r->size = size;
    r->pos = 0;
}

static inline int dk2_reader_left(const Dk2Reader *r) {
    return r->pos <= r->size ? (int)(r->size - r->pos) : -1;
}

static inline Dk2Result dk2_reader_seek(Dk2Reader *r, size_t pos) {
    if (pos > r->size) return DK2_ERR_BOUNDS;
    r->pos = pos;
    return DK2_OK;
}

static inline Dk2Result dk2_read_u8(Dk2Reader *r, uint8_t *out) {
    if (!r || !out || !r->data) return DK2_ERR_ARGS;
    if (r->pos >= r->size) return DK2_ERR_BOUNDS;
    *out = r->data[r->pos++];
    return DK2_OK;
}

static inline Dk2Result dk2_read_bytes(Dk2Reader *r, void *out, size_t n) {
    if (!r || (!out && n) || !r->data) return DK2_ERR_ARGS;
    if (n > r->size - r->pos) return DK2_ERR_BOUNDS;
    if (n) {
        unsigned char *dst = (unsigned char *)out;
        for (size_t i = 0; i < n; ++i) dst[i] = r->data[r->pos + i];
    }
    r->pos += n;
    return DK2_OK;
}

static inline Dk2Result dk2_u16le(const uint8_t *p, uint16_t *out) {
    if (!p || !out) return DK2_ERR_ARGS;
    *out = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    return DK2_OK;
}

static inline Dk2Result dk2_u16be(const uint8_t *p, uint16_t *out) {
    if (!p || !out) return DK2_ERR_ARGS;
    *out = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
    return DK2_OK;
}

static inline Dk2Result dk2_u32le(const uint8_t *p, uint32_t *out) {
    if (!p || !out) return DK2_ERR_ARGS;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return DK2_OK;
}

static inline Dk2Result dk2_read_u16le(Dk2Reader *r, uint16_t *out) {
    if (r->pos + 2 > r->size) return DK2_ERR_BOUNDS;
    Dk2Result rc = dk2_u16le(r->data + r->pos, out);
    if (rc == DK2_OK) r->pos += 2;
    return rc;
}

static inline Dk2Result dk2_read_u32le(Dk2Reader *r, uint32_t *out) {
    if (r->pos + 4 > r->size) return DK2_ERR_BOUNDS;
    Dk2Result rc = dk2_u32le(r->data + r->pos, out);
    if (rc == DK2_OK) r->pos += 4;
    return rc;
}

#endif
