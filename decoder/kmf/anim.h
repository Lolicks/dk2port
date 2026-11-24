#ifndef DK2_ANIM_H
#define DK2_ANIM_H

#include "../core/reader.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* DKAF — DK2 Animation Frame format                                         */
/*                                                                           */
/* All integers are little-endian. All float fields are IEEE-754 f32.        */
/*                                                                           */
/*  Header (56 bytes)                                                        */
/*    u32  magic          = 0x46414B44 ('DKAF')                              */
/*    u32  version        = 2                                                */
/*    u32  fps            = 30          (game tick; KmfModelLoader :278-280) */
/*    u32  frameCount                                                        */
/*    u32  vertexCount    (sum of per-sprite AnimVertEx counts)              */
/*    u32  indexCount     (LOD0 triangle indices)                            */
/*    u32  poseCount      (= frameCount * vertexCount)                       */
/*    f32  scale          (ANIM/HEAD.scale)                                  */
/*    f32  originX,Y,Z    (ANIM/HEAD.pos)                                    */
/*    f32  cubeScale      (ANIM/HEAD.cubeScale)                              */
/*    u32  frameFactor    (0 CLAMP / 1 WRAP)                                 */
/*                                                                           */
/*  Vertices: vertexCount * 40 bytes                                         */
/*    f32  baseX,Y,Z      (bind pose = GEOM coord at frame 0)                */
/*    f32  normalX,Y,Z    (VERT normal @4/@8/@12)                            */
/*    f32  u,v            (VERT uv @0/@2, divided by 32768)                  */
/*    u16  itabIndex      (VERT @16)                                         */
/*    u16  spriteIndex                                                       */
/*    u32  reserved                                                          */
/*                                                                           */
/*  Indices: indexCount * u16 (LOD0, already offset into the vertex array)   */
/*                                                                           */
/*  Poses: poseCount * 40 bytes, order = frame * vertexCount + vertex        */
/*    f32  prevX,Y,Z      (GEOM[geomIndex], the previous key-frame coord)    */
/*    f32  nextX,Y,Z      (GEOM[geomIndex + 1], the next key-frame coord)    */
/*    f32  weight         (FrameInfo.geomFactor, clamped to [0,1])           */
/*    u16  prevPoseFrame  (frameBlock*128 + GEOM.frameBase)                  */
/*    u16  nextPoseFrame  (frameBlock*128 + GEOM[+1].frameBase)              */
/*    u32  reserved                                                          */
/*                                                                           */
/*  The pose record is the C counterpart of OpenKeeper's FrameInfo          */
/*  (KmfModelLoader.java:295-460, 837-897).  A renderer computes             */
/*      position = mix(prevXYZ, nextXYZ, weight)                             */
/*  so every one of the 30 frames per second is a distinct pose; no baked    */
/*  linear interpolation of whole OBJ frames is required.                   */
/* ------------------------------------------------------------------------- */

#define DK2_ANIM_FPS 30u
#define DK2_DKAF_MAGIC 0x46414B44u
#define DK2_DKAF_VERSION 2u
#define DK2_DKAF_HEADER_BYTES 56u
#define DK2_DKAF_VERTEX_BYTES 40u
#define DK2_DKAF_POSE_BYTES 40u

typedef struct {
    uint32_t fps;
    uint32_t frame_count;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t pose_count;
    float scale;
    float origin[3];
    float cube_scale;
    uint32_t frame_factor;
} Dk2DkafHeader;

/* Export up to max_frames animation frames of an ANIM-type KMF as OBJ files
   frame_0000.obj ... (max_frames == 0 means every frame).  The OBJ carries
   VERT UVs (`vt`), VERT normals (`vn`) and DK2_POSE/DK2_ANIM comment records
   with the same morph weights as the binary DKAF export.  Uses the formula
   from KmfFile.java / KmfModelLoader.java:
       geomBase   = ITAB[frame >> 7][itab_index]
       geomIndex  = geomBase + VGEO[itab_index][frame]
       coord      = GEOM[geomIndex] (10-bit packed, scaled)
   Returns DK2_ERR_UNSUPPORTED for MESH/GROP. */
Dk2Result dk2_anim_export(const uint8_t *data, size_t size,
                          const char *outdir, unsigned max_frames,
                          unsigned *frames_written);

/* Write the canonical binary DKAF v2 animation described above.  Returns
   DK2_ERR_UNSUPPORTED for MESH/GROP, DK2_ERR_IO if the file cannot be
   created.  The header is copied into `header` when non-NULL. */
Dk2Result dk2_anim_export_dkaf(const uint8_t *data, size_t size,
                               const char *path, Dk2DkafHeader *header);

/* Write the material -> texture sidecar manifest (JSON) for a MESH or ANIM
   KMF, either to a path or to an already-open stream (used for the embedded
   OBJ comment in both exporters). Declared here because kmf.h is outside this
   change's write scope. */
Dk2Result dk2_kmf_export_manifest(const uint8_t *data, size_t size,
                                  const char *path);
Dk2Result dk2_kmf_write_manifest(const uint8_t *data, size_t size, FILE *out);

#ifdef __cplusplus
}
#endif
#endif
