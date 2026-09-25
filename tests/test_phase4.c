/* Host unit tests for phase 4: sun times and moon phase (Órbita face).
 * References: Open-Meteo sunrise/sunset, USNO moon phases 2026 (UT). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avo_core.h"

static int g_failed, g_passed;
#define CHECK(c) do { if (c) g_passed++; else { g_failed++; fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) == 0) g_passed++; else { g_failed++; \
    fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)
#define RUN(fn) do { fprintf(stderr, "- %s\n", #fn); fn(); } while (0)

#define NEAR_MIN(got, h, m) (abs((got) - ((h) * 60 + (m))) <= 3)

static void test_sun_bogota(void)
{
    avo_sun_t s;
    CHECK(avo_sun_times(4.61, -74.08, 2026, 9, 25, -300, &s));
    CHECK(NEAR_MIN(s.rise, 5, 44));
    CHECK(NEAR_MIN(s.set, 17, 50));
    CHECK(!s.polar_day && !s.polar_night);
}

static void test_sun_london_summer(void)
{
    avo_sun_t s;
    CHECK(avo_sun_times(51.5, -0.12, 2025, 6, 21, 60, &s));
    CHECK(NEAR_MIN(s.rise, 4, 43));
    CHECK(NEAR_MIN(s.set, 21, 21));
}

static void test_sun_sydney(void)
{
    avo_sun_t s;
    CHECK(avo_sun_times(-33.87, 151.21, 2026, 9, 25, 600, &s));
    CHECK(NEAR_MIN(s.rise, 5, 41));
    CHECK(NEAR_MIN(s.set, 17, 53));
}

static void test_sun_polar(void)
{
    avo_sun_t s;
    CHECK(!avo_sun_times(69.65, 18.96, 2026, 6, 21, 120, &s));
    CHECK(s.polar_day && !s.polar_night);
    CHECK(!avo_sun_times(69.65, 18.96, 2026, 12, 21, 60, &s));
    CHECK(s.polar_night && !s.polar_day);
}

static int64_t utc(int y, int mo, int d, int h, int mi)
{
    avo_time_t t = { .year = y, .month = mo, .day = d, .hour = h, .min = mi };
    return avo_time_to_epoch(&t, 0);
}

static void test_moon_phases_2026(void)
{
    double age = avo_moon_age(utc(2026, 9, 26, 16, 49));        /* full moon */
    CHECK(fabs(age - 14.77) < 0.8);
    CHECK(avo_moon_illumination(age) > 0.97);
    CHECK_STR(avo_moon_phase_name(age), "Luna llena");
    age = avo_moon_age(utc(2026, 9, 11, 3, 27));                 /* new moon */
    CHECK(age < 0.8 || age > 28.7);
    CHECK(avo_moon_illumination(age) < 0.03);
    CHECK_STR(avo_moon_phase_name(age), "Luna nueva");
    age = avo_moon_age(utc(2026, 9, 18, 20, 44));                /* first quarter */
    CHECK(fabs(avo_moon_illumination(age) - 0.5) < 0.12);
    CHECK_STR(avo_moon_phase_name(age), "Cuarto creciente");
    age = avo_moon_age(utc(2026, 10, 3, 13, 25));                /* last quarter */
    CHECK_STR(avo_moon_phase_name(age), "Cuarto menguante");
    CHECK(avo_moon_waxing(avo_moon_age(utc(2026, 9, 15, 0, 0))));
    CHECK(!avo_moon_waxing(avo_moon_age(utc(2026, 9, 30, 0, 0))));
}

static void test_moon_age_range(void)
{
    for (int d = 1; d <= 28; d++) {
        double a = avo_moon_age(utc(2026, 2, d, 12, 0));
        CHECK(a >= 0 && a < AVO_MOON_SYNODIC);
    }
    CHECK(avo_moon_age(utc(1990, 1, 1, 0, 0)) >= 0); /* before the reference */
}

static void test_day_fraction(void)
{
    avo_sun_t s = { .rise = 6 * 60, .set = 18 * 60 };
    CHECK(fabs(avo_sun_path_angle(&s, 6 * 60) - 180.0) < 0.01);   /* sunrise: left   */
    CHECK(fabs(avo_sun_path_angle(&s, 12 * 60) - 270.0) < 0.01);  /* noon: top       */
    CHECK(fabs(avo_sun_path_angle(&s, 18 * 60) - 360.0) < 0.01);  /* sunset: right   */
    CHECK(fabs(avo_sun_path_angle(&s, 0) - 90.0) < 0.01);         /* midnight: bottom */
    s.polar_day = true;
    CHECK(avo_sun_path_angle(&s, 0) >= 180.0);                     /* always above    */
}

/* ---------------- battery estimate ---------------- */

static void test_battery_estimate_linear(void)
{
    avo_batt_hist_t h = { 0 };
    int pct = 80;
    for (uint32_t m = 0; m <= 200; m += 5) {                 /* 1 % every 10 min */
        avo_batt_hist_add(&h, m, pct - (int)(m / 10), false);
    }
    int left = avo_batt_minutes_left(&h);                     /* 60 % left       */
    CHECK(left >= 570 && left <= 630);
}

static void test_battery_estimate_needs_data(void)
{
    avo_batt_hist_t h = { 0 };
    CHECK(avo_batt_minutes_left(&h) == -1);
    avo_batt_hist_add(&h, 0, 90, false);
    avo_batt_hist_add(&h, 5, 90, false);
    CHECK(avo_batt_minutes_left(&h) == -1);                   /* too early        */
    for (uint32_t m = 10; m <= 60; m += 5) avo_batt_hist_add(&h, m, 90, false);
    CHECK(avo_batt_minutes_left(&h) == -1);                   /* not dropping     */
}

static void test_battery_charging_resets(void)
{
    avo_batt_hist_t h = { 0 };
    for (uint32_t m = 0; m <= 60; m += 5) avo_batt_hist_add(&h, m, 70 - (int)(m / 5), false);
    CHECK(avo_batt_minutes_left(&h) > 0);
    avo_batt_hist_add(&h, 65, 58, true);
    CHECK(avo_batt_minutes_left(&h) == -1);
}

static void test_battery_estimate_caps(void)
{
    avo_batt_hist_t h = { 0 };
    for (uint32_t m = 0; m <= 600; m += 5) avo_batt_hist_add(&h, m, 100 - (int)(m / 300), false);
    int left = avo_batt_minutes_left(&h);
    CHECK(left > 0 && left <= 7 * 24 * 60);
}

/* ---------------- settings v4 ---------------- */

static void test_settings_upgrade_to_v4(void)
{
    avo_settings_t s;
    avo_settings_defaults(&s);
    CHECK(!s.low_power);
    s.version = 3;
    s.artwork = false;
    memset(&s.low_power, 0xAB, sizeof s - offsetof(avo_settings_t, low_power));
    CHECK(avo_settings_upgrade(&s, AVO_SETTINGS_V3_SIZE));
    CHECK(s.version == AVO_SETTINGS_VERSION && !s.artwork && !s.low_power);
    avo_settings_defaults(&s);
    s.version = 2;
    CHECK(avo_settings_upgrade(&s, AVO_SETTINGS_V2_SIZE));
    CHECK(s.artwork && !s.low_power);
}

/* ---------------- dithering ---------------- */

static double mean_red8(const uint16_t *px, int n)
{
    double sum = 0;
    for (int i = 0; i < n; i++) sum += ((px[i] >> 11) & 0x1F) * 255.0 / 31.0;
    return sum / n;
}

static void test_dither_keeps_mean(void)
{
    enum { W = 16, H = 16 };
    static uint8_t rgb[W * H * 3];
    static uint16_t out[W * H];
    for (int v = 0; v <= 255; v += 17) {
        for (int i = 0; i < W * H; i++) { rgb[3 * i] = (uint8_t)v; rgb[3 * i + 1] = 0; rgb[3 * i + 2] = 0; }
        avo_dither_rgb888_to_rgb565(rgb, out, W, H);
        CHECK(fabs(mean_red8(out, W * H) - v) < 2.5); /* truncation drifts up to 8 */
    }
}

static void test_dither_extremes_exact(void)
{
    uint8_t rgb[4 * 4 * 3];
    uint16_t out[16];
    memset(rgb, 255, sizeof rgb);
    avo_dither_rgb888_to_rgb565(rgb, out, 4, 4);
    for (int i = 0; i < 16; i++) CHECK(out[i] == 0xFFFF);
    memset(rgb, 0, sizeof rgb);
    avo_dither_rgb888_to_rgb565(rgb, out, 4, 4);
    for (int i = 0; i < 16; i++) CHECK(out[i] == 0x0000);
}

static void test_dither_gradient_is_smooth(void)
{
    /* a slow horizontal ramp: column averages must rise steadily, not in
     * 8-level stairs */
    enum { W = 256, H = 4 };
    static uint8_t rgb[W * H * 3];
    static uint16_t out[W * H];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t v = (uint8_t)(64 + x / 4);                /* 64..127 */
            rgb[3 * (y * W + x)] = v; rgb[3 * (y * W + x) + 1] = v; rgb[3 * (y * W + x) + 2] = v;
        }
    avo_dither_rgb888_to_rgb565(rgb, out, W, H);
    int flat_runs = 0;
    double prev = -1;
    for (int x = 0; x < W; x += 16) {
        uint16_t block[64];
        int k = 0;
        for (int y = 0; y < H; y++) for (int dx = 0; dx < 16; dx++) block[k++] = out[y * W + x + dx];
        double m = mean_red8(block, 64);
        if (prev >= 0 && fabs(m - prev) < 0.5) flat_runs++;
        prev = m;
    }
    CHECK(flat_runs <= 2);
}

int main(void)
{
    RUN(test_dither_keeps_mean);
    RUN(test_dither_extremes_exact);
    RUN(test_dither_gradient_is_smooth);
    RUN(test_battery_estimate_linear);
    RUN(test_battery_estimate_needs_data);
    RUN(test_battery_charging_resets);
    RUN(test_battery_estimate_caps);
    RUN(test_settings_upgrade_to_v4);
    RUN(test_sun_bogota);
    RUN(test_sun_london_summer);
    RUN(test_sun_sydney);
    RUN(test_sun_polar);
    RUN(test_moon_phases_2026);
    RUN(test_moon_age_range);
    RUN(test_day_fraction);
    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
