/*
 * hakuX HLE GPU — Kelvin values that come in a surface's own format (kvfmt.h).
 */
#include "hw/xbox/hle/pb/kvfmt.h"

static uint32_t wide(uint32_t x, uint32_t max)
{
    return x * 255 / max;
}

uint32_t hle_kv_clear_color(uint32_t cf, uint32_t v)
{
    switch (cf & 0xF) {
    case 1:     /* X1R5G5B5_Z1R5G5B5 */
    case 2:     /* X1R5G5B5_O1R5G5B5 */
        return ((cf & 0xF) == 2 ? 0xFF000000u : 0) | wide((v >> 10) & 0x1F, 31) << 16 |
               wide((v >> 5) & 0x1F, 31) << 8 | wide(v & 0x1F, 31);
    case 3:     /* R5G6B5 */
        return 0xFF000000u | wide((v >> 11) & 0x1F, 31) << 16 | wide((v >> 5) & 0x3F, 63) << 8 |
               wide(v & 0x1F, 31);
    case 4:     /* X8R8G8B8_Z8R8G8B8 */
        return v & 0x00FFFFFFu;
    case 5:     /* X8R8G8B8_O8R8G8B8 */
        return v | 0xFF000000u;
    case 6:     /* X1A7R8G8B8_Z1A7R8G8B8 */
        return v & 0x7FFFFFFFu;
    case 7:     /* X1A7R8G8B8_O1A7R8G8B8 */
        return v | 0x80000000u;
    default:    /* A8R8G8B8, B8, G8B8 */
        return v;
    }
}

bool hle_kv_takes_listed(uint32_t m, uint32_t v)
{
    switch (m) {
    case 0x0344:        /* SET_BLEND_FUNC_SFACTOR */
    case 0x0348:        /* SET_BLEND_FUNC_DFACTOR */
        return v <= 1 || (v >= 0x0300 && v <= 0x0308) || (v >= 0x8001 && v <= 0x8004);
    case 0x0350:        /* SET_BLEND_EQUATION: add, min, max, subtract, reverse subtract, and the
                         * signed reverse subtract and add */
        return (v >= 0x8006 && v <= 0x8008) || v == 0x800A || v == 0x800B || v == 0xF005 || v == 0xF006;
    case 0x0354:        /* SET_DEPTH_FUNC */
        return v >= 0x0200 && v <= 0x0207;
    case 0x037C:        /* SET_SHADE_MODE: flat, smooth */
        return v == 0x1D00 || v == 0x1D01;
    case 0x03A0:        /* SET_FRONT_FACE: clockwise, counter-clockwise */
        return v == 0x0900 || v == 0x0901;
    case 0x043C:        /* SET_POINT_SIZE: 9 bits, in eighths */
        return v <= 0x1FF;
    default:
        return true;
    }
}
