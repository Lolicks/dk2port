#include "wad.h"

#include <stdlib.h>
#include <string.h>

Dk2Result dk2_wad_parse(const uint8_t *d, size_t n, Dk2Wad *w) {
    if (!d || !w || n < 0x58) return DK2_ERR_ARGS;
    if (memcmp(d, "DWFB", 4) != 0) return DK2_ERR_MAGIC;
    uint32_t version;
    dk2_u32le(d + 4, &version);
    if (version != 2) return DK2_ERR_VERSION;

    dk2_u32le(d + 0x48, &w->count);
    dk2_u32le(d + 0x4C, &w->pool_offset);
    dk2_u32le(d + 0x50, &w->pool_size);
    if (w->count == 0 || w->count > DK2_WAD_MAX_ENTRIES) return DK2_ERR_SIZE;
    if (w->pool_offset > n || w->pool_size > n - w->pool_offset)
        return DK2_ERR_BOUNDS;
    if (0x58u + (size_t)w->count * 40u > n) return DK2_ERR_BOUNDS;

    w->entries = (Dk2WadEntry *)calloc(w->count, sizeof(Dk2WadEntry));
    if (!w->entries) return DK2_ERR_NOMEM;

    for (uint32_t i = 0; i < w->count; ++i) {
        const uint8_t *p = d + 0x58 + (size_t)i * 40u;
        Dk2WadEntry *e = &w->entries[i];
        dk2_u32le(p + 0x00, &e->hash_id);
        dk2_u32le(p + 0x04, &e->name_offset);
        dk2_u32le(p + 0x08, &e->name_length);
        dk2_u32le(p + 0x0C, &e->data_offset);
        dk2_u32le(p + 0x10, &e->data_size);
        dk2_u32le(p + 0x14, &e->flags);
        dk2_u32le(p + 0x18, &e->uncompressed_size);

        if (e->name_length == 0 || e->name_offset > n ||
            e->name_length > n - e->name_offset ||
            e->data_offset > n || e->data_size > n - e->data_offset) {
            dk2_wad_free(w);
            return DK2_ERR_BOUNDS;
        }
        size_t len = e->name_length;
        if (len > 0) len--; /* NUL */
        if (len >= sizeof(e->name)) len = sizeof(e->name) - 1;
        memcpy(e->name, d + e->name_offset, len);
        e->name[len] = '\0';
    }
    return DK2_OK;
}

void dk2_wad_free(Dk2Wad *w) {
    if (!w) return;
    free(w->entries);
    w->entries = NULL;
    w->count = 0;
}

Dk2Result dk2_wad_get(const uint8_t *d, size_t n, const Dk2WadEntry *e,
                      uint8_t **out, size_t *out_size) {
    if (!d || !e || !out || !out_size) return DK2_ERR_ARGS;
    if (e->data_offset > n || e->data_size > n - e->data_offset)
        return DK2_ERR_BOUNDS;
    uint8_t *p = (uint8_t *)malloc(e->data_size + 1u);
    if (!p) return DK2_ERR_NOMEM;
    memcpy(p, d + e->data_offset, e->data_size);
    p[e->data_size] = 0;
    *out = p;
    *out_size = e->data_size;
    return DK2_OK;
}
