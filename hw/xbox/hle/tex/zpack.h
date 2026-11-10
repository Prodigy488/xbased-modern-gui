/*
 * hakuX HLE GPU — a depth target's image as the NV2A's words in memory.
 *
 * The host copies a depth image out an aspect at a time: its depth as 16-bit
 * words, as 32-bit words with the 24 bits low, or as floats, and its stencil
 * as bytes. The NV2A's depth buffer is one word a texel: Z16, or Z24S8 with
 * the depth in the top 24 bits and the stencil in the low byte.
 */
#ifndef HW_XBOX_HLE_TEX_ZPACK_H
#define HW_XBOX_HLE_TEX_ZPACK_H

#include <stdint.h>

/* What the depth aspect's copy holds a texel. */
enum { HLE_ZSRC_NONE, HLE_ZSRC_D16, HLE_ZSRC_D24, HLE_ZSRC_F32 };

/* The bytes a texel's depth takes in the copy (0: not a source). */
static inline uint32_t hle_zsrc_bytes(int src)
{
    return src == HLE_ZSRC_D16 ? 2 : src == HLE_ZSRC_D24 || src == HLE_ZSRC_F32 ? 4 : 0;
}

/* n texels: depth (hle_zsrc_bytes(src) each) and stencil (a byte each; NULL:
 * none, 0) into out, gbpp bytes each: 2 Z16, 4 Z24S8. A depth of another
 * width is rescaled as the chip's own formats relate (a 16-bit one's bits
 * repeated into 24, a 24-bit one's top 16 kept); a float one is taken as
 * 0 .. 1 of the format's range, to nearest. */
void hle_zpack(const uint8_t *depth, const uint8_t *stencil, int src, uint32_t n, uint32_t gbpp,
               uint8_t *out);

#endif
