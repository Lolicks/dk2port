#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../core/reader.h"
#include "../wad/wad.h"
#include "../kmsh/kmsh.h"
#include "../kmf/kmf.h"
#include "../tgq/tgq.h"
#include "../kmf/anim.h"
#include "../tqi/tqi.h"
#include "../audio/tgq_audio.h"

static const char *err_name(Dk2Result r) {
    switch (r) {
        case DK2_OK: return "ok";
        case DK2_ERR_ARGS: return "args";
        case DK2_ERR_IO: return "io";
        case DK2_ERR_MAGIC: return "magic";
        case DK2_ERR_VERSION: return "version";
        case DK2_ERR_BOUNDS: return "bounds";
        case DK2_ERR_CORRUPT: return "corrupt";
        case DK2_ERR_NOMEM: return "nomem";
        case DK2_ERR_SIZE: return "size";
        case DK2_ERR_UNSUPPORTED: return "unsupported";
        default: return "unknown";
    }
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    buf[len] = 0;
    *size = (size_t)len;
    return buf;
}

static int cmd_wad(int argc, char **argv) {
    if (argc < 2) return 1;
    size_t n;
    uint8_t *d = read_file(argv[1], &n);
    if (!d) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    Dk2Wad w;
    Dk2Result rc = dk2_wad_parse(d, n, &w);
    if (rc != DK2_OK) { fprintf(stderr, "wad parse: %s\n", err_name(rc)); free(d); return 3; }
    printf("WAD %s: %u entries, name pool %u bytes\n",
           argv[1], w.count, w.pool_size);
    uint32_t compressed = 0;
    for (uint32_t i = 0; i < w.count; ++i)
        if (w.entries[i].flags == 4) ++compressed;
    printf("compressed entries: %u\n", compressed);
    if (argc >= 3 && strcmp(argv[2], "--list") == 0) {
        uint32_t shown = w.count < 20 ? w.count : 20;
        for (uint32_t i = 0; i < shown; ++i)
            printf("  [%u] %-50s size=%u flags=%u\n", i, w.entries[i].name,
                   w.entries[i].data_size, w.entries[i].flags);
    }
    dk2_wad_free(&w);
    free(d);
    return 0;
}

static int cmd_kmsh(int argc, char **argv) {
    if (argc < 3) return 1;
    size_t n;
    uint8_t *d = read_file(argv[1], &n);
    if (!d) return 2;
    uint8_t *out = NULL;
    size_t out_size = 0;
    Dk2Result rc = dk2_kmsh_decompress(d, n, &out, &out_size);
    if (rc != DK2_OK) { fprintf(stderr, "kmsh: %s\n", err_name(rc)); free(d); return 3; }
    FILE *f = fopen(argv[2], "wb");
    if (!f) { free(out); free(d); return 2; }
    fwrite(out, 1, out_size, f);
    fclose(f);
    printf("decompressed %zu -> %zu bytes\n", n, out_size);
    free(out);
    free(d);
    return 0;
}

static int cmd_kmf(int argc, char **argv) {
    if (argc < 2) return 1;
    size_t n;
    uint8_t *d = read_file(argv[1], &n);
    if (!d) return 2;
    Dk2KmfInfo info;
    Dk2Result rc = dk2_kmf_info(d, n, &info);
    if (rc != DK2_OK) { fprintf(stderr, "kmf: %s\n", err_name(rc)); free(d); return 3; }
    printf("%s: version=%u type=%u chunks=%u materials=%u textures=%u\n",
           argv[1], info.version, info.type, info.chunk_count,
           info.material_count, info.texture_count);
    printf("  vertices=%u extended=%u triangles(all LOD)=%u lod0=%u groups=%u lod=%u\n",
           info.vertex_count, info.extended_count, info.triangle_count,
           info.lod0_triangles, info.sprite_groups, info.lod_count);
    if (argc >= 4 && strcmp(argv[2], "--obj") == 0) {
        FILE *f = fopen(argv[3], "wb");
        if (!f) { free(d); return 2; }
        rc = dk2_kmf_export_obj(d, n, f);
        fclose(f);
        if (rc != DK2_OK) fprintf(stderr, "obj export: %s\n", err_name(rc));
    }
    free(d);
    return 0;
}

static int cmd_tgq(int argc, char **argv) {
    if (argc < 2) return 1;
    size_t n;
    uint8_t *d = read_file(argv[1], &n);
    if (!d) return 2;
    Dk2TgqInfo info;
    Dk2Result rc = dk2_tgq_parse(d, n, &info);
    if (rc != DK2_OK) { fprintf(stderr, "tgq: %s\n", err_name(rc)); free(d); return 3; }
    printf("%s: %ux%u frames=%u audio_blocks=%u quantizer=0x%02x first=0x%zx\n",
           argv[1], info.width, info.height, info.frame_count, info.audio_count,
           info.quantizer, info.first_frame_offset);
    free(d);
    return 0;
}

static int cmd_tgqdecode(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: dk2tool tgqdecode <movie.tgq> <outdir> [--frames N]\n");
        return 1;
    }
    const char *movie = argv[1];
    const char *outdir = argv[2];
    int max_frames = -1;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end = NULL;
            long v = strtol(argv[++i], &end, 10);
            if (*argv[i] == 0 || (end && *end != 0) || v < 0 || v > INT_MAX) {
                fprintf(stderr, "tgqdecode: bad --frames value: %s\n", argv[i]);
                return 1;
            }
            max_frames = (int)v;
        } else {
            fprintf(stderr, "tgqdecode: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (mkdir(outdir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "tgqdecode: cannot create %s: %s\n", outdir, strerror(errno));
        return 2;
    }
    struct stat st;
    if (stat(outdir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "tgqdecode: not a directory: %s\n", outdir);
        return 2;
    }

    size_t n;
    uint8_t *d = read_file(movie, &n);
    if (!d) {
        fprintf(stderr, "tgqdecode: cannot read %s\n", movie);
        return 2;
    }
    Dk2TqiChunkRef *refs = NULL;
    size_t count = 0;
    Dk2Result rc = dk2_tqi_find_frames(d, n, &refs, &count);
    if (rc != DK2_OK) {
        fprintf(stderr, "tgqdecode: container: %s\n", err_name(rc));
        free(d);
        return 3;
    }

    size_t todo = count;
    if (max_frames >= 0 && (size_t)max_frames < todo) todo = (size_t)max_frames;
    int status = 0;
    for (size_t i = 0; i < todo; ++i) {
        const uint8_t *payload = d + refs[i].offset;
        Dk2TqiFrame frame;
        rc = dk2_tqi_decode_frame(payload, refs[i].size, refs[i].index, &frame);
        if (rc != DK2_OK) {
            fprintf(stderr, "tgqdecode: frame %u: %s\n", refs[i].index, err_name(rc));
            status = 3;
            break;
        }
        char path[4096];
        int len = snprintf(path, sizeof(path), "%s/frame_%03u.ppm", outdir, refs[i].index);
        if (len < 0 || (size_t)len >= sizeof(path)) {
            fprintf(stderr, "tgqdecode: output path too long\n");
            dk2_tqi_frame_free(&frame);
            status = 2;
            break;
        }
        FILE *fp = fopen(path, "wb");
        if (!fp) {
            fprintf(stderr, "tgqdecode: cannot write %s\n", path);
            dk2_tqi_frame_free(&frame);
            status = 2;
            break;
        }
        rc = dk2_tqi_frame_write_ppm(&frame, fp);
        long bytes = ftell(fp);
        if (fclose(fp) != 0) rc = DK2_ERR_IO;
        if (rc != DK2_OK) {
            fprintf(stderr, "tgqdecode: ppm write: %s\n", err_name(rc));
            dk2_tqi_frame_free(&frame);
            status = 3;
            break;
        }
        printf("frame=%u size=%ux%u quantizer=0x%02x extra=%02x%02x%02x ppm=%s bytes=%ld\n",
               frame.frame_index, frame.width, frame.height, frame.quantizer,
               frame.extra[0], frame.extra[1], frame.extra[2], path, bytes);
        dk2_tqi_frame_free(&frame);
    }
    printf("movie=%s frames=%zu decoded=%zu\n", movie, count, todo);
    dk2_tqi_free_refs(refs);
    free(d);
    return status;
}

static int cmd_tgqaudio(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: dk2tool tgqaudio <movie.tgq> <out.wav>\n");
        return 1;
    }
    size_t n;
    uint8_t *d = read_file(argv[1], &n);
    if (!d) {
        fprintf(stderr, "tgqaudio: cannot read %s\n", argv[1]);
        return 2;
    }
    Dk2TgqAudioInfo info;
    int16_t *pcm = NULL;
    size_t frames = 0;
    Dk2Result rc = dk2_tgq_audio_decode(d, n, &pcm, &frames, &info);
    if (rc != DK2_OK) {
        fprintf(stderr, "tgqaudio: decode: %s\n", err_name(rc));
        free(d);
        return 3;
    }
    printf("tgqaudio: %s blocks=%u frames=%llu declared=%u rate=%u ch=%u bits=%u "
           "clip_min=%u clip_max=%u\n",
           argv[1], info.blocks, (unsigned long long)frames, info.declared_total,
           info.sample_rate, info.channels, info.bits_per_sample,
           info.clip_min, info.clip_max);
    dk2_tgq_audio_free(pcm);
    rc = dk2_tgq_audio_extract(d, n, argv[2]);
    free(d);
    if (rc != DK2_OK) {
        fprintf(stderr, "tgqaudio: wav write: %s\n", err_name(rc));
        return 4;
    }
    printf("tgqaudio: wrote %s (%llu sample frames)\n", argv[2],
           (unsigned long long)frames);
    return 0;
}

static int cmd_anim(int argc, char **argv) {
    if (argc < 3) return 1;
    size_t n;
    uint8_t *d = read_file(argv[1], &n);
    if (!d) return 2;
    unsigned limit = 3;
    if (argc >= 4) limit = (unsigned)strtoul(argv[3], NULL, 10);
    unsigned written = 0;
    Dk2Result rc = dk2_anim_export(d, n, argv[2], limit, &written);
    if (rc != DK2_OK) {
        fprintf(stderr, "anim: %s\n", err_name(rc));
        free(d);
        return 3;
    }
    printf("anim frames written: %u\n", written);
    free(d);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: dk2tool <wad|kmsh|kmf|tgq|tgqdecode|tgqaudio> <file> [options]\n");
        return 1;
    }
    if (strcmp(argv[1], "wad") == 0) return cmd_wad(argc - 1, argv + 1);
    if (strcmp(argv[1], "kmsh") == 0) return cmd_kmsh(argc - 1, argv + 1);
    if (strcmp(argv[1], "kmf") == 0) return cmd_kmf(argc - 1, argv + 1);
    if (strcmp(argv[1], "tgq") == 0) return cmd_tgq(argc - 1, argv + 1);
    if (strcmp(argv[1], "anim") == 0) return cmd_anim(argc - 1, argv + 1);
    if (strcmp(argv[1], "tgqdecode") == 0) return cmd_tgqdecode(argc - 1, argv + 1);
    if (strcmp(argv[1], "tgqaudio") == 0) return cmd_tgqaudio(argc - 1, argv + 1);
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 1;
}
