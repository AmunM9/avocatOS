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

int main(void)
{
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
