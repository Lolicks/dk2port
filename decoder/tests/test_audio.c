#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/reader.h"
#include "../audio/xa.h"
#include "../audio/tgq_audio.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static uint8_t *read_file(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long l = ftell(f);
    if (l < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *b = (uint8_t *)malloc((size_t)l + 1);
    if (!b) { fclose(f); return NULL; }
    if (l && fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[l] = 0;
    *n = (size_t)l;
    return b;
}

static uint64_t fnv1a64(const uint8_t *p, size_t n) {
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= UINT64_C(0x100000001b3);
    }
    return h;
}

/* Independent container walk: only counts raw SCDl u32 declarations. */
typedef struct {
    uint32_t blocks;
    uint32_t sccl;
    uint64_t declared_raw;
    uint64_t declared_rounded;
} RawCounts;

static int walk_counts(const uint8_t *d, size_t n, RawCounts *c) {
    memset(c, 0, sizeof(*c));
    size_t p = 0;
    while (p + 8 <= n) {
        uint32_t sz = 0;
        dk2_u32le(d + p + 4, &sz);
        if (sz < 8 || sz > n - p) return 0;
        const uint8_t *pay = d + p + 8;
        size_t ps = (size_t)sz - 8;
        if (memcmp(d + p, "SCDl", 4) == 0 && ps >= 12) {
            uint32_t raw = 0;
            dk2_u32le(pay, &raw);
            c->declared_raw += raw;
            c->declared_rounded += raw - raw % DK2_XA_GROUP_SAMPLES;
            ++c->blocks;
        } else if (memcmp(d + p, "SCCl", 4) == 0 && ps >= 4) {
            dk2_u32le(pay, &c->sccl);
        }
        p += sz;
    }
    return p == n;
}

static void check_xa_table_and_api(void) {
    /* EAAudioFrame.java:31-37. */
    static const int expected[20] = {
        0, 240, 460, 392,
        0, 0, -208, -220,
        0, 1, 3, 4,
        7, 8, 10, 11,
        0, -1, -3, -4
    };
    for (int i = 0; i < 20; ++i) CHECK(dk2_xa_adpcm_table[i] == expected[i]);

    int16_t out[DK2_XA_GROUP_SAMPLES * 2];
    int32_t pl = dk2_xa_predictor_pack(0, 0);
    int32_t pr = dk2_xa_predictor_pack(0, 0);
    uint8_t group[DK2_XA_GROUP_BYTES] = {0};

    CHECK(dk2_xa_decode_block(NULL, out, 2, &pl, &pr) == DK2_ERR_ARGS);
    CHECK(dk2_xa_decode_block(group, NULL, 2, &pl, &pr) == DK2_ERR_ARGS);
    CHECK(dk2_xa_decode_block(group, out, 1, &pl, &pr) == DK2_ERR_UNSUPPORTED);
    CHECK(dk2_xa_decode_block(group, out, 2, &pl, &pr) == DK2_OK);
    for (int i = 0; i < DK2_XA_GROUP_SAMPLES * 2; ++i) CHECK(out[i] == 0);
    CHECK(pl == dk2_xa_predictor_pack(0, 0));
    CHECK(pr == dk2_xa_predictor_pack(0, 0));

    /* signExtend(v,4) equivalence (EAAudioFrame.java:118-122): use shift 5
     * (control byte 0xF0 -> 20-15) so the nibble's sign reaches the output.
     * With zero predictors and coefficient index 0 the exact Java results are
     * ((-8<<5)+0x80)>>8 = -1 and ((-1<<20)+0x80)>>8 = -4096. */
    uint8_t g2[DK2_XA_GROUP_BYTES];
    memset(g2, 0, sizeof(g2));
    g2[0] = 0x00; g2[1] = 0xF0; /* coeff index 0; shift_left=5, shift_right=20 */
    g2[2] = 0x8F;               /* sample 0: left -8<<5, right -1<<20 */
    g2[29] = 0x80;              /* sample 27: left -8<<5, right 0 */
    CHECK(dk2_xa_decode_block(g2, out, 2, &pl, &pr) == DK2_OK);
    CHECK(out[0] == -1 && out[1] == -4096);
    CHECK(out[54] == -1 && out[55] == 0);
    /* Final predictor = (last sample, second-to-last sample) per channel. */
    CHECK(pl == dk2_xa_predictor_pack(-1, 0));
    CHECK(pr == dk2_xa_predictor_pack(0, 0));

    memset(g2, 0, sizeof(g2));
    g2[0] = 0x00; g2[1] = 0x0F; /* shift_left=20, shift_right=5 */
    g2[2] = 0x7F;               /* (7<<20)+0x80>>8 = 28672; (-1<<5)+0x80>>8 = 0 */
    CHECK(dk2_xa_decode_block(g2, out, 2, &pl, &pr) == DK2_OK);
    CHECK(out[0] == 28672 && out[1] == 0);
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : "..";
    const char *wav = argc > 2 ? argv[2] : "build/BullfrogIntro_audio.wav";
    char path[1024];
    snprintf(path, sizeof(path), "%s/Data/Movies/BullfrogIntro.tgq", game);

    check_xa_table_and_api();

    size_t n;
    uint8_t *d = read_file(path, &n);
    CHECK(d != NULL);
    if (!d) {
        printf("C AUDIO TESTS: FAIL (%d failures)\n", fails);
        return 1;
    }

    RawCounts raw;
    CHECK(walk_counts(d, n, &raw));
    CHECK(raw.blocks == 187);
    CHECK(raw.sccl == 187);
    CHECK(raw.declared_raw == 164228);
    CHECK(raw.declared_rounded == 164220);

    int16_t *pcm = NULL;
    size_t frames = 0;
    Dk2TgqAudioInfo info;
    Dk2Result rc = dk2_tgq_audio_decode(d, n, &pcm, &frames, &info);
    CHECK(rc == DK2_OK);
    if (rc == DK2_OK) {
        CHECK(info.blocks == raw.blocks);
        CHECK(info.have_count && info.expected_blocks == raw.sccl);
        CHECK(info.declared_total == raw.declared_raw);
        CHECK(info.sample_frames == raw.declared_rounded);
        CHECK(frames == 164220);
        CHECK(info.sample_rate == 22050);
        CHECK(info.channels == 2 && info.bits_per_sample == 16);
        CHECK(info.clip_min == 0 && info.clip_max == 0);

        uint64_t h1 = fnv1a64((const uint8_t *)pcm, frames * 2 * sizeof(int16_t));
        printf("audio: blocks=%u frames=%zu declared=%u rate=%u ch=%u "
               "clip_min=%u clip_max=%u pcm_fnv1a=%016llx\n",
               info.blocks, frames, info.declared_total, info.sample_rate,
               info.channels, info.clip_min, info.clip_max,
               (unsigned long long)h1);
        printf("window[0:8] =");
        for (int i = 0; i < 8; ++i) printf(" %d", pcm[i]);
        printf("\n");

        int16_t *again = NULL;
        size_t frames2 = 0;
        CHECK(dk2_tgq_audio_decode(d, n, &again, &frames2, NULL) == DK2_OK);
        CHECK(frames2 == frames);
        if (again && frames2 == frames) {
            uint64_t h2 = fnv1a64((const uint8_t *)again, frames2 * 2 * sizeof(int16_t));
            CHECK(h1 == h2);
            printf("audio: deterministic redecode hash=%016llx\n", (unsigned long long)h2);
        }
        dk2_tgq_audio_free(again);
    }

    CHECK(dk2_tgq_audio_extract(d, n, wav) == DK2_OK);
    FILE *f = fopen(wav, "rb");
    CHECK(f != NULL);
    if (f) {
        uint8_t hdr[44];
        CHECK(fread(hdr, 1, 44, f) == 44);
        CHECK(memcmp(hdr, "RIFF", 4) == 0);
        CHECK(memcmp(hdr + 8, "WAVE", 4) == 0);
        CHECK(memcmp(hdr + 12, "fmt ", 4) == 0);
        CHECK(memcmp(hdr + 36, "data", 4) == 0);
        uint32_t riff_size = 0, fmt_size = 0, data_size = 0;
        uint16_t format = 0, channels = 0, bits = 0;
        uint32_t rate = 0;
        dk2_u32le(hdr + 4, &riff_size);
        dk2_u32le(hdr + 16, &fmt_size);
        dk2_u16le(hdr + 20, &format);
        dk2_u16le(hdr + 22, &channels);
        dk2_u32le(hdr + 24, &rate);
        dk2_u16le(hdr + 34, &bits);
        dk2_u32le(hdr + 40, &data_size);
        CHECK(fmt_size == 16 && format == 1);
        CHECK(channels == 2 && bits == 16 && rate == 22050);
        CHECK(data_size == frames * 4);
        CHECK(riff_size == 36 + data_size);
        printf("wav: %s data_bytes=%u riff_size=%u\n", wav, data_size, riff_size);
        fclose(f);
    }

    dk2_tgq_audio_free(pcm);
    free(d);
    printf("C AUDIO TESTS: %s (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
