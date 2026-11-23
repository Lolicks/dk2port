#include "kmf.h"

#include <stdlib.h>
#include <string.h>

static Dk2Result cstr(Dk2Reader *r, char *out, size_t cap) {
    size_t n = 0;
    while (r->pos < r->size && n + 1 < cap) {
        uint8_t c = r->data[r->pos++];
        if (c == 0) {
            out[n] = '\0';
            return DK2_OK;
        }
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return DK2_ERR_CORRUPT;
}

static void tag_of(const uint8_t *p, char out[5]) {
    memcpy(out, p, 4);
    out[4] = '\0';
}

Dk2Result dk2_kmf_walk(const uint8_t *d, size_t n, Dk2KmfChunk *out,
                       uint32_t cap, uint32_t *count) {
    if (!d || !out || !count || n < 8) return DK2_ERR_ARGS;
    Dk2Reader r;
    dk2_reader_init(&r, d, n);
    uint32_t k = 0;
    while (r.pos + 8 <= n) {
        size_t start = r.pos;
        uint32_t size;
        dk2_u32le(d + r.pos + 4, &size);
        if (size < 8 || size > n - r.pos) return DK2_ERR_CORRUPT;
        if (k >= cap) return DK2_ERR_SIZE;
        tag_of(d + r.pos, out[k].tag);
        out[k].size = size;
        out[k].offset = start;
        ++k;
        r.pos += size;
    }
    if (r.pos != n) return DK2_ERR_CORRUPT;
    *count = k;
    return DK2_OK;
}

/* Nested chunk helper: find a child tag inside a parent payload range. */
static Dk2Result find_chunk(const uint8_t *d, size_t begin, size_t end,
                            const char *tag, Dk2Reader *out) {
    size_t p = begin;
    while (p + 8 <= end) {
        uint32_t sz;
        dk2_u32le(d + p + 4, &sz);
        if (sz < 8 || p + sz > end) return DK2_ERR_CORRUPT;
        if (memcmp(d + p, tag, 4) == 0) {
            dk2_reader_init(out, d + p + 8, sz - 8);
            return DK2_OK;
        }
        p += sz;
    }
    return DK2_ERR_BOUNDS;
}

static Dk2Result parse_mat2(Dk2Reader *r, Dk2KmfInfo *info) {
    char name[128];
    Dk2Result rc = cstr(r, name, sizeof(name));
    if (rc != DK2_OK) return rc;
    uint32_t ntex = 0;
    rc = dk2_read_u32le(r, &ntex);
    if (rc != DK2_OK || ntex > 64) return DK2_ERR_CORRUPT;
    for (uint32_t i = 0; i < ntex; ++i) {
        char tex[128];
        rc = cstr(r, tex, sizeof(tex));
        if (rc != DK2_OK) return rc;
        if (info && info->texture_count < DK2_KMF_MAX_TEXTURES) {
            snprintf(info->textures[info->texture_count],
                     sizeof(info->textures[0]), "%s", tex);
            ++info->texture_count;
        }
    }
    if (r->pos + 14 > r->size) return DK2_ERR_BOUNDS;
    r->pos += 1; /* flags */
    r->pos += 4; /* brightness */
    r->pos += 4; /* gamma */
    (void)cstr(r, name, sizeof(name)); /* envmap */
    return DK2_OK;
}

static Dk2Result parse_matl(const uint8_t *d, size_t begin, size_t end,
                            Dk2KmfInfo *info) {
    Dk2Reader m;
    Dk2Result rc = find_chunk(d, begin, end, "MATL", &m);
    if (rc != DK2_OK) return rc;
    uint32_t n = 0;
    rc = dk2_read_u32le(&m, &n);
    if (rc != DK2_OK || n > 4096) return DK2_ERR_CORRUPT;
    size_t p = m.pos;
    const uint8_t *base = m.data;
    for (uint32_t i = 0; i < n; ++i) {
        if (p + 8 > m.size) return DK2_ERR_BOUNDS;
        uint32_t sz;
        dk2_u32le(base + p + 4, &sz);
        if (sz < 8 || p + sz > m.size || memcmp(base + p, "MAT2", 4) != 0)
            return DK2_ERR_CORRUPT;
        Dk2Reader sub;
        dk2_reader_init(&sub, base + p + 8, sz - 8);
        rc = parse_mat2(&sub, info);
        if (rc != DK2_OK) return rc;
        p += sz;
    }
    if (info) info->material_count = n;
    return DK2_OK;
}

static Dk2Result parse_sprs(const uint8_t *d, size_t begin, size_t end,
                            Dk2KmfInfo *info, FILE *obj,
                            const float *verts, uint32_t vert_count,
                            uint32_t type, uint32_t *vt_base) {
    (void)verts; (void)vert_count;
    Dk2Reader sp;
    Dk2Result rc = find_chunk(d, begin, end, "SPRS", &sp);
    if (rc != DK2_OK) return rc;

    const uint8_t *base = sp.data;
    size_t total = sp.size;
    uint32_t groups = info ? info->sprite_groups : 0;
    uint32_t lod = info ? info->lod_count : 0;
    if (groups == 0 || groups > 8192 || lod == 0 || lod > 64)
        return DK2_ERR_CORRUPT;

    uint32_t *tri_counts = (uint32_t *)calloc((size_t)groups * lod, sizeof(uint32_t));
    uint32_t *num_ex = (uint32_t *)calloc(groups, sizeof(uint32_t));
    if (!tri_counts || !num_ex) { free(tri_counts); free(num_ex); return DK2_ERR_NOMEM; }

    size_t hp = 0;
    for (uint32_t g = 0; g < groups; ++g) {
        if (hp + 8 + (size_t)lod * 4 + 8 > total ||
            memcmp(base + hp, "SPHD", 4) != 0) {
            free(tri_counts); free(num_ex); return DK2_ERR_CORRUPT;
        }
        hp += 8;
        for (uint32_t l = 0; l < lod; ++l) {
            dk2_u32le(base + hp, &tri_counts[g * lod + l]);
            hp += 4;
        }
        dk2_u32le(base + hp, &num_ex[g]);
        hp += 8;
    }
    uint32_t total_ex = 0;
    for (uint32_t g = 0; g < groups; ++g) total_ex += num_ex[g];

    uint16_t *all_geom = NULL;
    float *all_uv = NULL;
    if (obj && type == 1) {
        all_geom = (uint16_t *)calloc(total_ex ? total_ex : 1, sizeof(uint16_t));
        all_uv = (float *)calloc((total_ex ? total_ex : 1) * 2u, sizeof(float));
        if (!all_geom || !all_uv) {
            free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
            return DK2_ERR_NOMEM;
        }
    }

    for (uint32_t g = 0; g < groups; ++g) {
        if (hp + 12 > total || memcmp(base + hp, "SPRS", 4) != 0) {
            free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
            return DK2_ERR_CORRUPT;
        }
        uint32_t gsz;
        dk2_u32le(base + hp + 4, &gsz);
        if (gsz < 12 || hp + gsz > total) {
            free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
            return DK2_ERR_BOUNDS;
        }
        Dk2Reader gr;
        dk2_reader_init(&gr, base + hp + 8, gsz - 8);
        uint32_t mat_index;
        if (dk2_read_u32le(&gr, &mat_index) != DK2_OK) {
            free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
            return DK2_ERR_BOUNDS;
        }

        if (type == 2) {
            if (gr.pos + 8 > gr.size || memcmp(gr.data + gr.pos, "POLY", 4) != 0) {
                free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
                return DK2_ERR_CORRUPT;
            }
            uint32_t psz;
            dk2_u32le(gr.data + gr.pos + 4, &psz);
            if (psz < 8 || gr.pos + psz > gr.size) {
                free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
                return DK2_ERR_BOUNDS;
            }
            gr.pos += 8;
        }

        if (obj && all_geom && type == 1) {
            for (uint32_t v = 0; v < num_ex[g]; ++v) {
                uint16_t raw;
                dk2_u16le(gr.data + gr.pos + v * 18u, &raw);
                int16_t gi = (int16_t)raw;
                dk2_u16le(gr.data + gr.pos + v * 18u + 2, &raw);
                int16_t uu = (int16_t)raw;
                dk2_u16le(gr.data + gr.pos + v * 18u + 4, &raw);
                int16_t vv = (int16_t)raw;
                uint32_t idx = (vt_base ? *vt_base : 0) + v;
                all_geom[idx] = gi < 0 ? 0 : (uint16_t)gi;
                all_uv[idx * 2] = (float)uu / 1024.0f;
                all_uv[idx * 2 + 1] = (float)vv / 1024.0f;
                fprintf(obj, "vt %.6f %.6f\n", all_uv[idx * 2], all_uv[idx * 2 + 1]);
            }
        }
        for (uint32_t l = 0; l < lod; ++l) {
            uint32_t k = tri_counts[g * lod + l];
            if (k > 200000) {
                free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
                return DK2_ERR_CORRUPT;
            }
            for (uint32_t t = 0; t < k; ++t) {
                if (gr.pos + 3 > gr.size) {
                    free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
                    return DK2_ERR_BOUNDS;
                }
                unsigned a = gr.data[gr.pos], b = gr.data[gr.pos + 1],
                         c = gr.data[gr.pos + 2];
                gr.pos += 3;
                if (info) {
                    ++info->triangle_count;
                    if (l == 0) ++info->lod0_triangles;
                }
                if (obj && a < vert_count && b < vert_count && c < vert_count)
                    fprintf(obj, "f %u %u %u\n", a + 1, b + 1, c + 1);
            }
        }

        if (type == 2 && gr.pos + 8 <= gr.size &&
            memcmp(gr.data + gr.pos, "VERT", 4) == 0)
            gr.pos += 8;
        if (gr.pos + (size_t)num_ex[g] * 18u > gr.size) {
            free(tri_counts); free(num_ex); free(all_geom); free(all_uv);
            return DK2_ERR_BOUNDS;
        }

        info->extended_count += num_ex[g];
        gr.pos += (size_t)num_ex[g] * 18u;
        if (vt_base && obj) *vt_base += num_ex[g];
        hp += gsz;
    }
    free(tri_counts);
    free(num_ex);
    free(all_geom);
    free(all_uv);
    return DK2_OK;
}

Dk2Result dk2_kmf_info(const uint8_t *d, size_t n, Dk2KmfInfo *info) {
    if (!d || !info || n < 28) return DK2_ERR_ARGS;
    memset(info, 0, sizeof(*info));
    if (memcmp(d, "KMSH", 4) != 0) return DK2_ERR_MAGIC;
    dk2_u32le(d + 8, &info->version);
    if (info->version != 17) return DK2_ERR_VERSION;

    const uint8_t *base = d + 12;      /* inside the KMSH chunk */
    size_t base_n = n - 12;
    Dk2KmfChunk chunks[DK2_KMF_MAX_CHUNKS];
    uint32_t count = 0;
    Dk2Result rc = dk2_kmf_walk(base, base_n, chunks, DK2_KMF_MAX_CHUNKS, &count);
    if (rc != DK2_OK) return rc;
    info->chunk_count = count;

    /* HEAD is a top-level chunk. */
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(chunks[i].tag, "HEAD") != 0) continue;
        Dk2Reader h;
        dk2_reader_init(&h, base + chunks[i].offset + 8, chunks[i].size - 8);
        rc = dk2_read_u32le(&h, &info->type);
        if (rc != DK2_OK) return rc;
        uint32_t uk;
        rc = dk2_read_u32le(&h, &uk);
        if (rc != DK2_OK) return rc;
        break;
    }
    if (info->type < 1 || info->type > 3) return DK2_ERR_CORRUPT;

    rc = parse_matl(base, 0, base_n, info);
    if (rc != DK2_OK && rc != DK2_ERR_BOUNDS) return rc;

    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(chunks[i].tag, "MESH") == 0 ||
            strcmp(chunks[i].tag, "ANIM") == 0 ||
            strcmp(chunks[i].tag, "GROP") == 0) {
            size_t begin = chunks[i].offset + 8;
            size_t end = chunks[i].offset + chunks[i].size;
            Dk2Reader h;
            rc = find_chunk(base, begin, end, "HEAD", &h);
            if (rc != DK2_OK) return rc;
            if (info->type == 3) {
                uint32_t elements = 0;
                if (h.pos + 4 <= h.size) dk2_u32le(h.data + h.pos, &elements);
                info->sprite_groups = elements;
                break;
            }
            if (strcmp(chunks[i].tag, "MESH") == 0) {
                (void)cstr(&h, info->mesh_name, sizeof(info->mesh_name));
                dk2_read_u32le(&h, &info->sprite_groups);
                dk2_read_u32le(&h, &info->vertex_count);
                if (h.pos + 16 > h.size) return DK2_ERR_BOUNDS;
                h.pos += 16; /* Vec3f pos + float scale */
                dk2_read_u32le(&h, &info->lod_count);
            } else {
                char tmp_name[128];
                if (cstr(&h, tmp_name, sizeof(tmp_name)) != DK2_OK) return DK2_ERR_CORRUPT;
                dk2_read_u32le(&h, &info->sprite_groups);
                uint32_t frames, indexes, geoms, fff;
                dk2_read_u32le(&h, &frames);
                dk2_read_u32le(&h, &indexes);
                dk2_read_u32le(&h, &geoms);
                dk2_read_u32le(&h, &fff);
                if (h.pos + 24 > h.size) return DK2_ERR_BOUNDS;
                h.pos += 20; /* pos(12) + cubeScale(4) + scale(4) */
                dk2_read_u32le(&h, &info->lod_count);
            }
            (void)parse_sprs(base, begin, end, info, NULL, NULL, 0, info->type, NULL);
            break;
        }
    }
    return DK2_OK;
}

Dk2Result dk2_kmf_export_obj(const uint8_t *d, size_t n, FILE *out) {
    Dk2KmfInfo info;
    Dk2Result rc = dk2_kmf_info(d, n, &info);
    if (rc != DK2_OK) return rc;
    if (info.type != 1) return DK2_ERR_UNSUPPORTED;

    const uint8_t *base = d + 12;
    size_t base_n = n - 12;
    Dk2KmfChunk chunks[DK2_KMF_MAX_CHUNKS];
    uint32_t count;
    rc = dk2_kmf_walk(base, base_n, chunks, DK2_KMF_MAX_CHUNKS, &count);
    if (rc != DK2_OK) return rc;
    for (uint32_t i = 0; i < count; ++i) {
        if (strcmp(chunks[i].tag, "MESH") != 0) continue;
        size_t begin = chunks[i].offset + 8, end = chunks[i].offset + chunks[i].size;
        Dk2Reader g;
        rc = find_chunk(base, begin, end, "GEOM", &g);
        if (rc != DK2_OK) return rc;
        fprintf(out, "# DK2 KMF: %s\n", info.mesh_name);
        for (uint32_t v = 0; v < info.vertex_count; ++v) {
            if (g.pos + 12 > g.size) return DK2_ERR_BOUNDS;
            float x, y, z;
            uint32_t bits;
            dk2_u32le(g.data + g.pos, &bits); memcpy(&x, &bits, 4); g.pos += 4;
            dk2_u32le(g.data + g.pos, &bits); memcpy(&y, &bits, 4); g.pos += 4;
            dk2_u32le(g.data + g.pos, &bits); memcpy(&z, &bits, 4); g.pos += 4;
            fprintf(out, "v %.6f %.6f %.6f\n", x, y, z);
        }
        uint32_t vt_base = 0;
        return parse_sprs(base, begin, end, &info, out, (const float *)g.data,
                          info.vertex_count, info.type, &vt_base);
    }
    return DK2_ERR_BOUNDS;
}
