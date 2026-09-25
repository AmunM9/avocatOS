/* Host unit tests for phase 3: sounds, motion (tap, flick, steps), activity,
 * alarms, weather/location parsing and the settings upgrade. */
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

#define PI 3.14159265f

/* Deterministic noise in [-amp, amp]. */
static uint32_t s_rng = 12345;
static float noise(float amp)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return ((float)((s_rng >> 8) & 0xFFFF) / 32767.5f - 1.0f) * amp;
}

/* ================================================================ sounds */

static size_t render_all(avo_sound_t id, uint8_t vol, int16_t *peak, int16_t *first)
{
    avo_synth_t s;
    avo_synth_start(&s, id, vol);
    int16_t buf[256];
    size_t total = 0;
    *peak = 0;
    for (int guard = 0; guard < 2000 && !avo_synth_done(&s); guard++) {
        size_t n = avo_synth_render(&s, buf, 256);
        for (size_t i = 0; i < n; i++) {
            if (total + i == 0) *first = buf[i];
            if (abs(buf[i]) > *peak) *peak = (int16_t)abs(buf[i]);
        }
        total += n;
        if (n < 256) break;
    }
    return total;
}

static void test_sound_length_matches_notes(void)
{
    int16_t peak, first;
    size_t n = render_all(AVO_SOUND_NOTIFY, 80, &peak, &first);
    size_t want = (size_t)avo_sound_duration_ms(AVO_SOUND_NOTIFY) * AVO_SYNTH_RATE / 1000;
    CHECK(n >= want - 4 && n <= want + 4);
    CHECK(avo_sound_duration_ms(AVO_SOUND_CLICK) < 30);
    CHECK(avo_sound_duration_ms(AVO_SOUND_ALARM) > 500);
}

static void test_sound_volume_scales_and_starts_soft(void)
{
    int16_t loud, soft, zero, first;
    render_all(AVO_SOUND_NOTIFY, 100, &loud, &first);
    CHECK(abs(first) < 2000);                 /* attack ramp: no pop */
    render_all(AVO_SOUND_NOTIFY, 30, &soft, &first);
    render_all(AVO_SOUND_NOTIFY, 0, &zero, &first);
    CHECK(loud > 20000);
    CHECK(soft > 0 && soft < loud / 4);       /* perceptual curve */
    CHECK(zero == 0);
}

static void test_sound_loops_until_stopped(void)
{
    CHECK(avo_sound_loops(AVO_SOUND_ALARM));
    CHECK(avo_sound_loops(AVO_SOUND_RING));
    CHECK(!avo_sound_loops(AVO_SOUND_CLICK));
    avo_synth_t s;
    avo_synth_start(&s, AVO_SOUND_ALARM, 60);
    int16_t buf[512];
    size_t pass = (size_t)avo_sound_duration_ms(AVO_SOUND_ALARM) * AVO_SYNTH_RATE / 1000;
    size_t total = 0;
    while (total < pass * 3) {
        CHECK(avo_synth_render(&s, buf, 512) == 512);
        total += 512;
    }
    CHECK(!avo_synth_done(&s));
}

static void test_sound_ends_with_silence(void)
{
    avo_synth_t s;
    avo_synth_start(&s, AVO_SOUND_CLICK, 100);
    int16_t buf[1024];
    size_t n = avo_synth_render(&s, buf, 1024);
    CHECK(n < 1024);
    CHECK(avo_synth_done(&s));
    CHECK(buf[1023] == 0 && buf[n] == 0);
    CHECK(avo_synth_render(&s, buf, 64) == 0);
}

/* ================================================================ double tap */

static int feed_taps(const float *spikes_at_ms, int nspikes, float spike_g, int total_ms, float noise_g)
{
    avo_tap_t t = { 0 };
    int fired = 0;
    for (int ms = 0; ms < total_ms; ms += 10) {
        float a[3] = { noise(noise_g), noise(noise_g), 1.0f + noise(noise_g) };
        for (int k = 0; k < nspikes; k++) {
            if (ms == (int)spikes_at_ms[k]) a[2] += spike_g;
            if (ms == (int)spikes_at_ms[k] + 10) a[2] -= spike_g * 0.5f;
        }
        fired += avo_tap_feed(&t, a);
    }
    return fired;
}

static void test_double_tap_detected(void)
{
    const float at[] = { 1000, 1220 };
    CHECK(feed_taps(at, 2, 0.9f, 2500, 0.01f) == 1);
}

static void test_single_or_triple_tap_ignored(void)
{
    const float one[] = { 1000 };
    CHECK(feed_taps(one, 1, 0.9f, 2500, 0.01f) == 0);
    const float three[] = { 1000, 1200, 1400 };
    CHECK(feed_taps(three, 3, 0.9f, 2500, 0.01f) == 0);
}

static void test_taps_too_far_apart_ignored(void)
{
    const float far[] = { 1000, 1700 };
    CHECK(feed_taps(far, 2, 0.9f, 2500, 0.01f) == 0);
}

static void test_taps_while_moving_ignored(void)
{
    const float at[] = { 1000, 1220 };
    CHECK(feed_taps(at, 2, 0.9f, 2500, 0.25f) == 0); /* wrist not still */
}

/* ================================================================ wrist flick */

static int feed_gyro(float (*fn)(int ms, int axis), int total_ms)
{
    avo_flick_t f = { 0 };
    int fired = 0;
    for (int ms = 0; ms < total_ms; ms += 10) {
        float g[3] = { fn(ms, 0), fn(ms, 1), fn(ms, 2) };
        fired += avo_flick_feed(&f, g);
    }
    return fired;
}

/* turn away for 150 ms then back for 150 ms, starting at 1 s */
static float flick_shape(int ms, int axis)
{
    if (axis != 0) return noise(5);
    if (ms >= 1000 && ms < 1150) return 420.0f * sinf(PI * (float)(ms - 1000) / 150.0f);
    if (ms >= 1150 && ms < 1300) return -380.0f * sinf(PI * (float)(ms - 1150) / 150.0f);
    return noise(5);
}

static float turn_only(int ms, int axis)
{
    if (axis == 1 && ms >= 1000 && ms < 1200) return 400.0f * sinf(PI * (float)(ms - 1000) / 200.0f);
    return noise(5);
}

static float arm_swing(int ms, int axis)
{
    return axis == 0 ? 260.0f * sinf(2.0f * PI * (float)ms / 1000.0f) : noise(20);
}

static void test_flick_detected(void) { CHECK(feed_gyro(flick_shape, 2500) == 1); }
static void test_turn_without_return_ignored(void) { CHECK(feed_gyro(turn_only, 2500) == 0); }
static void test_walking_swing_ignored(void) { CHECK(feed_gyro(arm_swing, 6000) == 0); }

/* ================================================================ steps */

static uint32_t walk(float hz, float amp_g, int seconds, float noise_g)
{
    avo_steps_t s;
    avo_steps_reset(&s);
    uint32_t steps = 0;
    for (int i = 0; i < seconds * AVO_MOTION_HZ; i++) {
        float t = (float)i / AVO_MOTION_HZ;
        float a[3] = { noise(noise_g), 0.2f + noise(noise_g),
                       0.98f + amp_g * sinf(2.0f * PI * hz * t) + noise(noise_g) };
        steps += avo_steps_feed(&s, a);
    }
    return steps;
}

static void test_steps_walking(void)
{
    uint32_t n = walk(1.8f, 0.35f, 60, 0.03f);
    CHECK(n >= 100 && n <= 112);   /* 108 expected */
}

static void test_steps_running(void)
{
    uint32_t n = walk(2.8f, 0.8f, 30, 0.05f);
    CHECK(n >= 78 && n <= 86);     /* 84 expected */
}

static void test_steps_ignore_still_and_noise(void)
{
    CHECK(walk(1.8f, 0.0f, 60, 0.02f) == 0);
    CHECK(walk(1.8f, 0.03f, 60, 0.01f) == 0); /* tiny wobble, not walking */
}

static void test_steps_ignore_short_bursts(void)
{
    avo_steps_t s;
    avo_steps_reset(&s);
    uint32_t steps = 0;
    for (int i = 0; i < 20 * AVO_MOTION_HZ; i++) {
        float t = (float)i / AVO_MOTION_HZ;
        /* three isolated shakes, 3 s apart */
        bool shake = (i % 300) < 60;
        float a[3] = { 0, 0, 1.0f + (shake ? 0.5f * sinf(2 * PI * 2.0f * t) : 0.0f) };
        steps += avo_steps_feed(&s, a);
    }
    CHECK(steps == 0);
}

/* ================================================================ activity */

static avo_time_t at(int d, int h, int m)
{
    return (avo_time_t){ .year = 2026, .month = 9, .day = d, .hour = h, .min = m };
}

static void test_activity_minutes_and_stand(void)
{
    avo_activity_t a;
    avo_time_t t = at(25, 8, 0);
    avo_activity_reset(&a, &t);
    for (int m = 0; m < 20; m++) {         /* brisk walk 08:00-08:19 */
        t = at(25, 8, m);
        avo_activity_add(&a, &t, 110);
    }
    t = at(25, 9, 5);
    avo_activity_add(&a, &t, 40);          /* a short walk at 09:05 */
    t = at(25, 9, 6);
    avo_activity_add(&a, &t, 0);
    CHECK(a.steps == 20 * 110 + 40);
    CHECK(a.exercise_min == 20);
    CHECK(avo_activity_stand_hours(&a) == 2);
}

static void test_activity_resets_at_midnight(void)
{
    avo_activity_t a;
    avo_time_t t = at(25, 23, 59);
    avo_activity_reset(&a, &t);
    avo_activity_add(&a, &t, 500);
    t = at(26, 0, 0);
    avo_activity_add(&a, &t, 10);
    CHECK(a.steps == 10);
    CHECK(a.day == 20260926);
    CHECK(a.exercise_min == 0);
}

/* ================================================================ alarms */

static void test_alarm_due(void)
{
    avo_alarm_t once = { 7, 30, 0, true };
    avo_time_t t = { .year = 2026, .month = 9, .day = 25, .wday = 5, .hour = 7, .min = 30, .sec = 12 };
    CHECK(avo_alarm_due(&once, &t));
    once.enabled = false;
    CHECK(!avo_alarm_due(&once, &t));
    avo_alarm_t weekdays = { 7, 30, AVO_DAYS_WEEKDAYS, true };
    CHECK(avo_alarm_due(&weekdays, &t));      /* Friday */
    t.wday = 6;
    CHECK(!avo_alarm_due(&weekdays, &t));     /* Saturday */
    t.wday = 5;
    t.min = 31;
    CHECK(!avo_alarm_due(&weekdays, &t));
}

static void test_alarm_next(void)
{
    avo_alarms_t s;
    avo_alarms_defaults(&s);
    s.count = 3;
    s.list[0] = (avo_alarm_t){ 6, 0, AVO_DAYS_WEEKDAYS, true };
    s.list[1] = (avo_alarm_t){ 22, 15, 0, true };
    s.list[2] = (avo_alarm_t){ 21, 0, 0, false };
    avo_time_t now = { .year = 2026, .month = 9, .day = 25, .wday = 5, .hour = 21, .min = 30 };
    int mins = -1;
    CHECK(avo_alarms_next(&s, &now, &mins) == 1 && mins == 45);
    s.list[1].enabled = false;
    /* Friday 21:30 -> Monday 06:00 */
    CHECK(avo_alarms_next(&s, &now, &mins) == 0 && mins == (2 * 24 + 8) * 60 + 30);
    s.list[0].enabled = false;
    CHECK(avo_alarms_next(&s, &now, &mins) == -1);
}

static void test_alarm_days_text(void)
{
    char b[40];
    avo_fmt_alarm_days(b, sizeof b, 0);
    CHECK_STR(b, "Una vez");
    avo_fmt_alarm_days(b, sizeof b, AVO_DAYS_ALL);
    CHECK_STR(b, "Todos los días");
    avo_fmt_alarm_days(b, sizeof b, AVO_DAYS_WEEKDAYS);
    CHECK_STR(b, "Entre semana");
    avo_fmt_alarm_days(b, sizeof b, AVO_DAYS_WEEKEND);
    CHECK_STR(b, "Fines de semana");
    avo_fmt_alarm_days(b, sizeof b, (1 << 1) | (1 << 3) | (1 << 5));
    CHECK_STR(b, "L X V");
    avo_fmt_alarm_days(b, sizeof b, (1 << 0) | (1 << 6) | (1 << 2));
    CHECK_STR(b, "M S D");
}

static void test_alarms_sanitize(void)
{
    avo_alarms_t s;
    avo_alarms_defaults(&s);
    CHECK(!avo_alarms_sanitize(&s));
    s.count = 40;
    s.list[0] = (avo_alarm_t){ 25, 61, 0xFF, true };
    CHECK(avo_alarms_sanitize(&s));
    CHECK(s.count == AVO_ALARM_MAX);
    CHECK(s.list[0].hour == 23 && s.list[0].min == 59 && s.list[0].days == AVO_DAYS_ALL);
}

/* ================================================================ weather */

static const char OPEN_METEO[] =
    "{\"latitude\":4.6045694,\"longitude\":-74.05252,\"utc_offset_seconds\":-18000,"
    "\"current_units\":{\"time\":\"iso8601\",\"temperature_2m\":\"°C\",\"weather_code\":\"wmo code\",\"is_day\":\"\"},"
    "\"current\":{\"time\":\"2026-09-25T12:45\",\"interval\":900,\"temperature_2m\":19.3,\"weather_code\":51,\"is_day\":1},"
    "\"daily_units\":{\"time\":\"iso8601\",\"weather_code\":\"wmo code\"},"
    "\"daily\":{\"time\":[\"2026-09-25\",\"2026-09-26\",\"2026-09-27\",\"2026-09-28\"],"
    "\"weather_code\":[55,53,80,3],\"temperature_2m_max\":[19.6,19.4,18.4,20.1],"
    "\"temperature_2m_min\":[11.1,11.0,-10.3,10.1]}}";

static void test_weather_parse(void)
{
    avo_weather_t w;
    CHECK(avo_weather_parse(OPEN_METEO, &w));
    CHECK(w.valid);
    CHECK(fabsf(w.temp - 19.3f) < 0.01f);
    CHECK(w.code == 51 && w.is_day);
    CHECK(w.days == 4);
    CHECK(w.day_code[2] == 80 && w.day_code[3] == 3);
    CHECK(fabsf(w.day_max[0] - 19.6f) < 0.01f);
    CHECK(fabsf(w.day_min[2] + 10.3f) < 0.01f);
}

static void test_weather_parse_rejects_errors(void)
{
    avo_weather_t w;
    CHECK(!avo_weather_parse("{\"error\":true,\"reason\":\"bad\"}", &w));
    CHECK(!avo_weather_parse("", &w));
    CHECK(!w.valid);
}

static void test_geo_parse(void)
{
    const char *j = "{\n  \"success\": true,\n  \"city\": \"Bogot\\u00e1\",\n"
                    "  \"latitude\": 4.609713,\n  \"longitude\": -74.081754\n}";
    double lat, lon;
    char city[32];
    CHECK(avo_geo_parse(j, &lat, &lon, city, sizeof city));
    CHECK(fabs(lat - 4.609713) < 1e-6 && fabs(lon + 74.081754) < 1e-6);
    CHECK_STR(city, "Bogotá");
    CHECK(!avo_geo_parse("{\"success\": false, \"message\": \"limit\"}", &lat, &lon, city, sizeof city));
}

static void test_json_string_escapes(void)
{
    char b[16];
    CHECK(avo_json_string("{\"a\":\"x\\\"y\\/z\"}", "a", b, sizeof b));
    CHECK_STR(b, "x\"y/z");
    CHECK(avo_json_string("{\"a\":\"abcdefghijklmnopqrstuvwxyz\"}", "a", b, 8));
    CHECK_STR(b, "abcdefg");
    CHECK(!avo_json_string("{\"a\":12}", "a", b, sizeof b));
    CHECK(!avo_json_string("{\"b\":\"x\"}", "a", b, sizeof b));
}

static void test_wmo_codes(void)
{
    CHECK(avo_wmo_kind(0) == AVO_WX_CLEAR);
    CHECK(avo_wmo_kind(2) == AVO_WX_PARTLY);
    CHECK(avo_wmo_kind(3) == AVO_WX_CLOUDY);
    CHECK(avo_wmo_kind(45) == AVO_WX_FOG);
    CHECK(avo_wmo_kind(53) == AVO_WX_DRIZZLE);
    CHECK(avo_wmo_kind(63) == AVO_WX_RAIN);
    CHECK(avo_wmo_kind(81) == AVO_WX_RAIN);
    CHECK(avo_wmo_kind(75) == AVO_WX_SNOW);
    CHECK(avo_wmo_kind(95) == AVO_WX_STORM);
    CHECK_STR(avo_wmo_text(0), "Despejado");
    CHECK_STR(avo_wmo_text(61), "Lluvia");
    CHECK(strlen(avo_wmo_text(1234)) > 0);
}

/* ================================================================ settings v2 */

static void test_settings_upgrade_from_v1(void)
{
    avo_settings_t s;
    avo_settings_defaults(&s);
    CHECK(s.volume > 0 && s.double_tap && s.step_goal == 8000);
    /* a v1 blob: same prefix, version 1, the new fields are garbage */
    s.version = 1;
    snprintf(s.wifi_ssid, sizeof s.wifi_ssid, "Casa");
    memset(&s.volume, 0xAB, sizeof s - offsetof(avo_settings_t, volume));
    CHECK(avo_settings_upgrade(&s, AVO_SETTINGS_V1_SIZE));
    CHECK(s.version == AVO_SETTINGS_VERSION);
    CHECK_STR(s.wifi_ssid, "Casa");
    CHECK(s.volume == 70 && s.double_tap && s.wrist_flick && s.weather && s.step_goal == 8000);
}

static void test_settings_upgrade_rejects_unknown(void)
{
    avo_settings_t s;
    avo_settings_defaults(&s);
    CHECK(avo_settings_upgrade(&s, sizeof s));
    CHECK(!avo_settings_upgrade(&s, sizeof s - 3));
    s.version = 9;
    CHECK(!avo_settings_upgrade(&s, sizeof s));
    s.version = 1;
    CHECK(!avo_settings_upgrade(&s, sizeof s)); /* v1 must be the short blob */
}

static void test_settings_sanitize_v2_fields(void)
{
    avo_settings_t s;
    avo_settings_defaults(&s);
    s.volume = 250;
    s.step_goal = 5;
    CHECK(avo_settings_sanitize(&s, 4));
    CHECK(s.volume == 100 && s.step_goal == 1000);
}

static void test_fmt_thousands(void)
{
    char b[16];
    avo_fmt_thousands(b, sizeof b, 0);
    CHECK_STR(b, "0");
    avo_fmt_thousands(b, sizeof b, 999);
    CHECK_STR(b, "999");
    avo_fmt_thousands(b, sizeof b, 6482);
    CHECK_STR(b, "6.482");
    avo_fmt_thousands(b, sizeof b, 12500000);
    CHECK_STR(b, "12.500.000");
    avo_fmt_thousands(b, 4, 6482);
    CHECK_STR(b, "6.4");
}

int main(void)
{
    RUN(test_fmt_thousands);
    RUN(test_sound_length_matches_notes);
    RUN(test_sound_volume_scales_and_starts_soft);
    RUN(test_sound_loops_until_stopped);
    RUN(test_sound_ends_with_silence);
    RUN(test_double_tap_detected);
    RUN(test_single_or_triple_tap_ignored);
    RUN(test_taps_too_far_apart_ignored);
    RUN(test_taps_while_moving_ignored);
    RUN(test_flick_detected);
    RUN(test_turn_without_return_ignored);
    RUN(test_walking_swing_ignored);
    RUN(test_steps_walking);
    RUN(test_steps_running);
    RUN(test_steps_ignore_still_and_noise);
    RUN(test_steps_ignore_short_bursts);
    RUN(test_activity_minutes_and_stand);
    RUN(test_activity_resets_at_midnight);
    RUN(test_alarm_due);
    RUN(test_alarm_next);
    RUN(test_alarm_days_text);
    RUN(test_alarms_sanitize);
    RUN(test_weather_parse);
    RUN(test_weather_parse_rejects_errors);
    RUN(test_geo_parse);
    RUN(test_json_string_escapes);
    RUN(test_wmo_codes);
    RUN(test_settings_upgrade_from_v1);
    RUN(test_settings_upgrade_rejects_unknown);
    RUN(test_settings_sanitize_v2_fields);
    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
