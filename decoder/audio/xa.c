#include "xa.h"

#include <stdint.h>

/*
 * EAAudioFrame.java:31-37:
 *   private static final int EA_ADPCM_TABLE[] = {
 *       0, 240, 460, 392,
 *       0, 0, -208, -220,
 *       0, 1, 3, 4,
 *       7, 8, 10, 11,
 *       0, -1, -3, -4
 *   };
 */
const int dk2_xa_adpcm_table[20] = {
    0, 240, 460, 392,
    0, 0, -208, -220,
    0, 1, 3, 4,
    7, 8, 10, 11,
    0, -1, -3, -4
};

int16_t dk2_u16_to_i16_local(uint16_t u) {
    if (u <= 0x7FFFu) return (int16_t)u;
    return (int16_t)(-(int32_t)(0xFFFFu - u) - 1);
}

/* Java `int` is 32-bit two's complement; keep that exact interpretation. */
static int32_t u32_to_i32(uint32_t u) {
    if (u <= 0x7FFFFFFFu) return (int32_t)u;
    return -(int32_t)(0xFFFFFFFFu - u) - 1;
}

/* Portable arithmetic right shift (Java's `>>` on int, EAAudioFrame.java:95,98,121). */
static int32_t sar32(int32_t v, unsigned n) {
    return v >= 0 ? (v >> n) : ~((~v) >> n);
}

/*
 * Java EAAudioFrame.signExtend(val, 4) (lines 118-122):
 *   shift = 32 - 4; s = val << shift; return s >> shift;
 * Only the low four bits of val survive, then they are sign-extended.
 */
static int32_t sx4(int32_t val) {
    int32_t v = val & 0xFu;
    return (v ^ 0x8) - 0x8;
}

/* Shift a possibly negative nibble without invoking C's UB on negative <<. */
static int32_t nibble_shift(int32_t nibble, unsigned shift) {
    return u32_to_i32((uint32_t)nibble << shift);
}

Dk2Result dk2_xa_decode_block(const uint8_t *block28, int16_t *pcm_out, int channels,
                              int32_t *predictor_left, int32_t *predictor_right) {
    if (!block28 || !pcm_out || !predictor_left || !predictor_right) return DK2_ERR_ARGS;
    /* EAAudioFrame.java:53-60: only EA_XA_ADPCM and exactly two channels are supported. */
    if (channels != 2) return DK2_ERR_UNSUPPORTED;

    int32_t cur_l = (int32_t)dk2_xa_predictor_current(*predictor_left);
    int32_t prv_l = (int32_t)dk2_xa_predictor_previous(*predictor_left);
    int32_t cur_r = (int32_t)dk2_xa_predictor_current(*predictor_right);
    int32_t prv_r = (int32_t)dk2_xa_predictor_previous(*predictor_right);

    size_t pos = 0;

    /* EAAudioFrame.java:78-82: one byte selects both channels' filter tap pairs. */
    uint8_t b = block28[pos++];
    unsigned idx_l = (unsigned)(b >> 4);
    unsigned idx_r = (unsigned)(b & 0x0F);
    int c1l = dk2_xa_adpcm_table[idx_l];
    int c2l = dk2_xa_adpcm_table[idx_l + 4];
    int c1r = dk2_xa_adpcm_table[idx_r];
    int c2r = dk2_xa_adpcm_table[idx_r + 4];

    /* EAAudioFrame.java:84-86: second byte carries the two shift counts. */
    b = block28[pos++];
    unsigned shift_l = 20u - (unsigned)(b >> 4);
    unsigned shift_r = 20u - (unsigned)(b & 0x0F);

    /* EAAudioFrame.java:88-106: 28 samples, two sign-extended nibbles per byte. */
    for (int i = 0; i < DK2_XA_GROUP_SAMPLES; ++i) {
        b = block28[pos++];

        int32_t next_l = nibble_shift(sx4((int32_t)(b >> 4)), shift_l); /* line 90 */
        int32_t next_r = nibble_shift(sx4((int32_t)b), shift_r);        /* line 91 */

        /* lines 93-98: predictor + +0x80 rounding + arithmetic >> 8 */
        next_l = sar32(next_l + cur_l * c1l + prv_l * c2l + 0x80, 8);
        next_r = sar32(next_r + cur_r * c1r + prv_r * c2r + 0x80, 8);

        int16_t out_l = dk2_u16_to_i16_local((uint16_t)(uint32_t)next_l);
        int16_t out_r = dk2_u16_to_i16_local((uint16_t)(uint32_t)next_r);

        /* lines 100-103: previous <- current, current <- (short)next */
        prv_l = cur_l;
        cur_l = (int32_t)out_l;
        prv_r = cur_r;
        cur_r = (int32_t)out_r;

        pcm_out[2 * i] = out_l;     /* line 104 */
        pcm_out[2 * i + 1] = out_r; /* line 105 */
    }

    *predictor_left = dk2_xa_predictor_pack((int16_t)cur_l, (int16_t)prv_l);
    *predictor_right = dk2_xa_predictor_pack((int16_t)cur_r, (int16_t)prv_r);
    return DK2_OK;
}

Dk2Result dk2_xa_read_block_header(const uint8_t *payload, size_t payload_size,
                                   int32_t *coded_samples,
                                   int32_t *predictor_left, int32_t *predictor_right) {
    if (!payload || !coded_samples || !predictor_left || !predictor_right) return DK2_ERR_ARGS;
    if (payload_size < DK2_XA_HEADER_BYTES) return DK2_ERR_ARGS;

    /* EAAudioFrame.java:63-64: buf.getInt() is LE (BufferedResourceReader.java:67)
     * and the count is then rounded down to a whole 28-sample group. */
    uint32_t raw = 0;
    dk2_u32le(payload, &raw);
    int32_t declared = u32_to_i32(raw);
    if (declared < 0) return DK2_ERR_CORRUPT;
    *coded_samples = declared - declared % DK2_XA_GROUP_SAMPLES;

    /* EAAudioFrame.java:66-69: four signed 16-bit LE predictors. */
    uint16_t cl, pl, cr, pr;
    dk2_u16le(payload + 4, &cl);
    dk2_u16le(payload + 6, &pl);
    dk2_u16le(payload + 8, &cr);
    dk2_u16le(payload + 10, &pr);
    *predictor_left = dk2_xa_predictor_pack(dk2_u16_to_i16_local(cl),
                                            dk2_u16_to_i16_local(pl));
    *predictor_right = dk2_xa_predictor_pack(dk2_u16_to_i16_local(cr),
                                             dk2_u16_to_i16_local(pr));

    /* Java loops over codedSamples/28 groups of 30 bytes (lines 77-89). */
    size_t groups = (size_t)(*coded_samples / DK2_XA_GROUP_SAMPLES);
    if (groups > (SIZE_MAX - DK2_XA_HEADER_BYTES) / DK2_XA_GROUP_BYTES) return DK2_ERR_SIZE;
    size_t need = DK2_XA_HEADER_BYTES + groups * DK2_XA_GROUP_BYTES;
    if (need > payload_size) return DK2_ERR_BOUNDS;
    return DK2_OK;
}
