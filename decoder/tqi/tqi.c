/*
 * TQI (Texture Quantized Image) intra-frame video decoder.
 *
 * Algorithm reference (read, not copied verbatim):
 *   port/research/OpenKeeper/src/main/java/toniarts/openkeeper/video/tgq/TgqFrame.java
 *   port/research/OpenKeeper/src/main/java/toniarts/openkeeper/tools/convert/BitReader.java
 * Encoding notes and Ghidra cross-checks:
 *   port/docs/format-research/TQI_TGQ_FORMAT.md
 *
 * This implements the OpenKeeper intra path only: it intentionally has no
 * motion compensation, because OpenKeeper has none. See TQI_C_PORT.md.
 */
#include "tqi.h"

#include "tqi_tables.h"

#include <stdlib.h>
#include <string.h>

#define TQI_DC_VLC_BITS 9
#define TQI_TEX_VLC_BITS 28
#define TQI_EOB ((int16_t)-5)
#define TQI_DCT_ESCAPE ((int16_t)-2)
#define TQI_MAX_DIM 8192u

/* ------------------------------------------------------------------ */
/* BitReader: faithful port of OpenKeeper's little-endian BitReader.   */
/* 32-bit LE words are loaded into `cur`; bits are consumed MSB first. */
/* ------------------------------------------------------------------ */

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t cur;
    int deficit;
    int overrun;
} BitReader;

static uint32_t bswap32(uint32_t v) {
    return ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
           ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
}

static uint32_t br_read_int(BitReader *b) {
    size_t remaining = b->pos <= b->size ? b->size - b->pos : 0;
    if (remaining >= 4) {
        uint32_t b1 = b->data[b->pos];
        uint32_t b2 = b->data[b->pos + 1];
        uint32_t b3 = b->data[b->pos + 2];
        uint32_t b4 = b->data[b->pos + 3];
        b->pos += 4;
        b->deficit -= 32;
        return (b4 << 24) | (b3 << 16) | (b2 << 8) | b1;
    }
    /* Java readIntSafe(): zero-pad a short tail, assemble BE, reverse bytes. */
    b->deficit -= (int)(remaining * 8);
    uint32_t res = 0;
    for (int i = 0; i < 4; ++i) {
        res <<= 8;
        if (b->pos < b->size) res |= b->data[b->pos++];
    }
    if (b->pos > b->size) b->overrun = 1;
    return bswap32(res);
}

static void br_init(BitReader *b, const uint8_t *data, size_t size) {
    b->data = data;
    b->size = size;
    b->pos = 0;
    b->deficit = 0;
    b->overrun = 0;
    b->cur = br_read_int(b);
    b->deficit = 0; /* the Java constructor does this after readInt() */
}

static uint32_t br_read_nbit(BitReader *b, int n) {
    uint32_t ret = 0;
    if (n + b->deficit > 31) {
        ret |= b->cur >> (b->deficit & 31);
        n -= 32 - b->deficit;
        ret <<= n;
        b->deficit = 32;
        b->cur = br_read_int(b);
    }
    if (n != 0) {
        ret |= b->cur >> (32 - n);
        b->cur <<= (uint32_t)n;
        b->deficit += n;
    }
    if (b->pos > b->size) b->overrun = 1;
    return ret;
}

static int br_read_1bit(BitReader *b) {
    int ret = (int)(b->cur >> 31);
    b->cur <<= 1;
    ++b->deficit;
    if (b->deficit == 32) b->cur = br_read_int(b);
    return ret;
}

static void br_skip(BitReader *b, int bits) {
    int left = bits;
    if (left + b->deficit > 31) {
        left -= 32 - b->deficit;
        b->deficit = 32;
        if (left > 31) {
            size_t remaining = b->pos <= b->size ? b->size - b->pos : 0;
            size_t skip = (size_t)(left >> 3);
            if (skip > remaining) skip = remaining;
            b->pos += skip;
            left -= (int)(skip << 3);
        }
        b->cur = br_read_int(b);
    }
    b->deficit += left;
    b->cur <<= (uint32_t)left;
    if (b->pos > b->size) b->overrun = 1;
}

/* ------------------------------------------------------------------ */
/* Signed-value helpers (TgqFrame.java:295-309).                       */
/* ------------------------------------------------------------------ */

static int32_t sext(uint32_t v, int bits) {
    if (bits >= 32) return (int32_t)v;
    uint32_t sign = 1u << (bits - 1);
    return (v & sign) ? (int32_t)(v - (sign << 1)) : (int32_t)v;
}

static int32_t mpeg_signed(BitReader *b, int size) {
    uint32_t val = br_read_nbit(b, size);
    uint32_t top = 1u << (size - 1);
    /* Java: sign = (val >>> (size-1)) ^ 1; return val + sign - (sign << size) */
    return (val & top) ? (int32_t)val : (int32_t)(val - (2u * top - 1u));
}

static int32_t to_signed(int32_t val, int s) {
    return s ? -val : val;
}

/* ------------------------------------------------------------------ */
/* VLC tree walk (TgqFrame.java:311-329).                              */
/* `tab` is the flat backing array; DC rows have 3 columns, AC rows 4. */
/* Returns the leaf's columns 2 and 3 (column 3 is 0 for DC tables).    */
/*                                                                      */
/* The Java loop starts readLength at -1 and increments once per        */
/* examined bit. A leaf has both children -1, so the last examined bit   */
/* is the first bit AFTER the code; the number of bits to consume is     */
/* therefore (iterations - 1).                                          */
/* ------------------------------------------------------------------ */

static Dk2Result decode_vlc(BitReader *b, int max_length,
                            const int16_t *tab, int stride, int rows,
                            int *out_a, int *out_b) {
    if (stride < 3 || rows <= 0 || max_length < 0 || max_length >= 31)
        return DK2_ERR_ARGS;

    BitReader probe = *b;
    uint32_t bits = br_read_nbit(&probe, max_length + 1);
    uint32_t mask = 1u << (max_length + 1);
    int idx = 0;
    int prev = 0;
    int len = -1;

    while (idx != -1) {
        if (idx < 0 || idx >= rows) return DK2_ERR_CORRUPT;
        mask >>= 1;
        prev = idx;
        idx = tab[(size_t)idx * stride + ((bits & mask) ? 1 : 0)];
        ++len;
        if (len > max_length + 1) return DK2_ERR_CORRUPT;
    }

    br_skip(b, len);
    if (b->pos > b->size) return DK2_ERR_BOUNDS;
    *out_a = tab[(size_t)prev * stride + 2];
    *out_b = stride >= 4 ? tab[(size_t)prev * stride + 3] : 0;
    return DK2_OK;
}

/* ------------------------------------------------------------------ */
/* Frame decoder context.                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    BitReader br;
    int32_t dequant[64];
    int32_t last_dc[3];
} TqiCtx;

static void calculate_dequant(int32_t dequant[64], uint8_t quantizer) {
    int64_t qscale = (int64_t)(215 - 2 * (int)quantizer) * 5;
    dequant[0] = (int32_t)(((int64_t)TQI_BASE_TABLE[0] * TQI_BASE_TABLE2[0]) >> 11);
    for (int i = 1; i < 64; ++i) {
        int64_t v = ((int64_t)TQI_BASE_TABLE[i] * TQI_BASE_TABLE2[i] * qscale + 32) >> 14;
        dequant[i] = (int32_t)v;
    }
}

static Dk2Result decode_dc(TqiCtx *c, int component, int32_t *diff_out) {
    int a, b;
    Dk2Result rc;
    if (component == 0) {
        rc = decode_vlc(&c->br, TQI_DC_VLC_BITS, &TQI_DC_LUMINANCE_VLC[0][0],
                        3, 19, &a, &b);
    } else {
        rc = decode_vlc(&c->br, TQI_DC_VLC_BITS, &TQI_DC_CHROMINANCE_VLC[0][0],
                        3, 19, &a, &b);
    }
    if (rc != DK2_OK) return rc;
    if (a < 0) return DK2_ERR_CORRUPT;
    *diff_out = (a == 0) ? 0 : mpeg_signed(&c->br, a);
    return DK2_OK;
}

static Dk2Result mpeg1_decode_block(TqiCtx *c, int32_t block[64], int component_index) {
    int component = component_index <= 3 ? 0 : component_index - 4 + 1;
    if (component < 0 || component > 2) return DK2_ERR_ARGS;

    int32_t diff;
    Dk2Result rc = decode_dc(c, component, &diff);
    if (rc != DK2_OK) return rc;
    if (diff >= 0xFFFF) return DK2_ERR_CORRUPT;

    int32_t dc = c->last_dc[component] + diff;
    c->last_dc[component] = dc;
    block[0] = dc * c->dequant[0];

    int a = 0, b = 0;
    int i = 0;
    rc = decode_vlc(&c->br, TQI_TEX_VLC_BITS, &TQI_DCT_COEFF[0][0], 4, 227, &a, &b);
    if (rc != DK2_OK) return rc;

    while (b != TQI_EOB) {
        int32_t level;
        int j;
        if (b == TQI_DCT_ESCAPE) {
            int run = (int)br_read_nbit(&c->br, 6) + 1;
            level = sext(br_read_nbit(&c->br, 8), 8);
            if (level == -128) {
                level = (int32_t)br_read_nbit(&c->br, 8) - 256;
            } else if (level == 0) {
                level = (int32_t)br_read_nbit(&c->br, 8);
            }
            i += run;
            if (i < 0 || i >= 64) return DK2_ERR_CORRUPT;
            j = TQI_SCAN_TABLE[i];
            if (j < 0 || j >= 64) return DK2_ERR_CORRUPT;
            if (level < 0) {
                level = -level;
                level = (level * c->dequant[j]) >> 4;
                level = (level - 1) | 1;
                level = -level;
            } else {
                level = (level * c->dequant[j]) >> 4;
                level = (level - 1) | 1;
            }
        } else {
            i += a + 1;
            if (i < 0 || i >= 64) return DK2_ERR_CORRUPT;
            j = TQI_SCAN_TABLE[i];
            if (j < 0 || j >= 64) return DK2_ERR_CORRUPT;
            level = b * c->dequant[j];
            level >>= 4;
            level = (level - 1) | 1;
            level = to_signed(level, br_read_1bit(&c->br));
        }
        block[j] = level;
        rc = decode_vlc(&c->br, TQI_TEX_VLC_BITS, &TQI_DCT_COEFF[0][0], 4, 227, &a, &b);
        if (rc != DK2_OK) return rc;
    }
    return DK2_OK;
}

/* ------------------------------------------------------------------ */
/* Integer IDCT (TgqFrame.java:352-419).                               */
/* ------------------------------------------------------------------ */

#define TQI_ASQRT 181
#define TQI_A4 669
#define TQI_A2 277
#define TQI_A5 196

static const int TQI_COL_OFF[8] = {0, 8, 16, 24, 32, 40, 48, 56};
static const int TQI_ROW_OFF[8] = {0, 1, 2, 3, 4, 5, 6, 7};

static void idct_core(int32_t out[8], const int32_t *src, const int *s) {
    int a1 = src[s[1]] + src[s[7]];
    int a7 = src[s[1]] - src[s[7]];
    int a5 = src[s[5]] + src[s[3]];
    int a3 = src[s[5]] - src[s[3]];
    int a2 = src[s[2]] + src[s[6]];
    int a6 = (TQI_ASQRT * (src[s[2]] - src[s[6]])) >> 8;
    int a0 = src[s[0]] + src[s[4]];
    int a4 = src[s[0]] - src[s[4]];
    int b0 = (((TQI_A4 - TQI_A5) * a7 - TQI_A5 * a3) >> 9) + a1 + a5;
    int b1 = (((TQI_A4 - TQI_A5) * a7 - TQI_A5 * a3) >> 9) +
             ((TQI_ASQRT * (a1 - a5)) >> 8);
    int b2 = (((TQI_A2 + TQI_A5) * a3 + TQI_A5 * a7) >> 9) +
             ((TQI_ASQRT * (a1 - a5)) >> 8);
    int b3 = ((TQI_A2 + TQI_A5) * a3 + TQI_A5 * a7) >> 9;

    out[0] = a0 + a2 + a6 + b0;
    out[1] = a4 + a6 + b1;
    out[2] = a4 - a6 + b2;
    out[3] = a0 - a2 - a6 + b3;
    out[4] = a0 - a2 - a6 - b3;
    out[5] = a4 - a6 - b2;
    out[6] = a4 + a6 - b1;
    out[7] = a0 + a2 + a6 - b0;
}

static uint8_t clip_byte(int val) {
    int v = val >> 4;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

static void ea_idct_col(int32_t temp[64], const int32_t *block, int i) {
    if ((block[i + 8] | block[i + 16] | block[i + 24] | block[i + 32] |
         block[i + 40] | block[i + 48] | block[i + 56]) == 0) {
        for (int k = 0; k < 8; ++k)
            temp[i + TQI_COL_OFF[k]] = block[i];
    } else {
        int32_t out[8];
        idct_core(out, block + i, TQI_COL_OFF);
        for (int k = 0; k < 8; ++k)
            temp[i + TQI_COL_OFF[k]] = out[k];
    }
}

static void ea_idct_put(uint8_t *dest, size_t stride, const int32_t *block_in) {
    int32_t block[64];
    int32_t temp[64];
    memcpy(block, block_in, sizeof(block));
    block[0] += 4;

    for (int i = 0; i < 8; ++i) ea_idct_col(temp, block, i);

    for (int i = 0; i < 8; ++i) {
        int32_t out[8];
        idct_core(out, temp + 8 * i, TQI_ROW_OFF);
        uint8_t *row = dest + (size_t)i * stride;
        for (int k = 0; k < 8; ++k) row[k] = clip_byte(out[k]);
    }
}

static Dk2Result idct_put(Dk2TqiFrame *f, const int32_t blocks[6][64], int mb_x, int mb_y) {
    size_t ly = (size_t)mb_y * 16 * f->stride[0] + (size_t)mb_x * 16;
    size_t cy = (size_t)mb_y * 8 * f->stride[1] + (size_t)mb_x * 8;
    if (ly + 8 * f->stride[0] + 8 > (size_t)f->coded_width * f->coded_height)
        return DK2_ERR_BOUNDS;
    if (cy + 8 * f->stride[1] + 8 > (size_t)f->coded_width * f->coded_height / 2)
        return DK2_ERR_BOUNDS;

    ea_idct_put(f->planes[0] + ly, f->stride[0], blocks[0]);
    ea_idct_put(f->planes[0] + ly + 8, f->stride[0], blocks[1]);
    ea_idct_put(f->planes[0] + ly + 8 * f->stride[0], f->stride[0], blocks[2]);
    ea_idct_put(f->planes[0] + ly + 8 * f->stride[0] + 8, f->stride[0], blocks[3]);
    ea_idct_put(f->planes[1] + cy, f->stride[1], blocks[4]);
    ea_idct_put(f->planes[2] + cy, f->stride[2], blocks[5]);
    return DK2_OK;
}

/* ------------------------------------------------------------------ */
/* Public frame API.                                                   */
/* ------------------------------------------------------------------ */

void dk2_tqi_frame_free(Dk2TqiFrame *f) {
    if (!f) return;
    for (int i = 0; i < 3; ++i) {
        free(f->planes[i]);
        f->planes[i] = NULL;
    }
}

Dk2Result dk2_tqi_decode_frame(const uint8_t *payload, size_t payload_size,
                               uint32_t frame_index, Dk2TqiFrame *out) {
    if (!payload || !out || payload_size < 8) return DK2_ERR_ARGS;
    memset(out, 0, sizeof(*out));

    uint16_t width, height;
    dk2_u16le(payload, &width);
    dk2_u16le(payload + 2, &height);
    if (width == 0 || height == 0 || width > TQI_MAX_DIM || height > TQI_MAX_DIM)
        return DK2_ERR_SIZE;

    uint16_t coded_width = (uint16_t)((width + 15) & ~15);
    uint16_t coded_height = (uint16_t)((height + 15) & ~15);
    if ((size_t)coded_width * coded_height > 64u * 1024u * 1024u) return DK2_ERR_SIZE;

    out->width = width;
    out->height = height;
    out->coded_width = coded_width;
    out->coded_height = coded_height;
    out->quantizer = payload[4];
    out->extra[0] = payload[5];
    out->extra[1] = payload[6];
    out->extra[2] = payload[7];
    out->frame_index = frame_index;
    out->stride[0] = coded_width;
    out->stride[1] = coded_width / 2;
    out->stride[2] = coded_width / 2;

    size_t luma_size = (size_t)coded_width * coded_height;
    size_t chroma_size = (size_t)(coded_width / 2) * (coded_height / 2);
    out->planes[0] = (uint8_t *)calloc(luma_size, 1);
    out->planes[1] = (uint8_t *)calloc(chroma_size, 1);
    out->planes[2] = (uint8_t *)calloc(chroma_size, 1);
    if (!out->planes[0] || !out->planes[1] || !out->planes[2]) {
        dk2_tqi_frame_free(out);
        return DK2_ERR_NOMEM;
    }

    TqiCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    br_init(&ctx.br, payload + 8, payload_size - 8);
    calculate_dequant(ctx.dequant, out->quantizer);
    ctx.last_dc[0] = ctx.last_dc[1] = ctx.last_dc[2] = 0;

    int mb_w = (width + 15) / 16;
    int mb_h = (height + 15) / 16;
    Dk2Result rc = DK2_OK;
    for (int y = 0; y < mb_h && rc == DK2_OK; ++y) {
        for (int x = 0; x < mb_w && rc == DK2_OK; ++x) {
            int32_t blocks[6][64];
            memset(blocks, 0, sizeof(blocks));
            for (int n = 0; n < 6; ++n) {
                rc = mpeg1_decode_block(&ctx, blocks[n], n);
                if (rc != DK2_OK) break;
            }
            if (rc == DK2_OK) rc = idct_put(out, blocks, x, y);
        }
    }
    if (rc == DK2_OK && ctx.br.overrun) rc = DK2_ERR_BOUNDS;
    if (rc != DK2_OK) dk2_tqi_frame_free(out);
    return rc;
}

Dk2Result dk2_tqi_frame_to_rgb24(const Dk2TqiFrame *f,
                                 uint8_t *rgb, size_t rgb_size) {
    if (!f || !rgb) return DK2_ERR_ARGS;
    size_t w = f->width, h = f->height;
    if (w > SIZE_MAX / 3 || h > SIZE_MAX / (w * 3)) return DK2_ERR_SIZE;
    if (rgb_size < w * h * 3) return DK2_ERR_BOUNDS;

    size_t luma_size = (size_t)f->coded_width * f->coded_height;
    size_t chroma_size = (size_t)(f->coded_width / 2) * (f->coded_height / 2);

    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            size_t yi = y * w + x;
            size_t ci = (y / 2) * (w / 2) + (x / 2);
            if (yi >= luma_size || ci >= chroma_size) return DK2_ERR_BOUNDS;
            int yp = f->planes[0][yi];
            int up = f->planes[1][ci];
            int vp = f->planes[2][ci];
            int r = (int)(yp + 1.402 * (vp - 128));
            int g = (int)(yp - 0.34414 * (up - 128) - 0.71414 * (vp - 128));
            int b = (int)(yp + 1.772 * (up - 128));
            /* TgqFrame.getImage() builds one Java int, then BufferedImage
             * TYPE_INT_RGB + ImageIO PNG keep its low 24 bits. A negative
             * g sign-extends into the red byte, so mask the packed word,
             * not each channel separately. */
            uint32_t packed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            rgb[yi * 3 + 0] = (uint8_t)(packed >> 16);
            rgb[yi * 3 + 1] = (uint8_t)(packed >> 8);
            rgb[yi * 3 + 2] = (uint8_t)packed;
        }
    }
    return DK2_OK;
}

Dk2Result dk2_tqi_frame_write_ppm(const Dk2TqiFrame *f, FILE *fp) {
    if (!f || !fp) return DK2_ERR_ARGS;
    size_t w = f->width, h = f->height;
    if (w > SIZE_MAX / 3 || h > SIZE_MAX / (w * 3)) return DK2_ERR_SIZE;
    uint8_t *rgb = (uint8_t *)malloc(w * h * 3);
    if (!rgb) return DK2_ERR_NOMEM;
    Dk2Result rc = dk2_tqi_frame_to_rgb24(f, rgb, w * h * 3);
    if (rc == DK2_OK) {
        if (fprintf(fp, "P6\n%u %u\n255\n", (unsigned)w, (unsigned)h) < 0 ||
            fwrite(rgb, 1, w * h * 3, fp) != w * h * 3) {
            rc = DK2_ERR_IO;
        }
    }
    free(rgb);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Container walk.                                                     */
/* ------------------------------------------------------------------ */

Dk2Result dk2_tqi_find_frames(const uint8_t *data, size_t size,
                              Dk2TqiChunkRef **out, size_t *count) {
    if (!data || !out || !count) return DK2_ERR_ARGS;
    *out = NULL;
    *count = 0;
    size_t p = 0, cnt = 0, cap = 0;
    Dk2TqiChunkRef *refs = NULL;

    while (p + 8 <= size) {
        uint32_t sz;
        dk2_u32le(data + p + 4, &sz);
        if (sz < 8 || sz > size - p) {
            free(refs);
            return DK2_ERR_CORRUPT;
        }
        if (memcmp(data + p, "pIQT", 4) == 0) {
            if (cnt == cap) {
                size_t new_cap = cap ? cap * 2 : 64;
                Dk2TqiChunkRef *tmp = (Dk2TqiChunkRef *)realloc(refs, new_cap * sizeof(*refs));
                if (!tmp) {
                    free(refs);
                    return DK2_ERR_NOMEM;
                }
                refs = tmp;
                cap = new_cap;
            }
            refs[cnt].offset = p + 8;
            refs[cnt].size = sz - 8;
            refs[cnt].index = (uint32_t)cnt;
            ++cnt;
        }
        p += sz;
    }
    if (p != size) {
        free(refs);
        return DK2_ERR_CORRUPT;
    }
    *out = refs;
    *count = cnt;
    return DK2_OK;
}

void dk2_tqi_free_refs(Dk2TqiChunkRef *refs) {
    free(refs);
}
