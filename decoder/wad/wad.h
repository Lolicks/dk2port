#ifndef DK2_WAD_H
#define DK2_WAD_H

#include "../core/reader.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DK2_WAD_MAX_ENTRIES 100000u

typedef struct {
    uint32_t hash_id;
    uint32_t name_offset, name_length;
    uint32_t data_offset, data_size;
    uint32_t flags, uncompressed_size;
    char name[256];
} Dk2WadEntry;

typedef struct {
    uint32_t count, pool_offset, pool_size;
    Dk2WadEntry *entries;
} Dk2Wad;

Dk2Result dk2_wad_parse(const uint8_t *data, size_t size, Dk2Wad *wad);
void dk2_wad_free(Dk2Wad *wad);
/* Returns a malloc'd copy of one entry's stored bytes. */
Dk2Result dk2_wad_get(const uint8_t *data, size_t size,
                      const Dk2WadEntry *e, uint8_t **out, size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif
