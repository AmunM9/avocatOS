/*
 * Alarms at runtime (ring at hh:mm, snooze 9 min like watchOS), the timer's
 * "done" alert, and small helpers for glanceable weather data.
 * The CPU never sleeps (no light sleep configured), so the 1 Hz clock of the
 * UI is enough to ring on time even with the screen off.
 */
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"

#define SNOOZE_MS (9u * 60u * 1000u)
#define ALARM_RING_MS (3u * 60u * 1000u)  /* unanswered: snooze by itself */
#define TIMER_RING_MS (90u * 1000u)

static avo_alarms_t s_alarms;
static int s_last_minute = -1;
static struct {
    bool active;
    uint32_t until_ms;
    uint8_t hour, min;             /* of the alarm that was snoozed */
} s_snooze;
static uint8_t s_ringing_hour, s_ringing_min;

void avo_alarms_init(void)
{
    if (!avo_hal_alarms_load(&s_alarms)) {
        avo_alarms_defaults(&s_alarms);
    }
}

avo_alarms_t *avo_alarms(void) { return &s_alarms; }

void avo_alarms_commit(void)
{
    avo_alarms_sanitize(&s_alarms);
    avo_hal_alarms_save(&s_alarms);
}

static void alarm_stop(void)
{
    s_snooze.active = false;
    avo_toast(AVO_SYM_CLOCK, "Alarma detenida");
}

static void alarm_snooze(void)
{
    s_snooze.active = true;
    s_snooze.until_ms = avo_hal_millis() + SNOOZE_MS;
    s_snooze.hour = s_ringing_hour;
    s_snooze.min = s_ringing_min;
    char hm[12], msg[48];
    bool snoozed;
    avo_alarms_next_text(hm, sizeof hm, &snoozed);
    snprintf(msg, sizeof msg, "Pospuesta %u min · suena %s", SNOOZE_MS / 60000u, hm);
    avo_toast(AVO_SYM_CLOCK, msg);
}

static void ring(uint8_t hour, uint8_t min, const char *cap)
{
    s_ringing_hour = hour;
    s_ringing_min = min;
    avo_time_t t = { .hour = hour, .min = min };
    char big[12];
    avo_fmt_hm(big, sizeof big, &t, avo_settings()->h24);
    avo_nav_wake();
    avo_overlay_alert(&(avo_alert_t){
        .title = "Alarma", .big = big, .caption = cap,
        .symbol = AVO_SYM_CLOCK, .hue = AVO_HUE_SOLAR,
        .primary = "Detener", .secondary = "Posponer",
        .on_primary = alarm_stop, .on_secondary = alarm_snooze,
        .on_double_tap = alarm_snooze, .on_dismiss = alarm_snooze,
        .sound = AVO_SOUND_ALARM, .timeout_ms = ALARM_RING_MS,
    });
}

void avo_alarms_tick(const avo_time_t *t)
{
    if (s_snooze.active && (int32_t)(avo_hal_millis() - s_snooze.until_ms) >= 0) {
        s_snooze.active = false;
        ring(s_snooze.hour, s_snooze.min, "Pospuesta 9 min");
    }
    int minute = t->hour * 60 + t->min;
    if (minute == s_last_minute) {
        return;
    }
    bool first = s_last_minute < 0;
    s_last_minute = minute;
    if (first) {
        return; /* booting at 07:30 must not ring the 07:30 alarm again */
    }
    for (int i = 0; i < s_alarms.count; i++) {
        avo_alarm_t *a = &s_alarms.list[i];
        if (avo_alarm_due(a, t)) {
            char cap[32];
            avo_fmt_alarm_days(cap, sizeof cap, a->days);
            if (a->days == 0) {
                a->enabled = false; /* one-shot alarms turn themselves off */
                avo_alarms_commit();
            }
            ring(a->hour, a->min, cap);
            return;
        }
    }
}

bool avo_alarms_next_text(char *out, size_t len, bool *snoozed)
{
    avo_time_t t = { 0 };
    *snoozed = s_snooze.active;
    if (s_snooze.active) {
        avo_hal_time_now(&t);
        uint32_t left_min = (s_snooze.until_ms - avo_hal_millis() + 59999u) / 60000u;
        int m = t.hour * 60 + t.min + (int)left_min;
        t.hour = (m / 60) % 24;
        t.min = m % 60;
    } else {
        avo_time_t now;
        avo_hal_time_now(&now);
        int i = avo_alarms_next(&s_alarms, &now, NULL);
        if (i < 0) {
            return false;
        }
        t.hour = s_alarms.list[i].hour;
        t.min = s_alarms.list[i].min;
    }
    avo_fmt_hm(out, len, &t, avo_settings()->h24);
    return true;
}

/* ---------------------------------------------------------------- timer */

static void (*s_timer_repeat)(void);

static void timer_stop(void) {}

void avo_alert_timer_done(uint32_t minutes, void (*repeat)(void))
{
    s_timer_repeat = repeat;
    char cap[24];
    snprintf(cap, sizeof cap, "%u min", (unsigned)minutes);
    avo_nav_wake();
    avo_overlay_alert(&(avo_alert_t){
        .title = "Temporizador", .big = "¡Listo!", .caption = cap,
        .symbol = AVO_SYM_HOURGLASS, .hue = AVO_HUE_IRIS,
        .primary = "Detener", .secondary = "Repetir",
        .on_primary = timer_stop, .on_secondary = s_timer_repeat,
        .on_double_tap = timer_stop, .on_dismiss = timer_stop,
        .sound = AVO_SOUND_TIMER, .timeout_ms = TIMER_RING_MS,
    });
}

/* ---------------------------------------------------------------- weather glyphs */

const char *avo_wx_symbol(int code, bool is_day)
{
    switch (avo_wmo_kind(code)) {
    case AVO_WX_CLEAR: return is_day ? AVO_SYM_SUN : AVO_SYM_MOON;
    case AVO_WX_PARTLY: return is_day ? AVO_SYM_CLOUD_SUN : AVO_SYM_CLOUD_MOON;
    case AVO_WX_FOG: return AVO_SYM_SMOG;
    case AVO_WX_DRIZZLE: return AVO_SYM_CLOUD_RAIN;
    case AVO_WX_RAIN: return AVO_SYM_SHOWERS;
    case AVO_WX_SNOW: return AVO_SYM_SNOW;
    case AVO_WX_STORM: return LV_SYMBOL_CHARGE;
    default: return AVO_SYM_CLOUD;
    }
}
