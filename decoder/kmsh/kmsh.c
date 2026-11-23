#include "kmsh.h"

#include <stdlib.h>
#include <string.h>

Dk2Result dk2_kmsh_envelope(const uint8_t *s, size_t n, uint32_t *declared) {
    if (!s || n < 14) return DK2_ERR_ARGS;
    if (s[0] != 0x10 || s[1] != 0xfb || memcmp(s + 6, "KMSH", 4) != 0)
        return DK2_ERR_MAGIC;

    uint32_t full;
    dk2_u32le(s + 10, &full);
    uint16_t low;
    dk2_u16be(s + 3, &low);
    if ((full & 0xFFFFu) != (uint32_t)low) return DK2_ERR_CORRUPT;
    if (s[12] != s[2] || full == 0) return DK2_ERR_CORRUPT;
    if (declared) *declared = full;
    return DK2_OK;
}

/* do { out[j++] = out[k++]; } while (counter-- != 0) => counter+1 copies */
static Dk2Result copy_ref(uint8_t *out, size_t out_cap, size_t *j,
                          long k, unsigned counter) {
    do {
        if (k < 0 || (size_t)k >= *j || *j >= out_cap) return DK2_ERR_CORRUPT;
        out[(*j)++] = out[k++];
    } while (counter-- != 0);
    return DK2_OK;
}

Dk2Result dk2_kmsh_decompress(const uint8_t *src, size_t n,
                              uint8_t **out, size_t *out_size) {
    if (!src || !out || !out_size) return DK2_ERR_ARGS;
    uint32_t declared = 0;
    Dk2Result rc = dk2_kmsh_envelope(src, n, &declared);
    if (rc != DK2_OK) return rc;
    if (declared > 256u * 1024u * 1024u) return DK2_ERR_SIZE;

    uint8_t *dst = (uint8_t *)malloc(declared);
    if (!dst) return DK2_ERR_NOMEM;

    size_t i = 0;
    if (src[i++] & 1u) {
        if (i + 3 > n) { free(dst); return DK2_ERR_BOUNDS; }
        i += 3;
    }
    if (i + 1 > n) { free(dst); return DK2_ERR_BOUNDS; }
    i++; /* skip second wrapper byte */
    if (i + 3 > n) { free(dst); return DK2_ERR_BOUNDS; }
    uint32_t stream_size = ((uint32_t)src[i] << 16) |
                           ((uint32_t)src[i + 1] << 8) |
                           (uint32_t)src[i + 2];
    i += 3;
    if (stream_size != declared) { free(dst); return DK2_ERR_SIZE; }

    size_t j = 0;
    int finished = 0;
    while (!finished) {
        if (i >= n) { free(dst); return DK2_ERR_BOUNDS; }
        uint8_t flag = src[i++];

        if ((flag & 0x80u) == 0) {
            if (i >= n) { free(dst); return DK2_ERR_BOUNDS; }
            unsigned tmp = src[i++];
            unsigned count = flag & 3u;
            while (count--) {
                if (i >= n || j >= declared) { free(dst); return DK2_ERR_BOUNDS; }
                dst[j++] = src[i++];
            }
            long k = (long)j - (long)(((flag & 0x60u) << 3)) - (long)tmp - 1;
            rc = copy_ref(dst, declared, &j, k, ((flag >> 2) & 7u) + 2u);
        } else if ((flag & 0x40u) == 0) {
            if (i + 2 > n) { free(dst); return DK2_ERR_BOUNDS; }
            unsigned tmp = src[i++], tmp2 = src[i++];
            unsigned count = tmp >> 6;
            while (count--) {
                if (i >= n || j >= declared) { free(dst); return DK2_ERR_BOUNDS; }
                dst[j++] = src[i++];
            }
            long k = (long)j - (long)((tmp & 0x3Fu) << 8) - (long)tmp2 - 1;
            rc = copy_ref(dst, declared, &j, k, (flag & 0x3Fu) + 3u);
        } else if ((flag & 0x20u) == 0) {
            if (i + 3 > n) { free(dst); return DK2_ERR_BOUNDS; }
            unsigned a = src[i++], b = src[i++], d = src[i++];
            unsigned count = flag & 3u;
            while (count--) {
                if (i >= n || j >= declared) { free(dst); return DK2_ERR_BOUNDS; }
                dst[j++] = src[i++];
            }
            long k = (long)j - (long)(((flag & 0x10u) << 12)) -
                     (long)(a << 8) - (long)b - 1;
            rc = copy_ref(dst, declared, &j, k,
                          d + ((flag & 0x0Cu) << 6) + 4u);
        } else {
            unsigned count = (flag & 0x1Fu) * 4u + 4u;
            if (count > 0x70u) {
                finished = 1;
                count = flag & 3u;
            }
            while (count--) {
                if (i >= n || j >= declared) { free(dst); return DK2_ERR_BOUNDS; }
                dst[j++] = src[i++];
            }
        }
        if (rc != DK2_OK) { free(dst); return rc; }
    }

    if (j != declared) { free(dst); return DK2_ERR_SIZE; }
    *out = dst;
    *out_size = j;
    return DK2_OK;
}
