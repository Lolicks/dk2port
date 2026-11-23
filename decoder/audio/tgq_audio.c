#include "tgq_audio.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xa.h"

/* PT defaults: TgqFile.java:287-297. The PT TLV has no sample-rate tag. */
#define DK2_AUDIO_DEFAULT_RATE 22050u
#define DK2_AUDIO_DEFAULT_BITS 16u
#define DK2_AUDIO_COMP_EA_XA 7 /* TgqFile.java:335-337: 0x07 -> EA_XA_ADPCM */

/* Length-prefixed BIG-ENDIAN value from TgqFile.java:355-366 (getValue). */
static Dk2Result pt_value(const uint8_t *p, size_t n, size_t *pos, uint32_t *out) {
    if (*pos >= n) return DK2_ERR_BOUNDS;
    uint32_t len = p[(*pos)++];
    if (len > n - *pos) return DK2_ERR_BOUNDS;
    uint32_t v = 0;
    for (uint32_t i = 0; i < len; ++i) {
        v = (v << 8) | p[(*pos)++];
    }
    *out = v;
    return DK2_OK;
}

/*
 * Minimal PT parser mirroring TgqFile.java:204-285:
 * "PT" + u16 platform, then length-prefixed tags; 0xFC/0xFE/0xFD open a
 * substream carrying 0x82 channels, 0x83 compression id, 0x85 total samples;
 * 0x8A leaves a substream, 0xFF ends the whole header.
 */
static Dk2Result parse_pt(const uint8_t *p, size_t n, Dk2TgqAudioInfo *info) {
    if (!p || n < 4 || memcmp(p, "PT", 2) != 0) return DK2_ERR_MAGIC;
    size_t i = 4;
    int channels = -1;
    int compression = -1;

    while (i < n) {
        uint8_t tag = p[i++];
        if (tag == 0xFF) break;
        if (tag == 0xFC || tag == 0xFE || tag == 0xFD) {
            int in_sub = 1;
            while (in_sub && i < n) {
                uint8_t sub = p[i++];
                if (sub == 0xFF) return DK2_OK; /* TgqFile.java:272-275: end all */
                uint32_t val = 0;
                Dk2Result rc = pt_value(p, n, &i, &val);
                if (rc != DK2_OK) return rc;
                switch (sub) {
                    case 0x82: channels = (int)val; break;      /* TgqFile.java:254-256 */
                    case 0x83: compression = (int)val; break;   /* TgqFile.java:257-260 */
                    case 0x85: info->declared_total = val; break; /* TgqFile.java:261-263 */
                    case 0x8A: in_sub = 0; break;               /* TgqFile.java:264-270 */
                    default: break;
                }
            }
        } else {
            uint32_t ignored = 0;
            Dk2Result rc = pt_value(p, n, &i, &ignored);
            if (rc != DK2_OK) return rc;
        }
    }
    if (compression != DK2_AUDIO_COMP_EA_XA || channels != 2) return DK2_ERR_UNSUPPORTED;
    info->channels = (uint16_t)channels;
    return DK2_OK;
}

static int tag_is(const uint8_t *p, const char *tag) {
    return memcmp(p, tag, 4) == 0;
}

static Dk2Result ensure_capacity(int16_t **pcm, size_t *cap, size_t need) {
    if (need <= *cap) return DK2_OK;
    size_t next = *cap ? *cap : 4096;
    while (next < need) {
        if (next > SIZE_MAX / 2) return DK2_ERR_NOMEM;
        next *= 2;
    }
    int16_t *grown = (int16_t *)realloc(*pcm, next * sizeof(**pcm));
    if (!grown) return DK2_ERR_NOMEM;
    *pcm = grown;
    *cap = next;
    return DK2_OK;
}

Dk2Result dk2_tgq_audio_decode(const uint8_t *tgq, size_t n,
                               int16_t **pcm_out, size_t *frames_out,
                               Dk2TgqAudioInfo *info) {
    if (!tgq || !pcm_out || !frames_out) return DK2_ERR_ARGS;
    *pcm_out = NULL;
    *frames_out = 0;

    Dk2TgqAudioInfo inf;
    memset(&inf, 0, sizeof(inf));
    inf.sample_rate = DK2_AUDIO_DEFAULT_RATE;
    inf.bits_per_sample = DK2_AUDIO_DEFAULT_BITS;
    inf.channels = 2;

    int16_t *pcm = NULL;
    size_t cap = 0, frames = 0;
    int have_header = 0;
    size_t p = 0;
    Dk2Result rc = DK2_OK;

    while (p + 8 <= n) {
        uint32_t sz = 0;
        dk2_u32le(tgq + p + 4, &sz);
        if (sz < 8 || sz > n - p) { rc = DK2_ERR_CORRUPT; goto fail; }
        const uint8_t *pay = tgq + p + 8;
        size_t ps = (size_t)sz - 8;

        if (tag_is(tgq + p, "SCHl") || tag_is(tgq + p, "SHEN")) {
            rc = parse_pt(pay, ps, &inf);
            if (rc != DK2_OK) goto fail;
            have_header = 1;
        } else if (tag_is(tgq + p, "SCCl") || tag_is(tgq + p, "SCEN")) {
            if (ps < 4) { rc = DK2_ERR_CORRUPT; goto fail; }
            dk2_u32le(pay, &inf.expected_blocks);
            inf.have_count = 1;
        } else if (tag_is(tgq + p, "SCDl") || tag_is(tgq + p, "SDEN")) {
            int32_t coded = 0, pl = 0, pr = 0;
            rc = dk2_xa_read_block_header(pay, ps, &coded, &pl, &pr);
            if (rc != DK2_OK) goto fail;

            if (coded > 0) {
                if ((uint64_t)frames + (uint64_t)coded > (uint64_t)SIZE_MAX / 2u) {
                    rc = DK2_ERR_SIZE; goto fail;
                }
                rc = ensure_capacity(&pcm, &cap, (frames + (size_t)coded) * 2u);
                if (rc != DK2_OK) goto fail;

                int32_t pred_l = pl, pred_r = pr;
                const uint8_t *group = pay + DK2_XA_HEADER_BYTES;
                for (int32_t k = 0; k < coded / DK2_XA_GROUP_SAMPLES; ++k) {
                    int16_t *out = pcm + frames * 2u;
                    rc = dk2_xa_decode_block(group, out, 2, &pred_l, &pred_r);
                    if (rc != DK2_OK) goto fail;
                    for (int s = 0; s < DK2_XA_GROUP_SAMPLES * 2; ++s) {
                        if (out[s] == INT16_MIN) ++inf.clip_min;
                        else if (out[s] == INT16_MAX) ++inf.clip_max;
                    }
                    group += DK2_XA_GROUP_BYTES;
                    frames += DK2_XA_GROUP_SAMPLES;
                }
            }
            ++inf.blocks;
        }
        /* pIQT video, SCEl/SEEN end markers and unknown tags are skipped,
         * exactly as TgqFile.java:155-177 does. */
        p += sz;
    }
    if (p != n) { rc = DK2_ERR_CORRUPT; goto fail; }
    if (!have_header) { rc = DK2_ERR_CORRUPT; goto fail; }
    if (inf.have_count && inf.blocks != inf.expected_blocks) {
        rc = DK2_ERR_CORRUPT; goto fail;
    }

    inf.sample_frames = frames;
    *pcm_out = pcm;
    *frames_out = frames;
    if (info) *info = inf;
    return DK2_OK;

fail:
    free(pcm);
    return rc;
}

void dk2_tgq_audio_free(int16_t *pcm) {
    free(pcm);
}

/* Canonical RIFF/WAVE, PCM format 1, little-endian 16-bit samples. */
static Dk2Result write_wave(const char *path, const int16_t *pcm, size_t frames,
                            uint32_t rate, uint16_t channels, uint16_t bits) {
    if (!path || (!pcm && frames)) return DK2_ERR_ARGS;
    uint64_t bytes64 = (uint64_t)frames * channels * (bits / 8u);
    if (bytes64 > (uint64_t)UINT32_MAX - 36u) return DK2_ERR_SIZE;

    FILE *f = fopen(path, "wb");
    if (!f) return DK2_ERR_IO;
    uint32_t data_bytes = (uint32_t)bytes64;
    uint8_t h[44];
    memset(h, 0, sizeof(h));
    memcpy(h, "RIFF", 4);
    h[4] = (uint8_t)(36u + data_bytes);
    h[5] = (uint8_t)((36u + data_bytes) >> 8);
    h[6] = (uint8_t)((36u + data_bytes) >> 16);
    h[7] = (uint8_t)((36u + data_bytes) >> 24);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    h[16] = 16; /* fmt chunk size */
    h[20] = 1; /* WAVE_FORMAT_PCM */
    h[22] = (uint8_t)channels;
    h[23] = (uint8_t)(channels >> 8);
    h[24] = (uint8_t)rate;
    h[25] = (uint8_t)(rate >> 8);
    h[26] = (uint8_t)(rate >> 16);
    h[27] = (uint8_t)(rate >> 24);
    uint32_t byte_rate = rate * channels * (bits / 8u);
    h[28] = (uint8_t)byte_rate;
    h[29] = (uint8_t)(byte_rate >> 8);
    h[30] = (uint8_t)(byte_rate >> 16);
    h[31] = (uint8_t)(byte_rate >> 24);
    uint16_t align = (uint16_t)(channels * (bits / 8u));
    h[32] = (uint8_t)align;
    h[33] = (uint8_t)(align >> 8);
    h[34] = (uint8_t)bits;
    h[35] = (uint8_t)(bits >> 8);
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)data_bytes;
    h[41] = (uint8_t)(data_bytes >> 8);
    h[42] = (uint8_t)(data_bytes >> 16);
    h[43] = (uint8_t)(data_bytes >> 24);
    if (fwrite(h, 1, sizeof(h), f) != sizeof(h)) { fclose(f); return DK2_ERR_IO; }

    /* Emit each sample explicitly little-endian, independent of host byte order. */
    size_t samples = frames * (size_t)channels;
    for (size_t i = 0; i < samples; ++i) {
        uint16_t u = (uint16_t)pcm[i];
        if (fputc((int)(u & 0xFFu), f) == EOF || fputc((int)(u >> 8), f) == EOF) {
            fclose(f);
            return DK2_ERR_IO;
        }
    }
    return fclose(f) == 0 ? DK2_OK : DK2_ERR_IO;
}

Dk2Result dk2_tgq_audio_extract(const uint8_t *tgq, size_t n, const char *out_wav) {
    int16_t *pcm = NULL;
    size_t frames = 0;
    Dk2TgqAudioInfo info;
    Dk2Result rc = dk2_tgq_audio_decode(tgq, n, &pcm, &frames, &info);
    if (rc != DK2_OK) return rc;
    rc = write_wave(out_wav, pcm, frames, info.sample_rate,
                    info.channels, info.bits_per_sample);
    dk2_tgq_audio_free(pcm);
    return rc;
}
