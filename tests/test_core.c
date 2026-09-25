/* Host unit tests for avo_core. Run: make -C tests */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "avo_core.h"

static int g_failed = 0;
static int g_passed = 0;

#define CHECK(cond) do { \
    if (cond) { g_passed++; } \
    else { g_failed++; fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(a, b) do { \
    if (strcmp((a), (b)) == 0) { g_passed++; } \
    else { g_failed++; fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } \
} while (0)

#define RUN(fn) do { fprintf(stderr, "- %s\n", #fn); fn(); } while (0)

static int dist(avo_pt_t p) { return (int)(0.5 + __builtin_sqrt((double)(p.x * p.x + p.y * p.y))); }

/* ---------------- honeycomb ---------------- */

static void test_hc_center_slot_is_origin(void)
{
    avo_pt_t p = avo_hc_slot(0, 100);
    CHECK(p.x == 0 && p.y == 0);
}

static void test_hc_ring1_slots_are_one_spacing_away(void)
{
    for (int i = 1; i <= 6; i++) {
        int d = dist(avo_hc_slot(i, 100));
        CHECK(d >= 99 && d <= 101);
    }
}

static void test_hc_ring2_slots_are_farther_than_ring1(void)
{
    for (int i = 7; i <= 18; i++) {
        int d = dist(avo_hc_slot(i, 100));
        CHECK(d >= 170 && d <= 201);
    }
}

static void test_hc_slots_are_unique(void)
{
    avo_pt_t pts[19];
    for (int i = 0; i < 19; i++) pts[i] = avo_hc_slot(i, 100);
    for (int i = 0; i < 19; i++)
        for (int j = i + 1; j < 19; j++)
            CHECK(!(pts[i].x == pts[j].x && pts[i].y == pts[j].y));
}

static void test_hc_neighbours_never_overlap(void)
{
    /* any two slots are at least `spacing` apart (hex packing) */
    for (int i = 0; i < 19; i++)
        for (int j = i + 1; j < 19; j++) {
            avo_pt_t a = avo_hc_slot(i, 100), b = avo_hc_slot(j, 100);
            avo_pt_t d = { (int16_t)(a.x - b.x), (int16_t)(a.y - b.y) };
            CHECK(dist(d) >= 99);
        }
}

static void test_hc_scale_full_inside_radius(void)
{
    avo_hc_cfg_t cfg = { .full_radius = 100, .fade_radius = 200, .min_scale = 96 };
    CHECK(avo_hc_scale(0, 0, &cfg) == AVO_SCALE_ONE);
    CHECK(avo_hc_scale(60, 60, &cfg) == AVO_SCALE_ONE);
}

static void test_hc_scale_min_outside_fade(void)
{
    avo_hc_cfg_t cfg = { .full_radius = 100, .fade_radius = 200, .min_scale = 96 };
    CHECK(avo_hc_scale(300, 0, &cfg) == 96);
    CHECK(avo_hc_scale(0, -250, &cfg) == 96);
}

static void test_hc_scale_is_monotonic_in_band(void)
{
    avo_hc_cfg_t cfg = { .full_radius = 100, .fade_radius = 200, .min_scale = 96 };
    uint16_t prev = AVO_SCALE_ONE;
    for (int d = 100; d <= 200; d += 5) {
        uint16_t s = avo_hc_scale(d, 0, &cfg);
        CHECK(s <= prev);
        CHECK(s >= 96 && s <= AVO_SCALE_ONE);
        prev = s;
    }
}

/* ---------------- time formatting ---------------- */

static const avo_time_t T_WED = { 2026, 9, 24, 3, 22, 9, 5 };
static const avo_time_t T_MIDNIGHT = { 2026, 1, 4, 0, 0, 7, 0 };

static void test_fmt_hm_24h(void)
{
    char b[16];
    avo_fmt_hm(b, sizeof b, &T_WED, true);
    CHECK_STR(b, "22:09");
}

static void test_fmt_hm_12h(void)
{
    char b[16];
    avo_fmt_hm(b, sizeof b, &T_WED, false);
    CHECK_STR(b, "10:09");
    avo_fmt_hm(b, sizeof b, &T_MIDNIGHT, false);
    CHECK_STR(b, "12:07");
    CHECK_STR(avo_fmt_ampm(&T_WED), "p. m.");
    CHECK_STR(avo_fmt_ampm(&T_MIDNIGHT), "a. m.");
}

static void test_fmt_hour_min_padded(void)
{
    char b[8];
    avo_fmt_hour(b, sizeof b, &T_MIDNIGHT, true);
    CHECK_STR(b, "00");
    avo_fmt_min(b, sizeof b, &T_MIDNIGHT);
    CHECK_STR(b, "07");
}

static void test_fmt_wday_day(void)
{
    char b[16];
    avo_fmt_wday_day(b, sizeof b, &T_WED);
    CHECK_STR(b, "MIÉ 24");
}

static void test_fmt_long_date(void)
{
    char b[64];
    avo_fmt_long_date(b, sizeof b, &T_WED);
    CHECK_STR(b, "miércoles, 24 de septiembre");
    avo_fmt_long_date(b, sizeof b, &T_MIDNIGHT);
    CHECK_STR(b, "domingo, 4 de enero");
}

static void test_fmt_never_overflows(void)
{
    char b[4];
    memset(b, 'x', sizeof b);
    avo_fmt_long_date(b, sizeof b, &T_WED);
    CHECK(b[3] == '\0');
}

static void test_fmt_stopwatch(void)
{
    char b[16];
    avo_fmt_stopwatch(b, sizeof b, 0);
    CHECK_STR(b, "00:00,00");
    avo_fmt_stopwatch(b, sizeof b, 61230);
    CHECK_STR(b, "01:01,23");
    avo_fmt_stopwatch(b, sizeof b, 3600000 + 2000);
    CHECK_STR(b, "1:00:02,00");
}

/* ---------------- power state ---------------- */

static const avo_pwr_cfg_t PCFG = { .dim_after_ms = 8000, .sleep_after_ms = 15000, .aod_enabled = true };

static void test_pwr_starts_active(void)
{
    avo_pwr_t p;
    avo_pwr_init(&p, &PCFG, 1000);
    CHECK(p.state == AVO_PWR_ACTIVE);
}

static void test_pwr_dims_then_aod(void)
{
    avo_pwr_t p;
    avo_pwr_init(&p, &PCFG, 0);
    CHECK(avo_pwr_tick(&p, 7999) == AVO_PWR_ACTIVE);
    CHECK(avo_pwr_tick(&p, 8000) == AVO_PWR_DIM);
    CHECK(avo_pwr_tick(&p, 15000) == AVO_PWR_AOD);
}

static void test_pwr_off_when_aod_disabled(void)
{
    avo_pwr_cfg_t c = PCFG;
    c.aod_enabled = false;
    avo_pwr_t p;
    avo_pwr_init(&p, &c, 0);
    CHECK(avo_pwr_tick(&p, 20000) == AVO_PWR_OFF);
}

static void test_pwr_activity_wakes(void)
{
    avo_pwr_t p;
    avo_pwr_init(&p, &PCFG, 0);
    avo_pwr_tick(&p, 20000);
    CHECK(avo_pwr_activity(&p, 20001) == AVO_PWR_ACTIVE);
    CHECK(avo_pwr_tick(&p, 20002 + 7000) == AVO_PWR_ACTIVE);
}

static void test_pwr_forced_sleep(void)
{
    avo_pwr_t p;
    avo_pwr_init(&p, &PCFG, 0);
    CHECK(avo_pwr_sleep(&p) == AVO_PWR_AOD);
}

static void test_pwr_survives_tick_wraparound(void)
{
    avo_pwr_t p;
    avo_pwr_init(&p, &PCFG, 0xFFFFF000u);
    CHECK(avo_pwr_tick(&p, 0x00000100u) == AVO_PWR_ACTIVE); /* 4.3 s later */
}

/* ---------------- battery ---------------- */

static void test_batt_bounds(void)
{
    CHECK(avo_batt_percent_from_mv(4300) == 100);
    CHECK(avo_batt_percent_from_mv(4200) == 100);
    CHECK(avo_batt_percent_from_mv(3300) == 0);
    CHECK(avo_batt_percent_from_mv(0) == 0);
}

static void test_batt_monotonic(void)
{
    int prev = -1;
    for (int mv = 3300; mv <= 4200; mv += 10) {
        int pct = avo_batt_percent_from_mv(mv);
        CHECK(pct >= prev);
        prev = pct;
    }
}

/* ---------------- settings ---------------- */

static void test_settings_defaults_are_sane(void)
{
    avo_settings_t s;
    avo_settings_defaults(&s);
    CHECK(s.version == AVO_SETTINGS_VERSION);
    CHECK(s.theme == AVO_THEME_CLEAN);
    CHECK(s.h24 == true);
    CHECK(s.brightness >= 5 && s.brightness <= 100);
    CHECK(!avo_settings_sanitize(&s, 4));
}

static void test_settings_sanitize_clamps(void)
{
    avo_settings_t s;
    avo_settings_defaults(&s);
    s.brightness = 0;
    s.theme = 9;
    s.face = 42;
    s.screen_timeout_s = 1;
    s.utc_offset_min = 5000;
    s.wifi_ssid[AVO_WIFI_SSID_MAX - 1] = 'x';
    CHECK(avo_settings_sanitize(&s, 4));
    CHECK(s.brightness == 5);
    CHECK(s.theme == AVO_THEME_CLEAN);
    CHECK(s.face == 0);
    CHECK(s.screen_timeout_s == 5);
    CHECK(s.utc_offset_min == 840);
    CHECK(s.wifi_ssid[AVO_WIFI_SSID_MAX - 1] == '\0');
}

int main(void)
{
    RUN(test_hc_center_slot_is_origin);
    RUN(test_hc_ring1_slots_are_one_spacing_away);
    RUN(test_hc_ring2_slots_are_farther_than_ring1);
    RUN(test_hc_slots_are_unique);
    RUN(test_hc_neighbours_never_overlap);
    RUN(test_hc_scale_full_inside_radius);
    RUN(test_hc_scale_min_outside_fade);
    RUN(test_hc_scale_is_monotonic_in_band);
    RUN(test_fmt_hm_24h);
    RUN(test_fmt_hm_12h);
    RUN(test_fmt_hour_min_padded);
    RUN(test_fmt_wday_day);
    RUN(test_fmt_long_date);
    RUN(test_fmt_never_overflows);
    RUN(test_fmt_stopwatch);
    RUN(test_pwr_starts_active);
    RUN(test_pwr_dims_then_aod);
    RUN(test_pwr_off_when_aod_disabled);
    RUN(test_pwr_activity_wakes);
    RUN(test_pwr_forced_sleep);
    RUN(test_pwr_survives_tick_wraparound);
    RUN(test_batt_bounds);
    RUN(test_batt_monotonic);
    RUN(test_settings_defaults_are_sane);
    RUN(test_settings_sanitize_clamps);
    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
