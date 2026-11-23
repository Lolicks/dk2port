#ifndef DK2_KMSH_H
#define DK2_KMSH_H

#include "../core/reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decompress one complete stored WAD entry (the 10 fb ... payload).
   On success allocates *out with malloc(); caller frees it. */
Dk2Result dk2_kmsh_decompress(const uint8_t *stored, size_t stored_size,
                              uint8_t **out, size_t *out_size);

/* Validate the 14-byte envelope without decompressing. */
Dk2Result dk2_kmsh_envelope(const uint8_t *stored, size_t stored_size,
                            uint32_t *declared_size);

#ifdef __cplusplus
}
#endif
#endif
