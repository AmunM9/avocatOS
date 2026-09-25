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

#define VOLUME_DEFAULT 70
#define STEP_GOAL_DEFAULT 8000
#define STEP_GOAL_MIN 1000
#define STEP_GOAL_MAX 50000

/* Fields added in settings version 2. */
static void defaults_v2(avo_settings_t *s)
{
    s->volume = VOLUME_DEFAULT;
    s->double_tap = true;
    s->wrist_flick = true;
    s->weather = true;
    s->step_goal = STEP_GOAL_DEFAULT;
}

/* Fields added in settings version 3. */
static void defaults_v3(avo_settings_t *s)
{
    s->artwork = true;
}

/* Fields added in settings version 4. */
static void defaults_v4(avo_settings_t *s)
{
    s->low_power = false;
}

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
    defaults_v2(s);
    defaults_v3(s);
    defaults_v4(s);
}

bool avo_settings_upgrade(avo_settings_t *s, size_t loaded_len)
{
    /* each older version is a prefix of the next: fill what it lacks */
    static const struct {
        uint16_t version;
        size_t size;
    } OLD[] = { { 1, AVO_SETTINGS_V1_SIZE }, { 2, AVO_SETTINGS_V2_SIZE }, { 3, AVO_SETTINGS_V3_SIZE } };
    if (s->version == AVO_SETTINGS_VERSION && loaded_len == sizeof *s) {
        return true;
    }
    for (size_t i = 0; i < sizeof OLD / sizeof OLD[0]; i++) {
        if (s->version == OLD[i].version && loaded_len == OLD[i].size) {
            if (s->version < 2) defaults_v2(s);
            if (s->version < 3) defaults_v3(s);
            if (s->version < 4) defaults_v4(s);
            s->version = AVO_SETTINGS_VERSION;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Battery time left                                                   */
/* ------------------------------------------------------------------ */

#define BATT_MIN_SAMPLES 3
#define BATT_MIN_SPAN_MIN 20
#define BATT_MAX_LEFT_MIN (7 * 24 * 60)

void avo_batt_hist_add(avo_batt_hist_t *h, uint32_t minute, int percent, bool charging)
{
    if (charging || percent < 0) {
        memset(h, 0, sizeof *h);
        return;
    }
    h->minute[h->head] = minute;
    h->pct[h->head] = (uint8_t)(percent > 100 ? 100 : percent);
    h->head = (uint8_t)((h->head + 1) % AVO_BATT_HIST);
    if (h->n < AVO_BATT_HIST) h->n++;
}

int avo_batt_minutes_left(const avo_batt_hist_t *h)
{
    if (h->n < BATT_MIN_SAMPLES) {
        return -1;
    }
    int oldest = (h->head + AVO_BATT_HIST - h->n) % AVO_BATT_HIST;
    int newest = (h->head + AVO_BATT_HIST - 1) % AVO_BATT_HIST;
    if (h->minute[newest] - h->minute[oldest] < BATT_MIN_SPAN_MIN) {
        return -1;
    }
    /* least-squares slope of percent over time (relative to the oldest) */
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int k = 0; k < h->n; k++) {
        int i = (oldest + k) % AVO_BATT_HIST;
        double x = (double)(h->minute[i] - h->minute[oldest]), y = h->pct[i];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    double den = h->n * sxx - sx * sx;
    double slope = den > 0 ? (h->n * sxy - sx * sy) / den : 0; /* % per minute */
    if (slope >= 0) {
        return -1;
    }
    double left = h->pct[newest] / -slope;
    return left > BATT_MAX_LEFT_MIN ? BATT_MAX_LEFT_MIN : (int)left;
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
    CLAMP_FIELD(s->volume, 0, 100);
    CLAMP_FIELD(s->step_goal, STEP_GOAL_MIN, STEP_GOAL_MAX);
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
