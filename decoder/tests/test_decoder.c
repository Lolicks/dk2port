#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/reader.h"
#include "../wad/wad.h"
#include "../kmsh/kmsh.h"
#include "../kmf/kmf.h"
#include "../tgq/tgq.h"
#include "../kmf/anim.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static uint8_t *read_file(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    rewind(f);
    uint8_t *b = (uint8_t *)malloc((size_t)l + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[l] = 0;
    *n = (size_t)l;
    return b;
}

int main(int argc, char **argv) {
    const char *game = argc > 1 ? argv[1] : "..";
    const char *kmf_dir = argc > 2 ? argv[2] : "build/KMF";
    char path[1024];

    /* WAD */
    snprintf(path, sizeof(path), "%s/Data/Meshes.WAD", game);
    size_t n;
    uint8_t *d = read_file(path, &n);
    CHECK(d != NULL);
    if (d) {
        Dk2Wad w;
        CHECK(dk2_wad_parse(d, n, &w) == DK2_OK);
        CHECK(w.count == 2015);
        CHECK(strcmp(w.entries[0].name, "g_Maiden-Torture.kmf") == 0);
        CHECK(w.entries[0].flags == 4);
        CHECK(w.entries[0].uncompressed_size == 21287);

        /* KMSH */
        uint8_t *entry = NULL;
        size_t es = 0;
        CHECK(dk2_wad_get(d, n, &w.entries[0], &entry, &es) == DK2_OK);
        uint8_t *kmf = NULL;
        size_t ks = 0;
        CHECK(dk2_kmsh_decompress(entry, es, &kmf, &ks) == DK2_OK);
        CHECK(ks == 21287);
        if (kmf) {
            CHECK(memcmp(kmf, "KMSH", 4) == 0);
            Dk2KmfInfo info;
            CHECK(dk2_kmf_info(kmf, ks, &info) == DK2_OK);
            CHECK(info.version == 17);
            CHECK(info.type == 1 || info.type == 2);
            CHECK(info.chunk_count >= 3);
            CHECK(info.triangle_count > 0);
            printf("mesh0: chunks=%u tris=%u mats=%u verts=%u\n",
                   info.chunk_count, info.triangle_count,
                   info.material_count, info.vertex_count);
            free(kmf);
        }
        free(entry);

        /* corrupt input must fail safely */
        uint8_t bad[32];
        memset(bad, 0, sizeof(bad));
        CHECK(dk2_kmsh_decompress(bad, sizeof(bad), NULL, NULL) == DK2_ERR_ARGS);
        dk2_wad_free(&w);
        free(d);
    }

    /* 20 pre-decoded KMF files */
    const char *names[] = {
        "g_Maiden-Torture.kmf", "G_Maiden-electrocute.kmf", "g_maiden-elec.kmf",
        "3DFE_GemHolder.kmf", "DHeartPlug4.kmf", "PORTAL4.kmf",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", kmf_dir, names[i]);
        size_t k;
        uint8_t *b = read_file(path, &k);
        if (!b) continue;
        Dk2KmfInfo info;
        Dk2Result rc = dk2_kmf_info(b, k, &info);
        CHECK(rc == DK2_OK);
        if (rc == DK2_OK)
            printf("%s: type=%u tris=%u tex=%u\n", names[i], info.type,
                   info.triangle_count, info.texture_count);
        free(b);
    }

    /* ANIM frame export */
    snprintf(path, sizeof(path), "%s/Mis-Idle1.kmf", kmf_dir);
    uint8_t *anim = read_file(path, &n);
    if (anim) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/anim_test", kmf_dir);
        unsigned written = 0;
        Dk2Result ar = dk2_anim_export(anim, n, dir, 4, &written);
        CHECK(ar == DK2_OK);
        CHECK(written == 4);
        printf("anim: exported %u frames\n", written);
        free(anim);
    }

    /* TGQ */
    snprintf(path, sizeof(path), "%s/Data/Movies/BullfrogIntro.tgq", game);
    uint8_t *t = read_file(path, &n);
    CHECK(t != NULL);
    if (t) {
        Dk2TgqInfo ti;
        CHECK(dk2_tgq_parse(t, n, &ti) == DK2_OK);
        CHECK(ti.frame_count > 0);
        printf("tgq: frames=%u audio=%u %ux%u\n", ti.frame_count,
               ti.audio_count, ti.width, ti.height);
        free(t);
    }

    printf("C DECODER TESTS: %s (%d failures)\n", fails ? "FAIL" : "OK", fails);
    return fails ? 1 : 0;
}
