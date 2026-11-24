#include "kmf.h"
#include "anim.h"

#include <stdlib.h>
#include <string.h>

#define DK2_KMF_MAX_MATERIALS 4096u
#define DK2_KMF_MAT_TEX 16u

/* Per-material data recovered from MATL/MAT2. The public Dk2KmfInfo keeps the
   historical flat texture list; this table preserves the material -> texture
   relationship needed by the OBJ `usemtl` directives and the JSON sidecar. */
typedef struct {
    char name[128];
    char textures[DK2_KMF_MAT_TEX][128];
    uint32_t texture_count;
    uint32_t flags;
} Dk2Material;

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

static void add_texture(Dk2Material *mat, Dk2KmfInfo *info, const char *tex) {
    if (mat && mat->texture_count < DK2_KMF_MAT_TEX) {
        snprintf(mat->textures[mat->texture_count],
                 sizeof(mat->textures[0]), "%s", tex);
        ++mat->texture_count;
    }
    if (info && info->texture_count < DK2_KMF_MAX_TEXTURES) {
        snprintf(info->textures[info->texture_count],
                 sizeof(info->textures[0]), "%s", tex);
        ++info->texture_count;
    }
}

/* MAT2 body: name, numTextures, texture names, flags, brightness, gamma,
   envmap.  Flags are a u32 (the old code skipped a single byte before the
   brightness and never validated the envmap range). */
static Dk2Result parse_mat2(Dk2Reader *r, Dk2Material *mat, Dk2KmfInfo *info) {
    char name[128];
    Dk2Result rc = cstr(r, name, sizeof(name));
    if (rc != DK2_OK) return rc;
    if (mat) snprintf(mat->name, sizeof(mat->name), "%s", name);

    uint32_t ntex = 0;
    rc = dk2_read_u32le(r, &ntex);
    if (rc != DK2_OK || ntex > 64) return DK2_ERR_CORRUPT;
    for (uint32_t i = 0; i < ntex; ++i) {
        char tex[128];
        rc = cstr(r, tex, sizeof(tex));
        if (rc != DK2_OK) return rc;
        add_texture(mat, info, tex);
    }
    if (r->pos + 12 > r->size) return DK2_ERR_BOUNDS;
    uint32_t flags;
    dk2_u32le(r->data + r->pos, &flags);
    r->pos += 4; /* flags */
    r->pos += 4; /* brightness */
    r->pos += 4; /* gamma / shininess */
    if (mat) mat->flags = flags;
    char envmap[128];
    return cstr(r, envmap, sizeof(envmap));
}

/* Collect every MAT2 into a per-material table.  `info` may be NULL for a
   manifest-only call; when supplied its flat texture list is also filled. */
static Dk2Result collect_materials(const uint8_t *base, size_t base_n,
                                   Dk2KmfInfo *info,
                                   Dk2Material **out, uint32_t *out_count) {
    *out = NULL;
    *out_count = 0;
    Dk2KmfChunk chunks[DK2_KMF_MAX_CHUNKS];
    uint32_t count = 0;
    Dk2Result rc = dk2_kmf_walk(base, base_n, chunks, DK2_KMF_MAX_CHUNKS, &count);
    if (rc != DK2_OK) return rc;

    Dk2Reader m;
    rc = find_chunk(base, 0, base_n, "MATL", &m);
    if (rc == DK2_ERR_BOUNDS) return DK2_OK; /* GROP has no MATL */
    if (rc != DK2_OK) return rc;

    uint32_t n = 0;
    rc = dk2_read_u32le(&m, &n);
    if (rc != DK2_OK || n > DK2_KMF_MAX_MATERIALS) return DK2_ERR_CORRUPT;
    Dk2Material *mats = (Dk2Material *)calloc(n ? n : 1, sizeof(Dk2Material));
    if (!mats) return DK2_ERR_NOMEM;

    size_t p = m.pos;
    const uint8_t *data = m.data;
    for (uint32_t i = 0; i < n; ++i) {
        if (p + 8 > m.size) { rc = DK2_ERR_BOUNDS; goto fail; }
        uint32_t sz;
        dk2_u32le(data + p + 4, &sz);
        if (sz < 8 || p + sz > m.size || memcmp(data + p, "MAT2", 4) != 0) {
            rc = DK2_ERR_CORRUPT;
            goto fail;
        }
        Dk2Reader sub;
        dk2_reader_init(&sub, data + p + 8, sz - 8);
        rc = parse_mat2(&sub, &mats[i], info);
        if (rc != DK2_OK) goto fail;
        p += sz;
    }
    if (info) info->material_count = n;
    *out = mats;
    *out_count = n;
    return DK2_OK;
fail:
    free(mats);
    return rc;
}

/* ------------------------------------------------------------------------ */
/* SPRS traversal.                                                           */
/*                                                                          */
/* LOD policy: OpenKeeper's KmfModelLoader explicitly says "LODs are broken, */
/* take L0" (handleMesh :209-210/237-241, handleAnim :288-291/462-466).      */
/* Only LOD0 triangles are counted into `triangle_count` and emitted as `f`  */
/* lines.  Later LODs are still walked (to reach the extended vertices) but  */
/* are never exported.                                                       */
/* ------------------------------------------------------------------------ */
static Dk2Result parse_sprs(const uint8_t *d, size_t begin, size_t end,
                            Dk2KmfInfo *info, FILE *obj,
                            const float *verts, uint32_t vert_count,
                            uint32_t type, uint32_t *vt_base) {
    Dk2Reader sp;
    Dk2Result rc = find_chunk(d, begin, end, "SPRS", &sp);
    if (rc != DK2_OK) return rc;

    const uint8_t *base = sp.data;
    size_t total = sp.size;
    uint32_t groups = info ? info->sprite_groups : 0;
    uint32_t lod = info ? info->lod_count : 0;
    if (groups == 0 || groups > 8192 || lod == 0 || lod > 64)
        return DK2_ERR_CORRUPT;

    uint8_t *lod0_tris = NULL;
    size_t lod0_cap = 0;
    uint32_t *tri_counts = (uint32_t *)calloc((size_t)groups * lod, sizeof(uint32_t));
    uint32_t *num_ex = (uint32_t *)calloc(groups, sizeof(uint32_t));
    if (!tri_counts || !num_ex) {
        free(tri_counts); free(num_ex);
        return DK2_ERR_NOMEM;
    }

    size_t hp = 0;
    for (uint32_t g = 0; g < groups; ++g) {
        if (hp + 8 + (size_t)lod * 4 + 8 > total ||
            memcmp(base + hp, "SPHD", 4) != 0) {
            rc = DK2_ERR_CORRUPT; goto cleanup;
        }
        hp += 8;
        for (uint32_t l = 0; l < lod; ++l) {
            dk2_u32le(base + hp, &tri_counts[g * lod + l]);
            hp += 4;
        }
        dk2_u32le(base + hp, &num_ex[g]);
        hp += 8; /* numVertsEx + mmFactor */
    }

    uint32_t vbase = vt_base ? *vt_base : 0;

    for (uint32_t g = 0; g < groups; ++g) {
        if (hp + 12 > total || memcmp(base + hp, "SPRS", 4) != 0) {
            rc = DK2_ERR_CORRUPT; goto cleanup;
        }
        uint32_t gsz;
        dk2_u32le(base + hp + 4, &gsz);
        if (gsz < 12 || hp + gsz > total) { rc = DK2_ERR_BOUNDS; goto cleanup; }
        Dk2Reader gr;
        dk2_reader_init(&gr, base + hp + 8, gsz - 8);
        uint32_t mat_index;
        if (dk2_read_u32le(&gr, &mat_index) != DK2_OK) {
            rc = DK2_ERR_BOUNDS; goto cleanup;
        }

        if (type == 2) { /* ANIM: skip the POLY chunk header */
            if (gr.pos + 8 > gr.size || memcmp(gr.data + gr.pos, "POLY", 4) != 0) {
                rc = DK2_ERR_CORRUPT; goto cleanup;
            }
            uint32_t psz;
            dk2_u32le(gr.data + gr.pos + 4, &psz);
            if (psz < 8 || gr.pos + psz > gr.size) { rc = DK2_ERR_BOUNDS; goto cleanup; }
            gr.pos += 8;
        }

        /* All triangle LODs are stored first, back to back. */
        for (uint32_t l = 0; l < lod; ++l) {
            uint32_t k = tri_counts[g * lod + l];
            if (k > 200000) { rc = DK2_ERR_CORRUPT; goto cleanup; }
            size_t bytes = (size_t)k * 3u;
            if (gr.pos + bytes > gr.size) { rc = DK2_ERR_BOUNDS; goto cleanup; }
            if (info) {
                if (l == 0) {
                    info->triangle_count += k;
                    info->lod0_triangles += k;
                }
            }
            if (obj && l == 0 && k) {
                if (bytes > lod0_cap) {
                    uint8_t *tmp = (uint8_t *)realloc(lod0_tris, bytes);
                    if (!tmp) { rc = DK2_ERR_NOMEM; goto cleanup; }
                    lod0_tris = tmp;
                    lod0_cap = bytes;
                }
                memcpy(lod0_tris, gr.data + gr.pos, bytes);
            }
            gr.pos += bytes;
        }

        if (type == 2 && gr.pos + 8 <= gr.size &&
            memcmp(gr.data + gr.pos, "VERT", 4) == 0)
            gr.pos += 8;
        if (gr.pos + (size_t)num_ex[g] * 18u > gr.size) {
            rc = DK2_ERR_BOUNDS; goto cleanup;
        }

        if (obj) {
            /* MESH/AnimVertEx are both 18 bytes but with different field
               order.  OBJ export is MESH-only (type 1): index(i16) @0,
               uv(i16,i16) @2/@4, normal(3*f32) @6/@10/@14. */
            fprintf(obj, "g group_%u\nusemtl mat_%u\n", g, mat_index);
            for (uint32_t v = 0; v < num_ex[g]; ++v) {
                const uint8_t *rec = gr.data + gr.pos + (size_t)v * 18u;
                uint16_t raw;
                dk2_u16le(rec, &raw);
                uint16_t gi = raw; /* Java readUnsignedShort() (JAVA:345) */
                dk2_u16le(rec + 2, &raw);
                uint16_t uu = raw; /* Java readUnsignedShort() (JAVA:346) */
                dk2_u16le(rec + 4, &raw);
                uint16_t vv = raw;
                uint32_t bits;
                float nx, ny, nz;
                dk2_u32le(rec + 6, &bits); memcpy(&nx, &bits, 4);
                dk2_u32le(rec + 10, &bits); memcpy(&ny, &bits, 4);
                dk2_u32le(rec + 14, &bits); memcpy(&nz, &bits, 4);
                if ((uint32_t)gi >= vert_count) gi = 0;
                const float *p = verts + (size_t)gi * 3u;
                fprintf(obj, "v %.6f %.6f %.6f\n", p[0], p[1], p[2]);
                fprintf(obj, "vt %.6f %.6f\n",
                        (float)uu / 32768.0f, (float)vv / 32768.0f);
                fprintf(obj, "vn %.6f %.6f %.6f\n", nx, ny, nz);
            }
            uint32_t k = tri_counts[g * lod]; /* LOD0 */
            for (uint32_t t = 0; t < k; ++t) {
                unsigned a = lod0_tris[t * 3], b = lod0_tris[t * 3 + 1],
                         c = lod0_tris[t * 3 + 2];
                if (a < num_ex[g] && b < num_ex[g] && c < num_ex[g]) {
                    fprintf(obj, "f %u/%u/%u %u/%u/%u %u/%u/%u\n",
                            vbase + a + 1, vbase + a + 1, vbase + a + 1,
                            vbase + b + 1, vbase + b + 1, vbase + b + 1,
                            vbase + c + 1, vbase + c + 1, vbase + c + 1);
                }
            }
        }

        if (info) info->extended_count += num_ex[g];
        gr.pos += (size_t)num_ex[g] * 18u;
        vbase += num_ex[g];
        hp += gsz;
    }
    if (vt_base) *vt_base = vbase;
    rc = DK2_OK;

cleanup:
    free(lod0_tris);
    free(tri_counts);
    free(num_ex);
    return rc;
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

    Dk2Material *dummy = NULL;
    uint32_t dummy_count = 0;
    rc = collect_materials(base, base_n, info, &dummy, &dummy_count);
    free(dummy);
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
                snprintf(info->mesh_name, sizeof(info->mesh_name), "%s", tmp_name);
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
            (void)parse_sprs(base, begin, end, info, NULL, NULL, 0,
                             info->type, NULL);
            break;
        }
    }
    return DK2_OK;
}

/* JSON string escaping shared by the embedded OBJ comment and the sidecar. */
static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; ++p) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc((char)c, f); }
        else if (c < 0x20) fprintf(f, "\\u%04x", c);
        else fputc((char)c, f);
    }
    fputc('"', f);
}

static void write_manifest(FILE *f, const Dk2KmfInfo *info,
                           const Dk2Material *mats, uint32_t mat_count) {
    fputs("{\"format\":\"dk2-kmf-materials-v1\",\"mesh\":", f);
    json_str(f, info->mesh_name);
    fputs(",\"lod\":0,\"triangle_count\":", f);
    fprintf(f, "%u", info->triangle_count);
    fputs(",\"usemtl_prefix\":\"mat_\",\"materials\":[", f);
    for (uint32_t i = 0; i < mat_count; ++i) {
        if (i) fputc(',', f);
        fputs("{\"usemtl\":", f);
        char label[64];
        snprintf(label, sizeof(label), "mat_%u", i);
        json_str(f, label);
        fputs(",\"name\":", f);
        json_str(f, mats[i].name);
        fprintf(f, ",\"flags\":%u,\"textures\":[", mats[i].flags);
        for (uint32_t t = 0; t < mats[i].texture_count; ++t) {
            if (t) fputc(',', f);
            json_str(f, mats[i].textures[t]);
        }
        fputs("]}", f);
    }
    fputs("]}", f);
}

Dk2Result dk2_kmf_write_manifest(const uint8_t *d, size_t n, FILE *out) {
    if (!d || !out || n < 28) return DK2_ERR_ARGS;
    Dk2KmfInfo info;
    Dk2Result rc = dk2_kmf_info(d, n, &info);
    if (rc != DK2_OK) return rc;
    if (info.type != 1 && info.type != 2) return DK2_ERR_UNSUPPORTED;

    Dk2Material *mats = NULL;
    uint32_t mat_count = 0;
    rc = collect_materials(d + 12, n - 12, NULL, &mats, &mat_count);
    if (rc != DK2_OK) return rc;
    write_manifest(out, &info, mats, mat_count);
    free(mats);
    return DK2_OK;
}

Dk2Result dk2_kmf_export_manifest(const uint8_t *d, size_t n, const char *path) {
    if (!d || !path || n < 28) return DK2_ERR_ARGS;
    FILE *f = fopen(path, "wb");
    if (!f) return DK2_ERR_IO;
    Dk2Result rc = dk2_kmf_write_manifest(d, n, f);
    if (rc == DK2_OK) fputc('\n', f);
    fclose(f);
    return rc;
}

Dk2Result dk2_kmf_export_obj(const uint8_t *d, size_t n, FILE *out) {
    if (!d || !out || n < 28) return DK2_ERR_ARGS;
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
        if (g.size < (size_t)info.vertex_count * 12u) return DK2_ERR_BOUNDS;

        fprintf(out, "# DK2 KMF: %s (LOD0 only, %u triangles)\n",
                info.mesh_name, info.triangle_count);
        uint32_t vt_base = 0;
        Dk2KmfInfo export_info = info; /* counting must not corrupt `info` */
        rc = parse_sprs(base, begin, end, &export_info, out,
                        (const float *)g.data, info.vertex_count,
                        info.type, &vt_base);
        if (rc == DK2_OK) {
            fputs("# DK2_MATERIAL_MANIFEST ", out);
            (void)dk2_kmf_write_manifest(d, n, out);
            fputc('\n', out);
        }
        return rc;
    }
    return DK2_ERR_BOUNDS;
}
