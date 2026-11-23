#include "anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define DK2_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define DK2_MKDIR(p) mkdir(p, 0777)
#endif

static int find_chunk(const uint8_t *d, size_t end, const char *tag,
                      Dk2Reader *out) {
    size_t p = 0;
    while (p + 8 <= end) {
        uint32_t sz;
        dk2_u32le(d + p + 4, &sz);
        if (sz < 8 || p + sz > end) return 0;
        if (memcmp(d + p, tag, 4) == 0) {
            dk2_reader_init(out, d + p + 8, sz - 8);
            return 1;
        }
        p += sz;
    }
    return 0;
}

static Dk2Result cstr(Dk2Reader *r, char *out, size_t cap) {
    size_t n = 0;
    while (r->pos < r->size && n + 1 < cap) {
        uint8_t c = r->data[r->pos++];
        if (!c) { out[n] = 0; return DK2_OK; }
        out[n++] = (char)c;
    }
    out[n] = 0;
    return DK2_ERR_CORRUPT;
}

Dk2Result dk2_anim_export(const uint8_t *d, size_t n, const char *outdir,
                          unsigned max_frames, unsigned *written) {
    if (!d || !outdir || n < 32 || memcmp(d, "KMSH", 4) != 0)
        return DK2_ERR_ARGS;
    if (written) *written = 0;
    DK2_MKDIR(outdir);

    const uint8_t *base = d + 12;
    size_t base_n = n - 12;
    /* Walk top-level chunks inside KMSH to find ANIM. */
    Dk2Reader top;
    dk2_reader_init(&top, base, base_n);
    const uint8_t *anim = NULL;
    size_t anim_payload = 0;
    while (top.pos + 8 <= base_n) {
        size_t start = top.pos;
        uint32_t sz;
        dk2_u32le(base + top.pos + 4, &sz);
        if (sz < 8 || sz > base_n - top.pos) return DK2_ERR_CORRUPT;
        if (memcmp(base + top.pos, "ANIM", 4) == 0) {
            anim = base + top.pos + 8;
            anim_payload = sz - 8;
        }
        top.pos = start + sz;
    }
    if (!anim) return DK2_ERR_UNSUPPORTED;

    Dk2Reader h;
    if (!find_chunk(anim, anim_payload, "HEAD", &h)) return DK2_ERR_CORRUPT;
    char name[128];
    if (cstr(&h, name, sizeof(name)) != DK2_OK) return DK2_ERR_CORRUPT;
    uint32_t sprs, frames, indexes, geoms, fff, lod;
    if (dk2_read_u32le(&h, &sprs) != DK2_OK ||
        dk2_read_u32le(&h, &frames) != DK2_OK ||
        dk2_read_u32le(&h, &indexes) != DK2_OK ||
        dk2_read_u32le(&h, &geoms) != DK2_OK ||
        dk2_read_u32le(&h, &fff) != DK2_OK) return DK2_ERR_BOUNDS;
    h.pos += 20; /* pos, cubeScale, scale */
    if (dk2_read_u32le(&h, &lod) != DK2_OK) return DK2_ERR_BOUNDS;
    float scale = 1.0f;
    {
        Dk2Reader hh;
        dk2_reader_init(&hh, anim, anim_payload);
        if (find_chunk(anim, anim_payload, "HEAD", &hh)) {
            char tmp[128];
            cstr(&hh, tmp, sizeof(tmp));
            uint32_t a, b, c, e, f;
            dk2_read_u32le(&hh, &a); dk2_read_u32le(&hh, &b);
            dk2_read_u32le(&hh, &c); dk2_read_u32le(&hh, &e);
            dk2_read_u32le(&hh, &f);
            hh.pos += 12;
            uint32_t bits;
            if (hh.pos + 4 <= hh.size) {
                dk2_u32le(hh.data + hh.pos, &bits);
                memcpy(&scale, &bits, 4);
            }
        }
    }
    if (sprs == 0 || sprs > 4096 || frames == 0 || frames > 4096 ||
        indexes == 0 || indexes > 100000 || geoms == 0 || geoms > 1000000 ||
        lod == 0 || lod > 64) return DK2_ERR_SIZE;
    if (max_frames && max_frames < frames) frames = max_frames;

    /* SPRS container: SPHD headers + per-group POLY/VERT */
    Dk2Reader sp;
    if (!find_chunk(anim, anim_payload, "SPRS", &sp)) {
        return DK2_ERR_BOUNDS;
    }
    /* ITAB */
    Dk2Reader it;
    if (!find_chunk(sp.data, sp.size, "ITAB", &it)) return DK2_ERR_BOUNDS;
    uint32_t chunks = (frames - 1) / 128 + 1;
    uint32_t *itab = (uint32_t *)calloc((size_t)chunks * indexes, sizeof(uint32_t));
    if (!itab) return DK2_ERR_NOMEM;
    for (uint32_t i = 0; i < chunks * indexes; ++i) {
        if (dk2_read_u32le(&it, &itab[i]) != DK2_OK) { free(itab); return DK2_ERR_BOUNDS; }
    }

    /* GEOM: 5 bytes each */
    Dk2Reader gm;
    if (!find_chunk(sp.data, sp.size, "GEOM", &gm)) { free(itab); return DK2_ERR_BOUNDS; }
    float *gx = (float *)calloc(geoms + 1, sizeof(float));
    float *gy = (float *)calloc(geoms + 1, sizeof(float));
    float *gz = (float *)calloc(geoms + 1, sizeof(float));
    uint8_t *fbase = (uint8_t *)calloc(geoms + 1, 1);
    if (!gx || !gy || !gz || !fbase) { free(itab); free(gx); free(gy); free(gz); free(fbase); return DK2_ERR_NOMEM; }
    for (uint32_t i = 0; i < geoms; ++i) {
        if (gm.pos + 5 > gm.size) { free(itab); free(gx); free(gy); free(gz); free(fbase); return DK2_ERR_BOUNDS; }
        uint32_t co;
        dk2_u32le(gm.data + gm.pos, &co);
        fbase[i] = gm.data[gm.pos + 4];
        gm.pos += 5;
        gx[i] = (((int)((co >> 20) & 1023) - 512) / 511.0f) * scale;
        gy[i] = (((int)((co >> 10) & 1023) - 512) / 511.0f) * scale;
        gz[i] = (((int)(co & 1023) - 512) / 511.0f) * scale;
    }
    fbase[geoms] = fbase[geoms - 1];
    gx[geoms] = gx[geoms - 1]; gy[geoms] = gy[geoms - 1]; gz[geoms] = gz[geoms - 1];

    /* VGEO: indexes * frames u8 */
    Dk2Reader vg;
    if (!find_chunk(sp.data, sp.size, "VGEO", &vg)) {
        free(itab); free(gx); free(gy); free(gz); free(fbase);
        return DK2_ERR_BOUNDS;
    }
    if ((size_t)indexes * frames > vg.size) {
        free(itab); free(gx); free(gy); free(gz); free(fbase);
        return DK2_ERR_SIZE;
    }

    uint32_t *tris = (uint32_t *)calloc((size_t)sprs * lod, sizeof(uint32_t));
    uint32_t *vex = (uint32_t *)calloc(sprs, sizeof(uint32_t));
    if (!tris || !vex) { free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_NOMEM; }
    size_t hp = 0;
    for (uint32_t g = 0; g < sprs; ++g) {
        if (hp + 8 + (size_t)lod * 4 + 8 > sp.size ||
            memcmp(sp.data + hp, "SPHD", 4) != 0) {
            free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex);
            return DK2_ERR_CORRUPT;
        }
        hp += 8;
        for (uint32_t l = 0; l < lod; ++l) { dk2_u32le(sp.data + hp, &tris[g * lod + l]); hp += 4; }
        dk2_u32le(sp.data + hp, &vex[g]);
        hp += 8;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame_%04d.obj", outdir, frame);
        FILE *f = fopen(path, "wb");
        if (!f) { free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_IO; }
        fprintf(f, "# DK2 ANIM: %s frame %u/%u\n", name, frame, frames);
        size_t group_pos = hp;
        uint32_t global_vertex = 0;
        for (uint32_t g = 0; g < sprs; ++g) {
            if (group_pos + 12 > sp.size) { fclose(f); free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_BOUNDS; }
            uint32_t gsz;
            dk2_u32le(sp.data + group_pos + 4, &gsz);
            if (gsz < 12 || group_pos + gsz > sp.size) { fclose(f); free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_BOUNDS; }
            Dk2Reader gr;
            dk2_reader_init(&gr, sp.data + group_pos + 8, gsz - 8);
            uint32_t dummy;
            dk2_read_u32le(&gr, &dummy);
            Dk2Reader poly, vert;
            if (!find_chunk(gr.data + 4, gr.size - 4, "POLY", &poly) ||
                !find_chunk(gr.data + 4, gr.size - 4, "VERT", &vert)) {
                fclose(f); free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex);
                return DK2_ERR_CORRUPT;
            }
            if (vex[g] * 18u > vert.size) { fclose(f); free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_BOUNDS; }
            for (uint32_t v = 0; v < vex[g]; ++v) {
                uint16_t raw;
                dk2_u16le(vert.data + v * 18u + 16, &raw);
                uint32_t ti = raw;
                if (ti >= indexes) { fclose(f); free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_CORRUPT; }
                uint32_t base_i = itab[(frame >> 7) * indexes + ti];
                uint8_t off = vg.data[ti * frames + frame];
                uint32_t gi = base_i + off;
                if (gi > geoms) { fclose(f); free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_CORRUPT; }
                float vx = gx[gi], vy = gy[gi], vz = gz[gi];
                int fb = fbase[gi], nfb = fbase[gi + 1];
                if (nfb > fb) {
                    float f = (float)((int)(frame & 0x7Fu) - fb) / (float)(nfb - fb);
                    if (f < 0.0f) f = 0.0f;
                    if (f > 1.0f) f = 1.0f;
                    vx += (gx[gi + 1] - gx[gi]) * f;
                    vy += (gy[gi + 1] - gy[gi]) * f;
                    vz += (gz[gi + 1] - gz[gi]) * f;
                }
                fprintf(f, "v %.6f %.6f %.6f\n", vx, vy, vz);
            }
            size_t pp = 0;
            for (uint32_t l = 0; l < lod; ++l) {
                uint32_t k = tris[g * lod + l];
                if (pp + k * 3 > poly.size) { fclose(f); free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex); return DK2_ERR_BOUNDS; }
                if (l == 0) {
                    for (uint32_t t = 0; t < k; ++t) {
                        unsigned a = poly.data[pp + t * 3], b = poly.data[pp + t * 3 + 1], c = poly.data[pp + t * 3 + 2];
                        if (a < vex[g] && b < vex[g] && c < vex[g])
                            fprintf(f, "f %u %u %u\n", global_vertex + a + 1,
                                    global_vertex + b + 1, global_vertex + c + 1);
                    }
                }
                pp += k * 3;
            }
            global_vertex += vex[g];
            group_pos += gsz;
        }
        fclose(f);
        if (written) ++*written;
    }

    free(itab); free(gx); free(gy); free(gz); free(fbase); free(tris); free(vex);
    return DK2_OK;
}
