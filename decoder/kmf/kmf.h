#ifndef DK2_KMF_H
#define DK2_KMF_H

#include "../core/reader.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DK2_KMF_MAX_CHUNKS 4096u
#define DK2_KMF_MAX_TEXTURES 1024u

typedef struct {
    char tag[5];
    uint32_t size;      /* includes the 8-byte chunk header */
    size_t offset;      /* offset of the chunk header in the file */
} Dk2KmfChunk;

typedef struct {
    uint32_t version;
    uint32_t type;      /* 1=MESH, 2=ANIM, 3=GROP */
    char mesh_name[128];
    uint32_t chunk_count;
    uint32_t material_count;
    uint32_t texture_count;
    char textures[DK2_KMF_MAX_TEXTURES][128];
    uint32_t vertex_count;      /* GEOM vertices (MESH) */
    uint32_t extended_count;    /* MeshVertEx/AnimVertEx */
    uint32_t triangle_count;    /* all LODs */
    uint32_t lod0_triangles;
    uint32_t sprite_groups;
    uint32_t lod_count;
    float scale;
} Dk2KmfInfo;

Dk2Result dk2_kmf_walk(const uint8_t *data, size_t size,
                       Dk2KmfChunk *out, uint32_t capacity, uint32_t *count);
Dk2Result dk2_kmf_info(const uint8_t *data, size_t size, Dk2KmfInfo *info);
/* Export MESH geometry as Wavefront OBJ (vertices + triangles + UVs).
   Returns DK2_ERR_UNSUPPORTED for ANIM/GROP (no static triangle soup). */
Dk2Result dk2_kmf_export_obj(const uint8_t *data, size_t size, FILE *out);

#ifdef __cplusplus
}
#endif
#endif
