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

/* Flat, group-ordered extended vertex exported from VERT (18-byte records). */
typedef struct {
    uint16_t u;
    uint16_t v;
    float nx, ny, nz;
    uint16_t itab_index;
    uint32_t sprite;
    float base_x, base_y, base_z;
} Dk2AnimVertex;

typedef struct {
    char name[128];
    uint32_t sprites;
    uint32_t frames;
    uint32_t indexes;
    uint32_t geoms;
    uint32_t frame_factor;
    float origin[3];
    float cube_scale;
    float scale;
    uint32_t lod;

    uint32_t *itab;
    uint8_t *vgeo;
    float *gx, *gy, *gz;
    uint8_t *fbase;
    uint32_t *tri_counts;
    uint32_t *vex;
    uint32_t *mat_index;

    Dk2AnimVertex *verts;
    uint32_t vertex_count;
    uint16_t *indices;
    uint32_t index_count;
} Dk2AnimModel;

static void anim_free(Dk2AnimModel *m) {
    free(m->itab); free(m->vgeo);
    free(m->gx); free(m->gy); free(m->gz); free(m->fbase);
    free(m->tri_counts); free(m->vex); free(m->mat_index);
    free(m->verts); free(m->indices);
    memset(m, 0, sizeof(*m));
}

static void decode_geom(const Dk2AnimModel *m, uint32_t gi,
                        float *x, float *y, float *z) {
    *x = m->gx[gi];
    *y = m->gy[gi];
    *z = m->gz[gi];
}

/* One morph sample, the C counterpart of OpenKeeper's FrameInfo. */
typedef struct {
    float prev[3], next[3];
    float weight;
    uint16_t prev_pose, next_pose;
} Dk2Pose;

static void pose_at(const Dk2AnimModel *m, uint32_t frame, uint32_t vi,
                    Dk2Pose *out) {
    const Dk2AnimVertex *av = &m->verts[vi];
    uint32_t block = frame >> 7;
    uint32_t ti = av->itab_index;
    uint32_t base = m->itab[block * m->indexes + ti];
    uint32_t off = m->vgeo[(size_t)ti * m->frames + frame];
    uint32_t gi = base + off;
    if (gi > m->geoms) gi = m->geoms; /* GEOM[] has one synthetic tail copy */

    decode_geom(m, gi, &out->prev[0], &out->prev[1], &out->prev[2]);
    decode_geom(m, gi + 1, &out->next[0], &out->next[1], &out->next[2]);

    int fb = m->fbase[gi], nfb = m->fbase[gi + 1];
    float w = 0.0f;
    if (nfb != fb) {
        w = (float)(((int)(frame & 0x7Fu)) - fb) / (float)(nfb - fb);
        if (w < 0.0f) w = 0.0f;
        if (w > 1.0f) w = 1.0f;
    }
    out->weight = w;
    out->prev_pose = (uint16_t)(block * 128u + (uint32_t)(fb < 0 ? 0 : fb));
    out->next_pose = (uint16_t)(block * 128u + (uint32_t)(nfb < 0 ? 0 : nfb));
}

static Dk2Result anim_load(const uint8_t *d, size_t n, Dk2AnimModel *m) {
    memset(m, 0, sizeof(*m));
    if (!d || n < 32 || memcmp(d, "KMSH", 4) != 0) return DK2_ERR_ARGS;
    const uint8_t *base = d + 12;
    size_t base_n = n - 12;

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
    if (cstr(&h, m->name, sizeof(m->name)) != DK2_OK) return DK2_ERR_CORRUPT;
    if (dk2_read_u32le(&h, &m->sprites) != DK2_OK ||
        dk2_read_u32le(&h, &m->frames) != DK2_OK ||
        dk2_read_u32le(&h, &m->indexes) != DK2_OK ||
        dk2_read_u32le(&h, &m->geoms) != DK2_OK ||
        dk2_read_u32le(&h, &m->frame_factor) != DK2_OK) return DK2_ERR_BOUNDS;
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t bits;
        if (dk2_read_u32le(&h, &bits) != DK2_OK) return DK2_ERR_BOUNDS;
        memcpy(&m->origin[i], &bits, 4);
    }
    uint32_t bits;
    if (dk2_read_u32le(&h, &bits) != DK2_OK) return DK2_ERR_BOUNDS;
    memcpy(&m->cube_scale, &bits, 4);
    if (dk2_read_u32le(&h, &bits) != DK2_OK) return DK2_ERR_BOUNDS;
    memcpy(&m->scale, &bits, 4);
    if (dk2_read_u32le(&h, &m->lod) != DK2_OK) return DK2_ERR_BOUNDS;

    if (m->sprites == 0 || m->sprites > 4096 || m->frames == 0 ||
        m->frames > 4096 || m->indexes == 0 || m->indexes > 100000 ||
        m->geoms == 0 || m->geoms > 1000000 || m->lod == 0 || m->lod > 64)
        return DK2_ERR_SIZE;

    Dk2Reader sp;
    if (!find_chunk(anim, anim_payload, "SPRS", &sp)) return DK2_ERR_BOUNDS;
    Dk2Reader it;
    if (!find_chunk(sp.data, sp.size, "ITAB", &it)) return DK2_ERR_BOUNDS;
    uint32_t chunks = (m->frames - 1) / 128u + 1u;
    m->itab = (uint32_t *)calloc((size_t)chunks * m->indexes, sizeof(uint32_t));
    if (!m->itab) return DK2_ERR_NOMEM;
    for (uint32_t i = 0; i < chunks * m->indexes; ++i) {
        if (dk2_read_u32le(&it, &m->itab[i]) != DK2_OK) return DK2_ERR_BOUNDS;
    }

    Dk2Reader gm;
    if (!find_chunk(sp.data, sp.size, "GEOM", &gm)) return DK2_ERR_BOUNDS;
    m->gx = (float *)calloc((size_t)m->geoms + 1, sizeof(float));
    m->gy = (float *)calloc((size_t)m->geoms + 1, sizeof(float));
    m->gz = (float *)calloc((size_t)m->geoms + 1, sizeof(float));
    m->fbase = (uint8_t *)calloc((size_t)m->geoms + 1, 1);
    if (!m->gx || !m->gy || !m->gz || !m->fbase) return DK2_ERR_NOMEM;
    for (uint32_t i = 0; i < m->geoms; ++i) {
        if (gm.pos + 5 > gm.size) return DK2_ERR_BOUNDS;
        uint32_t co;
        dk2_u32le(gm.data + gm.pos, &co);
        m->fbase[i] = gm.data[gm.pos + 4];
        gm.pos += 5;
        m->gx[i] = (((int)((co >> 20) & 1023) - 512) / 511.0f) * m->scale;
        m->gy[i] = (((int)((co >> 10) & 1023) - 512) / 511.0f) * m->scale;
        m->gz[i] = (((int)(co & 1023) - 512) / 511.0f) * m->scale;
    }
    m->fbase[m->geoms] = m->fbase[m->geoms - 1];
    m->gx[m->geoms] = m->gx[m->geoms - 1];
    m->gy[m->geoms] = m->gy[m->geoms - 1];
    m->gz[m->geoms] = m->gz[m->geoms - 1];

    Dk2Reader vg;
    if (!find_chunk(sp.data, sp.size, "VGEO", &vg)) return DK2_ERR_BOUNDS;
    if ((size_t)m->indexes * m->frames > vg.size) return DK2_ERR_SIZE;
    m->vgeo = (uint8_t *)malloc((size_t)m->indexes * m->frames);
    if (!m->vgeo) return DK2_ERR_NOMEM;
    memcpy(m->vgeo, vg.data, (size_t)m->indexes * m->frames);

    m->tri_counts = (uint32_t *)calloc((size_t)m->sprites * m->lod, sizeof(uint32_t));
    m->vex = (uint32_t *)calloc(m->sprites, sizeof(uint32_t));
    m->mat_index = (uint32_t *)calloc(m->sprites, sizeof(uint32_t));
    if (!m->tri_counts || !m->vex || !m->mat_index) return DK2_ERR_NOMEM;
    size_t hp = 0;
    for (uint32_t g = 0; g < m->sprites; ++g) {
        if (hp + 8 + (size_t)m->lod * 4 + 8 > sp.size ||
            memcmp(sp.data + hp, "SPHD", 4) != 0) return DK2_ERR_CORRUPT;
        hp += 8;
        for (uint32_t l = 0; l < m->lod; ++l) {
            dk2_u32le(sp.data + hp, &m->tri_counts[g * m->lod + l]);
            hp += 4;
        }
        dk2_u32le(sp.data + hp, &m->vex[g]);
        hp += 8;
    }

    uint32_t total_verts = 0, total_idx = 0;
    for (uint32_t g = 0; g < m->sprites; ++g) {
        total_verts += m->vex[g];
        total_idx += m->tri_counts[g * m->lod] * 3u;
    }
    if (total_verts == 0 || total_verts > 65535 || total_idx > 6000000)
        return DK2_ERR_SIZE;
    m->verts = (Dk2AnimVertex *)calloc(total_verts, sizeof(Dk2AnimVertex));
    m->indices = (uint16_t *)calloc(total_idx ? total_idx : 1, sizeof(uint16_t));
    if (!m->verts || !m->indices) return DK2_ERR_NOMEM;
    m->vertex_count = total_verts;
    m->index_count = total_idx;

    uint32_t vbase = 0, ibase = 0;
    for (uint32_t g = 0; g < m->sprites; ++g) {
        if (hp + 12 > sp.size || memcmp(sp.data + hp, "SPRS", 4) != 0)
            return DK2_ERR_CORRUPT;
        uint32_t gsz;
        dk2_u32le(sp.data + hp + 4, &gsz);
        if (gsz < 12 || hp + gsz > sp.size) return DK2_ERR_BOUNDS;
        Dk2Reader gr;
        dk2_reader_init(&gr, sp.data + hp + 8, gsz - 8);
        if (dk2_read_u32le(&gr, &m->mat_index[g]) != DK2_OK) return DK2_ERR_BOUNDS;
        Dk2Reader poly, vert;
        if (!find_chunk(gr.data + 4, gr.size - 4, "POLY", &poly) ||
            !find_chunk(gr.data + 4, gr.size - 4, "VERT", &vert))
            return DK2_ERR_CORRUPT;
        if (m->tri_counts[g * m->lod] * 3u > poly.size) return DK2_ERR_BOUNDS;
        for (uint32_t t = 0; t < m->tri_counts[g * m->lod] * 3u; ++t) {
            uint32_t local = poly.data[t];
            if (local >= m->vex[g]) return DK2_ERR_CORRUPT;
            m->indices[ibase + t] = (uint16_t)(vbase + local);
        }
        ibase += m->tri_counts[g * m->lod] * 3u;

        if (m->vex[g] * 18u > vert.size) return DK2_ERR_BOUNDS;
        for (uint32_t v = 0; v < m->vex[g]; ++v) {
            const uint8_t *rec = vert.data + (size_t)v * 18u;
            Dk2AnimVertex *av = &m->verts[vbase + v];
            uint16_t raw;
            dk2_u16le(rec, &raw); av->u = raw;
            dk2_u16le(rec + 2, &raw); av->v = raw;
            uint32_t bits;
            dk2_u32le(rec + 4, &bits); memcpy(&av->nx, &bits, 4);
            dk2_u32le(rec + 8, &bits); memcpy(&av->ny, &bits, 4);
            dk2_u32le(rec + 12, &bits); memcpy(&av->nz, &bits, 4);
            dk2_u16le(rec + 16, &raw); av->itab_index = raw;
            av->sprite = g;
            if (av->itab_index >= m->indexes) return DK2_ERR_CORRUPT;
            Dk2Pose p;
            pose_at(m, 0, vbase + v, &p);
            av->base_x = p.prev[0];
            av->base_y = p.prev[1];
            av->base_z = p.prev[2];
        }
        vbase += m->vex[g];
        hp += gsz;
    }
    return DK2_OK;
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static void put_f32(uint8_t *p, float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    put_u32(p, bits);
}

Dk2Result dk2_anim_export_dkaf(const uint8_t *d, size_t n, const char *path,
                               Dk2DkafHeader *header) {
    Dk2AnimModel m;
    Dk2Result rc = anim_load(d, n, &m);
    if (rc != DK2_OK) return rc;
    if (!path) { anim_free(&m); return DK2_ERR_ARGS; }

    FILE *f = fopen(path, "wb");
    if (!f) { anim_free(&m); return DK2_ERR_IO; }
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    put_u32(buf, DK2_DKAF_MAGIC);
    put_u32(buf + 4, DK2_DKAF_VERSION);
    put_u32(buf + 8, DK2_ANIM_FPS);
    put_u32(buf + 12, m.frames);
    put_u32(buf + 16, m.vertex_count);
    put_u32(buf + 20, m.index_count);
    put_u32(buf + 24, m.frames * m.vertex_count);
    put_f32(buf + 28, m.scale);
    put_f32(buf + 32, m.origin[0]);
    put_f32(buf + 36, m.origin[1]);
    put_f32(buf + 40, m.origin[2]);
    put_f32(buf + 44, m.cube_scale);
    put_u32(buf + 48, m.frame_factor);
    if (fwrite(buf, 1, DK2_DKAF_HEADER_BYTES, f) != DK2_DKAF_HEADER_BYTES) {
        rc = DK2_ERR_IO; goto done;
    }

    for (uint32_t i = 0; i < m.vertex_count; ++i) {
        const Dk2AnimVertex *av = &m.verts[i];
        memset(buf, 0, DK2_DKAF_VERTEX_BYTES);
        put_f32(buf, av->base_x);
        put_f32(buf + 4, av->base_y);
        put_f32(buf + 8, av->base_z);
        put_f32(buf + 12, av->nx);
        put_f32(buf + 16, av->ny);
        put_f32(buf + 20, av->nz);
        put_f32(buf + 24, (float)av->u / 32768.0f);
        put_f32(buf + 28, (float)av->v / 32768.0f);
        put_u16(buf + 32, av->itab_index);
        put_u16(buf + 34, (uint16_t)av->sprite);
        if (fwrite(buf, 1, DK2_DKAF_VERTEX_BYTES, f) != DK2_DKAF_VERTEX_BYTES) {
            rc = DK2_ERR_IO; goto done;
        }
    }
    for (uint32_t i = 0; i < m.index_count; ++i) {
        put_u16(buf, m.indices[i]);
        if (fwrite(buf, 1, 2, f) != 2) { rc = DK2_ERR_IO; goto done; }
    }
    for (uint32_t frame = 0; frame < m.frames; ++frame) {
        for (uint32_t vi = 0; vi < m.vertex_count; ++vi) {
            Dk2Pose p;
            pose_at(&m, frame, vi, &p);
            memset(buf, 0, DK2_DKAF_POSE_BYTES);
            put_f32(buf, p.prev[0]);
            put_f32(buf + 4, p.prev[1]);
            put_f32(buf + 8, p.prev[2]);
            put_f32(buf + 12, p.next[0]);
            put_f32(buf + 16, p.next[1]);
            put_f32(buf + 20, p.next[2]);
            put_f32(buf + 24, p.weight);
            put_u16(buf + 28, p.prev_pose);
            put_u16(buf + 30, p.next_pose);
            if (fwrite(buf, 1, DK2_DKAF_POSE_BYTES, f) != DK2_DKAF_POSE_BYTES) {
                rc = DK2_ERR_IO; goto done;
            }
        }
    }
    rc = DK2_OK;
done:
    fclose(f);
    if (header && rc == DK2_OK) {
        header->fps = DK2_ANIM_FPS;
        header->frame_count = m.frames;
        header->vertex_count = m.vertex_count;
        header->index_count = m.index_count;
        header->pose_count = m.frames * m.vertex_count;
        header->scale = m.scale;
        memcpy(header->origin, m.origin, sizeof(header->origin));
        header->cube_scale = m.cube_scale;
        header->frame_factor = m.frame_factor;
    }
    anim_free(&m);
    return rc;
}

Dk2Result dk2_anim_export(const uint8_t *d, size_t n, const char *outdir,
                          unsigned max_frames, unsigned *written) {
    Dk2AnimModel m;
    Dk2Result rc = anim_load(d, n, &m);
    if (rc != DK2_OK) return rc;
    if (!outdir) { anim_free(&m); return DK2_ERR_ARGS; }
    if (written) *written = 0;
    DK2_MKDIR(outdir);
    uint32_t frames = m.frames;
    if (max_frames && max_frames < frames) frames = max_frames;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame_%04d.obj", outdir, frame);
        FILE *f = fopen(path, "wb");
        if (!f) { anim_free(&m); return DK2_ERR_IO; }
        fprintf(f, "# DK2 ANIM: %s frame %u/%u fps=%u\n",
                m.name, frame, m.frames, DK2_ANIM_FPS);
        fprintf(f, "# DK2_ANIM_HEAD scale=%.6f origin=%.6f,%.6f,%.6f cube=%.6f factor=%u\n",
                m.scale, m.origin[0], m.origin[1], m.origin[2],
                m.cube_scale, m.frame_factor);
        fprintf(f, "# DK2_POSES count=%u (morph weights live in DKAF v2)\n",
                m.vertex_count);

        /* Bind-pose vertices followed by VERT UVs and normals. */
        for (uint32_t i = 0; i < m.vertex_count; ++i) {
            Dk2Pose p;
            pose_at(&m, frame, i, &p);
            float x = p.prev[0] + (p.next[0] - p.prev[0]) * p.weight;
            float y = p.prev[1] + (p.next[1] - p.prev[1]) * p.weight;
            float z = p.prev[2] + (p.next[2] - p.prev[2]) * p.weight;
            fprintf(f, "v %.6f %.6f %.6f\n", x, y, z);
        }
        for (uint32_t i = 0; i < m.vertex_count; ++i) {
            fprintf(f, "vt %.6f %.6f\n",
                    (double)m.verts[i].u / 32768.0,
                    (double)m.verts[i].v / 32768.0);
        }
        for (uint32_t i = 0; i < m.vertex_count; ++i) {
            fprintf(f, "vn %.6f %.6f %.6f\n",
                    m.verts[i].nx, m.verts[i].ny, m.verts[i].nz);
        }
        uint32_t current_sprite = 0xffffffffu;
        for (uint32_t i = 0; i < m.index_count; i += 3) {
            unsigned a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
            uint32_t sprite = m.verts[a].sprite;
            if (sprite != current_sprite) {
                fprintf(f, "usemtl mat_%u\n", m.mat_index[sprite]);
                current_sprite = sprite;
            }
            fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
                    a + 1, a + 1, a + 1,
                    b + 1, b + 1, b + 1,
                    c + 1, c + 1, c + 1);
        }
        fputs("# DK2_MATERIAL_MANIFEST ", f);
        (void)dk2_kmf_write_manifest(d, n, f);
        fputc('\n', f);
        fclose(f);
        if (written) ++*written;
    }
    anim_free(&m);
    return DK2_OK;
}
