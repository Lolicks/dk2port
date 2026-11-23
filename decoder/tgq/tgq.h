#ifndef DK2_TGQ_H
#define DK2_TGQ_H

#include "../core/reader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frame_count;
    uint32_t audio_count;
    uint32_t width, height;
    uint8_t quantizer;
    size_t first_frame_offset;
} Dk2TgqInfo;

Dk2Result dk2_tgq_parse(const uint8_t *data, size_t size, Dk2TgqInfo *info);

#ifdef __cplusplus
}
#endif
#endif
