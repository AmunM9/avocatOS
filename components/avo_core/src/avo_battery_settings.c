#include <string.h>
#include "avo_core.h"

/* ------------------------------------------------------------------ */
/* Battery: typical single-cell Li-ion discharge curve (rest voltage)  */
/* ------------------------------------------------------------------ */

typedef struct {
    int16_t mv;
    int8_t pct;
} curve_pt_t;

static const curve_pt_t CURVE[] = {
    { 3300, 0 }, { 3500, 5 }, { 3600, 12 }, { 3650, 20 }, { 3700, 30 }, { 3750, 40 },
    { 3800, 50 }, { 3900, 65 }, { 4000, 78 }, { 4100, 90 }, { 4200, 100 },
};

int avo_batt_percent_from_mv(int mv)
{
    const size_t n = sizeof CURVE / sizeof CURVE[0];
    if (mv <= CURVE[0].mv) {
        return 0;
    }
    if (mv >= CURVE[n - 1].mv) {
        return 100;
    }
    for (size_t i = 1; i < n; i++) {
        if (mv <= CURVE[i].mv) {
            const curve_pt_t a = CURVE[i - 1], b = CURVE[i];
            return a.pct + (mv - a.mv) * (b.pct - a.pct) / (b.mv - a.mv);
        }
    }
    return 100;
}

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */

#define BRIGHTNESS_MIN 5
#define BRIGHTNESS_MAX 100
#define TIMEOUT_MIN_S 5
#define TIMEOUT_MAX_S 120
#define UTC_MIN (-720)
#define UTC_MAX 840
#define DEFAULT_UTC_OFFSET (-300) /* UTC-5 (Bogotá); changed from Ajustes > Hora */

void avo_settings_defaults(avo_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->version = AVO_SETTINGS_VERSION;
    s->brightness = 80;
    s->theme = AVO_THEME_CLEAN;
    s->face = 0;
    s->h24 = true;
    s->aod = true;
    s->raise_to_wake = true;
    s->sounds = true;
    s->bluetooth = true;
    s->wifi = false;
    s->show_fps = false;
    s->screen_timeout_s = 15;
    s->utc_offset_min = DEFAULT_UTC_OFFSET;
}

#define CLAMP_FIELD(field, lo, hi) do { \
    if ((field) < (lo)) { (field) = (lo); changed = true; } \
    else if ((field) > (hi)) { (field) = (hi); changed = true; } \
} while (0)

bool avo_settings_sanitize(avo_settings_t *s, uint8_t face_count)
{
    bool changed = false;
    if (s->version != AVO_SETTINGS_VERSION) {
        s->version = AVO_SETTINGS_VERSION;
        changed = true;
    }
    CLAMP_FIELD(s->brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    CLAMP_FIELD(s->screen_timeout_s, TIMEOUT_MIN_S, TIMEOUT_MAX_S);
    CLAMP_FIELD(s->utc_offset_min, UTC_MIN, UTC_MAX);
    if (s->theme > AVO_THEME_AVOCADO) {
        s->theme = AVO_THEME_CLEAN;
        changed = true;
    }
    if (face_count == 0 || s->face >= face_count) {
        if (s->face != 0) {
            changed = true;
        }
        s->face = 0;
    }
    if (s->wifi_ssid[AVO_WIFI_SSID_MAX - 1] != '\0') {
        s->wifi_ssid[AVO_WIFI_SSID_MAX - 1] = '\0';
        changed = true;
    }
    if (s->wifi_pass[AVO_WIFI_PASS_MAX - 1] != '\0') {
        s->wifi_pass[AVO_WIFI_PASS_MAX - 1] = '\0';
        changed = true;
    }
    return changed;
}
