#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/reader.h"
#include "../tgq/tgq.h"
#include "../tqi/tqi.h"
#include "../tqi/tqi_tables.h"

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

/* FNV-1a of the RGB bytes of OpenKeeper reference PNGs frame_000..009. */
static const uint64_t expected_rgb_hash[10] = {
    UINT64_C(0xa8e323e002c4822a), UINT64_C(0x682f2a062a847b1e),
    UINT64_C(0x662211d18a9ad09b), UINT64_C(0xebf65e46f6d599b2),
    UINT64_C(0x0ca1168fcd520b2f), UINT64_C(0xc55028341d625ec7),
    UINT64_C(0x9c1d8a79079365e2), UINT64_C(0xf0859aaca1bc088c),
    UINT64_C(0xf42448b961738f65), UINT64_C(0x9962d561263eac40)
};

static void check_tables(void) {
    int seen[64];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < 64; ++i) {
        int j = TQI_SCAN_TABLE[i];
        CHECK(j >= 0 && j < 64);
        if (j >= 0 && j < 64) {
            CHECK(seen[j] == 0);
            seen[j] = 1;
        }
    }
    for (int i = 0; i < 227; ++i) {
        for (int k = 0; k < 2; ++k) {
            int c = TQI_DCT_COEFF[i][k];
            CHECK(c == -1 || (c >= 0 && c < 227));
        }
    }
    for (int i = 0; i < 19; ++i) {
        for (int k = 0; k < 2; ++k) {
            int c = TQI_DC_LUMINANCE_VLC[i][k];
            CHECK(c == -1 || (c >= 0 && c < 19));
            c = TQI_DC_CHROMINANCE_VLC[i][k];
            CHECK(c == -1 || (c >= 0 && c < 19));
        }
    }
    CHECK(TQI_DCT_COEFF[1][2] == -5 && TQI_DCT_COEFF[1][3] == -5);
    CHECK(TQI_DCT_COEFF[17][2] == -2 && TQI_DCT_COEFF[17][3] == -2);
    CHECK(TQI_BASE_TABLE[0] == 4096 && TQI_BASE_TABLE2[0] == 8);
}

static void check_errors(void) {
    Dk2TqiFrame f;
    uint8_t short_payload[7] = {0};
    uint8_t bad_header[16] = {0};
    Dk2TqiChunkRef *refs = (Dk2TqiChunkRef *)0x1;
    size_t count = 99;

    CHECK(dk2_tqi_decode_frame(NULL, 0, 0, &f) == DK2_ERR_ARGS);
    CHECK(dk2_tqi_decode_frame(short_payload, sizeof(short_payload), 0, &f) == DK2_ERR_ARGS);
    CHECK(dk2_tqi_decode_frame(bad_header, sizeof(bad_header), 0, &f) == DK2_ERR_SIZE);
    CHECK(dk2_tqi_find_frames(NULL, 0, &refs, &count) == DK2_ERR_ARGS);

    uint8_t truncated[16];
    memset(truncated, 0, sizeof(truncated));
    memcpy(truncated, "pIQT", 4);
    truncated[7] = 0x7f; /* chunk size far past the buffer */
    CHECK(dk2_tqi_find_frames(truncated, sizeof(truncated), &refs, &count) == DK2_ERR_CORRUPT);
    CHECK(refs == NULL && count == 0);
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : "..";
    char path[1024];

    check_tables();
    check_errors();

    snprintf(path, sizeof(path), "%s/Data/Movies/BullfrogIntro.tgq", game);
    size_t n;
    uint8_t *d = read_file(path, &n);
    CHECK(d != NULL);
    if (!d) {
        printf("C TQI TESTS: FAIL (%d failures)\n", fails);
        return 1;
    }

    Dk2TgqInfo ti;
    CHECK(dk2_tgq_parse(d, n, &ti) == DK2_OK);
    CHECK(ti.frame_count == 189);
    CHECK(ti.width == 320 && ti.height == 480);
    CHECK(ti.quantizer == 0x64);
    CHECK(ti.first_frame_offset == 48);

    Dk2TqiChunkRef *refs = NULL;
    size_t count = 0;
    CHECK(dk2_tqi_find_frames(d, n, &refs, &count) == DK2_OK);
    CHECK(count == ti.frame_count);
    if (count > 0) {
        CHECK(refs[0].offset == 48);
        CHECK(refs[0].size == 6484);
        /* One trailing SCEl (8 bytes) closes the audio stream. */
        size_t tail = refs[count - 1].offset + refs[count - 1].size;
        CHECK(n - tail == 8);
        if (n - tail == 8) CHECK(memcmp(d + tail, "SCEl", 4) == 0);
    }

    int decoded = 0;
    for (size_t i = 0; refs && i < count; ++i) {
        Dk2TqiFrame f;
        Dk2Result rc = dk2_tqi_decode_frame(d + refs[i].offset, refs[i].size,
                                            refs[i].index, &f);
        if (rc != DK2_OK) {
            printf("FAIL frame %zu decode: %d\n", i, (int)rc);
            ++fails;
            continue;
        }
        CHECK(f.width == 320 && f.height == 480);
        CHECK(f.coded_width == 320 && f.coded_height == 480);
        CHECK(f.quantizer == 0x64);
        CHECK(f.extra[0] == 0x14 && f.extra[1] == 0x1e && f.extra[2] == 0x03);
        CHECK(f.stride[0] == 320 && f.stride[1] == 160 && f.stride[2] == 160);
        CHECK(f.frame_index == (uint32_t)i);

        if (i < 10) {
            size_t rgb_size = (size_t)f.width * f.height * 3;
            uint8_t *rgb = (uint8_t *)malloc(rgb_size);
            CHECK(rgb != NULL);
            if (rgb) {
                CHECK(dk2_tqi_frame_to_rgb24(&f, rgb, rgb_size) == DK2_OK);
                uint64_t got = fnv1a64(rgb, rgb_size);
                CHECK(got == expected_rgb_hash[i]);
                if (got != expected_rgb_hash[i])
                    printf("  frame %zu hash=%016llx expected=%016llx\n", i,
                           (unsigned long long)got,
                           (unsigned long long)expected_rgb_hash[i]);
                free(rgb);
            }
        }
        if (i == 0) {
            FILE *tmp = tmpfile();
            CHECK(tmp != NULL);
            if (tmp) {
                CHECK(dk2_tqi_frame_write_ppm(&f, tmp) == DK2_OK);
                long size = ftell(tmp);
                CHECK(size == 15 + 460800); /* "P6\n320 480\n255\n" */
                rewind(tmp);
                char magic[3] = {0};
                int w = 0, h = 0, maxv = 0;
                CHECK(fscanf(tmp, "%2s %d %d %d", magic, &w, &h, &maxv) == 4);
                CHECK(strcmp(magic, "P6") == 0 && w == 320 && h == 480 && maxv == 255);
                fclose(tmp);
            }
        }
        dk2_tqi_frame_free(&f);
        ++decoded;
    }
    CHECK(decoded == 189);
    dk2_tqi_free_refs(refs);
    free(d);

    printf("C TQI TESTS: %s (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
