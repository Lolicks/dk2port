#ifndef DK2_XA_H
#define DK2_XA_H

#include "../core/reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EA XA ADPCM, transcribed from the authoritative algorithm in
 * port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/EAAudioFrame.java
 * (decodeFrame, lines 52-109). See port/docs/format-research/XA_ADPCM_PORT.md
 * for the exact line-by-line mapping.
 */

#define DK2_XA_GROUP_BYTES 30   /* EAAudioFrame.java:65,77-89: 2 control bytes + 28 nibbles */
#define DK2_XA_GROUP_SAMPLES 28 /* EAAudioFrame.java:64,65,88: samples produced per group */
#define DK2_XA_HEADER_BYTES 12  /* EAAudioFrame.java:62-69: u32 count + 4 signed i16 predictors */

/* Filter coefficients: EAAudioFrame.java:31-37 (EA_ADPCM_TABLE). */
extern const int dk2_xa_adpcm_table[20];

/* Two's-complement uint16 -> int16 conversion without implementation-defined casts. */
int16_t dk2_u16_to_i16_local(uint16_t u);

/*
 * Predictor state. Java keeps four signed 16-bit predictors
 * (current/previous for left and right, EAAudioFrame.java:66-69,100-103).
 * Each int32_t below packs ONE channel's pair so the public block function
 * keeps the exact signature requested:
 *
 *   low 16 bits  = current predictor sample  (currentLeftSample / currentRightSample)
 *   high 16 bits = previous predictor sample (previousLeftSample / previousRightSample)
 *
 * Both halves are sign-extended int16 values, matching Java's `short` arithmetic
 * (a short is promoted to a signed int before use, EAAudioFrame.java:94-98).
 */
static inline int32_t dk2_xa_predictor_pack(int16_t current, int16_t previous) {
    return (int32_t)((uint32_t)(uint16_t)current |
                     ((uint32_t)(uint16_t)previous << 16));
}
static inline int16_t dk2_xa_predictor_current(int32_t state) {
    return dk2_u16_to_i16_local((uint16_t)((uint32_t)state & 0xFFFFu));
}
static inline int16_t dk2_xa_predictor_previous(int32_t state) {
    return dk2_u16_to_i16_local((uint16_t)(((uint32_t)state >> 16) & 0xFFFFu));
}

/*
 * Decode one 30-byte packed group into 28 interleaved stereo sample frames
 * (56 int16_t values, left/right/left/right...).
 *
 * block28   : 30 bytes (2 coefficient/shift bytes followed by 28 packed nibbles).
 * pcm_out   : at least DK2_XA_GROUP_SAMPLES * channels int16_t entries.
 * channels  : must be 2, exactly as EAAudioFrame.java:57-60 requires.
 * predictor_left/right : in/out packed predictor state (see above). Seed these
 *             from the block header with dk2_xa_read_block_header() for the
 *             first group, then pass them unchanged through subsequent groups.
 *
 * Returns DK2_OK, DK2_ERR_ARGS for invalid parameters/unsupported channel
 * count, or DK2_ERR_BOUNDS if a caller-supplied output buffer is impossible.
 */
Dk2Result dk2_xa_decode_block(const uint8_t *block28, int16_t *pcm_out, int channels,
                              int32_t *predictor_left, int32_t *predictor_right);

/*
 * Parse the 12-byte SCDl block header (EAAudioFrame.java:62-69):
 *   +0 u32 LE declared sample count (rounded DOWN to a multiple of 28, line 64)
 *   +4 i16 LE current left, +6 i16 LE previous left
 *   +8 i16 LE current right, +10 i16 LE previous right
 * Also verifies that the payload contains enough 30-byte groups for the
 * rounded-down count (Java would otherwise throw BufferUnderflowException).
 */
Dk2Result dk2_xa_read_block_header(const uint8_t *payload, size_t payload_size,
                                   int32_t *coded_samples,
                                   int32_t *predictor_left, int32_t *predictor_right);

#ifdef __cplusplus
}
#endif
#endif
