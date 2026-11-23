#include "tgq.h"

#include <string.h>

Dk2Result dk2_tgq_parse(const uint8_t *d, size_t n, Dk2TgqInfo *info) {
    if (!d || !info || n < 8) return DK2_ERR_ARGS;
    memset(info, 0, sizeof(*info));

    size_t p = 0;
    while (p + 8 <= n) {
        uint32_t sz;
        dk2_u32le(d + p + 4, &sz);
        if (sz < 8 || sz > n - p) return DK2_ERR_CORRUPT;
        char tag[5];
        memcpy(tag, d + p, 4);
        tag[4] = 0;

        if (strcmp(tag, "pIQT") == 0) {
            if (sz < 16) return DK2_ERR_CORRUPT;
            uint16_t w, h;
            dk2_u16le(d + p + 8, &w);
            dk2_u16le(d + p + 10, &h);
            if (!info->frame_count) {
                info->width = w;
                info->height = h;
                info->quantizer = d[p + 12];
                info->first_frame_offset = p + 8;
            }
            ++info->frame_count;
        } else if (strcmp(tag, "SCDl") == 0 || strcmp(tag, "SCEl") == 0) {
            ++info->audio_count;
        }
        p += sz;
    }
    if (p != n) return DK2_ERR_CORRUPT;
    return DK2_OK;
}
