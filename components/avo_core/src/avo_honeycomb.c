#include <math.h>
#include "avo_core.h"

/* Axial hex directions for a pointy-top layout, walked counter-clockwise. */
static const int8_t HEX_DIR[6][2] = {
    { +1, 0 }, { +1, -1 }, { 0, -1 }, { -1, 0 }, { -1, +1 }, { 0, +1 },
};

static void slot_to_axial(int index, int *q, int *r)
{
    *q = 0;
    *r = 0;
    if (index <= 0) {
        return;
    }
    /* ring k holds 6k slots; find the ring that contains `index` */
    int k = 1;
    int first = 1;
    while (index >= first + 6 * k) {
        first += 6 * k;
        k++;
    }
    int step = index - first;
    int cq = HEX_DIR[4][0] * k;
    int cr = HEX_DIR[4][1] * k;
    for (int side = 0; side < 6; side++) {
        for (int j = 0; j < k; j++) {
            if (step == 0) {
                *q = cq;
                *r = cr;
                return;
            }
            cq += HEX_DIR[side][0];
            cr += HEX_DIR[side][1];
            step--;
        }
    }
}

avo_pt_t avo_hc_slot(int index, int spacing)
{
    int q, r;
    slot_to_axial(index, &q, &r);
    const float s = (float)spacing;
    avo_pt_t p = {
        .x = (int16_t)lroundf(s * ((float)q + (float)r * 0.5f)),
        .y = (int16_t)lroundf(s * 0.8660254f * (float)r),
    };
    return p;
}

uint16_t avo_hc_scale(int dx, int dy, const avo_hc_cfg_t *cfg)
{
    const float d = sqrtf((float)(dx * dx + dy * dy));
    if (d <= (float)cfg->full_radius) {
        return AVO_SCALE_ONE;
    }
    if (d >= (float)cfg->fade_radius || cfg->fade_radius <= cfg->full_radius) {
        return cfg->min_scale;
    }
    const float t = (d - (float)cfg->full_radius) / (float)(cfg->fade_radius - cfg->full_radius);
    const float ease = t * t * (3.0f - 2.0f * t);
    const float range = (float)(AVO_SCALE_ONE - cfg->min_scale);
    return (uint16_t)lroundf((float)AVO_SCALE_ONE - range * ease);
}
