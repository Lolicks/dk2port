#ifndef DK2_TGQ_AUDIO_H
#define DK2_TGQ_AUDIO_H

#include <stdint.h>

#include "../core/reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TGQ audio stream extraction. Container/chunk semantics and the PT header
 * are those parsed by OpenKeeper's TgqFile.java:55-64,119-181,204-366;
 * the per-block codec is xa.h / EAAudioFrame.java:52-109.
 */
typedef struct {
    uint32_t blocks;          /* number of SCDl/SDEN chunks decoded */
    uint64_t sample_frames;   /* sample frames PER CHANNEL in the output PCM */
    uint32_t declared_total;  /* PT sub-tag 0x85 (number of samples in stream) */
    uint32_t expected_blocks; /* SCCl/SCEN u32, when present */
    int have_count;           /* 1 when an SCCl/SCEN chunk was seen */
    uint32_t sample_rate;     /* 22050: TgqFile.java:294 (PT has no rate tag) */
    uint16_t channels;        /* from PT sub-tag 0x82 */
    uint16_t bits_per_sample; /* 16: TgqFile.java:291 */
    uint32_t clip_min;        /* output samples equal to INT16_MIN */
    uint32_t clip_max;        /* output samples equal to INT16_MAX */
} Dk2TgqAudioInfo;

/*
 * Decode every SCDl/SDEN chunk of a TGQ image into one interleaved signed
 * 16-bit LE PCM buffer. The caller owns *pcm_out and frees it with
 * dk2_tgq_audio_free(). info may be NULL.
 */
Dk2Result dk2_tgq_audio_decode(const uint8_t *tgq, size_t n,
                               int16_t **pcm_out, size_t *frames_out,
                               Dk2TgqAudioInfo *info);
void dk2_tgq_audio_free(int16_t *pcm);

/* Decode all audio and write a canonical RIFF/WAVE PCM (16-bit) file. */
Dk2Result dk2_tgq_audio_extract(const uint8_t *tgq, size_t n, const char *out_wav);

#ifdef __cplusplus
}
#endif
#endif
