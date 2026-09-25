/* Image helpers for the 16-bit panel. */
#include "avo_core.h"

/* 4x4 Bayer matrix: thresholds 0..15 spread evenly in every 2x2 and 4x4. */
static const uint8_t BAYER4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

/* floor(v * levels / 255 + (t + 0.5) / 16): averages to the exact value. */
static inline uint32_t quantize(uint32_t v, uint32_t levels, uint32_t t)
{
    uint32_t q = (v * levels * 32u + (2u * t + 1u) * 255u) / (255u * 32u);
    return q > levels ? levels : q;
}

void avo_dither_rgb888_to_rgb565(const uint8_t *rgb, uint16_t *out, int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t t = BAYER4[y & 3][x & 3];
            const uint8_t *p = rgb + 3 * ((size_t)y * (size_t)w + (size_t)x);
            uint32_t r = quantize(p[0], 31, t), g = quantize(p[1], 63, t), b = quantize(p[2], 31, t);
            out[(size_t)y * (size_t)w + (size_t)x] = (uint16_t)(r << 11 | g << 5 | b);
        }
    }
}
