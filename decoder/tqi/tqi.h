#ifndef DK2_TQI_H
#define DK2_TQI_H

#include "../core/reader.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One decoded TQI frame. The planes are zero-padded to coded (16-aligned)
 * dimensions, exactly like OpenKeeper's TgqFrame. Strides are in bytes:
 * stride[0] = coded_width, stride[1] = stride[2] = coded_width / 2.
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t coded_width;
    uint16_t coded_height;
    uint8_t quantizer;
    uint8_t extra[3];
    uint32_t frame_index;
    uint8_t *planes[3];
    size_t stride[3];
} Dk2TqiFrame;

void dk2_tqi_frame_free(Dk2TqiFrame *frame);

/*
 * Decode a single pIQT payload: the 8-byte frame header followed by the VLC
 * bitstream. `payload` points just past the IFF chunk header.
 */
Dk2Result dk2_tqi_decode_frame(const uint8_t *payload, size_t payload_size,
                               uint32_t frame_index, Dk2TqiFrame *out);

/* Nearest-neighbour 4:2:0 upsample + JPEG YCbCr coefficients -> packed RGB24. */
Dk2Result dk2_tqi_frame_to_rgb24(const Dk2TqiFrame *frame,
                                 uint8_t *rgb, size_t rgb_size);

/* Binary PPM (P6) of the visible width x height RGB image. */
Dk2Result dk2_tqi_frame_write_ppm(const Dk2TqiFrame *frame, FILE *fp);

/* Locate pIQT payloads in an EA IFF-style TGQ container. */
typedef struct {
    size_t offset;
    size_t size;
    uint32_t index;
} Dk2TqiChunkRef;

Dk2Result dk2_tqi_find_frames(const uint8_t *data, size_t size,
                              Dk2TqiChunkRef **out, size_t *count);
void dk2_tqi_free_refs(Dk2TqiChunkRef *refs);

#ifdef __cplusplus
}
#endif
#endif
