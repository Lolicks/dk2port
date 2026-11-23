#ifndef DK2_ANIM_H
#define DK2_ANIM_H

#include "../core/reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Export up to max_frames animation frames of an ANIM-type KMF as OBJ files
   frame_0000.obj ... using the formula from KmfFile.java / KmfModelLoader.java:
       geomBase = ITAB[frame >> 7][itab_index]
       geomIndex = geomBase + VGEO[itab_index][frame]
       coord = GEOM[geomIndex] (10-bit packed, scaled)
   Returns DK2_ERR_UNSUPPORTED for MESH/GROP. */
Dk2Result dk2_anim_export(const uint8_t *data, size_t size,
                          const char *outdir, unsigned max_frames,
                          unsigned *frames_written);

#ifdef __cplusplus
}
#endif
#endif
