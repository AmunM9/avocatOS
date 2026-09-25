/* Daily activity totals and alarm scheduling. */
#include <stdio.h>
#include <string.h>
#include "avo_core.h"

/* ---------------------------------------------------------------- activity */
#define EXERCISE_STEPS_PER_MIN 100  /* brisk walking cadence                  */
#define STAND_STEPS_PER_MIN 30      /* moving around for a minute of the hour */

static uint32_t day_key(const avo_time_t *t)
{
    return (uint32_t)(t->year * 10000 + t->month * 100 + t->day);
}

void avo_activity_reset(avo_activity_t *a, const avo_time_t *t)
{
    memset(a, 0, sizeof *a);
    a->day = day_key(t);
    a->minute = (int16_t)(t->hour * 60 + t->min);
}

void avo_activity_add(avo_activity_t *a, const avo_time_t *t, uint32_t steps)
{
    if (a->day != day_key(t)) {
        avo_activity_reset(a, t);
    }
    int16_t minute = (int16_t)(t->hour * 60 + t->min);
    if (minute != a->minute) {
        if (a->minute_steps >= EXERCISE_STEPS_PER_MIN) {
            a->exercise_min++;
        }
        a->minute = minute;
        a->minute_steps = 0;
    }
    a->steps += steps;
    a->minute_steps = (uint16_t)(a->minute_steps + steps);
    if (a->minute_steps >= STAND_STEPS_PER_MIN && t->hour >= 0 && t->hour < 24) {
        a->stand_mask |= 1u << t->hour;
    }
}

int avo_activity_stand_hours(const avo_activity_t *a)
{
    int n = 0;
    for (uint32_t m = a->stand_mask; m; m &= m - 1) {
        n++;
    }
    return n;
}

/* ---------------------------------------------------------------- alarms */
#define MIN_PER_DAY (24 * 60)

void avo_alarms_defaults(avo_alarms_t *s)
{
    memset(s, 0, sizeof *s);
    s->version = AVO_ALARMS_VERSION;
}

bool avo_alarms_sanitize(avo_alarms_t *s)
{
    bool changed = false;
    if (s->version != AVO_ALARMS_VERSION) {
        s->version = AVO_ALARMS_VERSION;
        changed = true;
    }
    if (s->count > AVO_ALARM_MAX) {
        s->count = AVO_ALARM_MAX;
        changed = true;
    }
    for (int i = 0; i < s->count; i++) {
        avo_alarm_t *a = &s->list[i];
        if (a->hour > 23) { a->hour = 23; changed = true; }
        if (a->min > 59) { a->min = 59; changed = true; }
        if (a->days & ~AVO_DAYS_ALL) { a->days &= AVO_DAYS_ALL; changed = true; }
    }
    return changed;
}

bool avo_alarm_due(const avo_alarm_t *a, const avo_time_t *t)
{
    if (!a->enabled || a->hour != t->hour || a->min != t->min) {
        return false;
    }
    return a->days == 0 || (a->days & (1u << t->wday));
}

int avo_alarms_next(const avo_alarms_t *s, const avo_time_t *now, int *minutes_until)
{
    int best = -1, best_min = 0;
    int now_min = now->hour * 60 + now->min;
    for (int i = 0; i < s->count; i++) {
        const avo_alarm_t *a = &s->list[i];
        if (!a->enabled) {
            continue;
        }
        for (int d = 0; d <= 7; d++) {
            int delta = d * MIN_PER_DAY + a->hour * 60 + a->min - now_min;
            int wday = (now->wday + d) % 7;
            if (delta > 0 && (a->days == 0 || (a->days & (1u << wday)))) {
                if (best < 0 || delta < best_min) {
                    best = i;
                    best_min = delta;
                }
                break;
            }
        }
    }
    if (best >= 0 && minutes_until) {
        *minutes_until = best_min;
    }
    return best;
}

size_t avo_fmt_alarm_days(char *out, size_t len, uint8_t days)
{
    days &= AVO_DAYS_ALL;
    if (days == 0) return (size_t)snprintf(out, len, "Una vez");
    if (days == AVO_DAYS_ALL) return (size_t)snprintf(out, len, "Todos los días");
    if (days == AVO_DAYS_WEEKDAYS) return (size_t)snprintf(out, len, "Entre semana");
    if (days == AVO_DAYS_WEEKEND) return (size_t)snprintf(out, len, "Fines de semana");
    /* Monday first, Spanish initials (X = miércoles) */
    static const char LETTER[7] = { 'D', 'L', 'M', 'X', 'J', 'V', 'S' };
    size_t w = 0;
    for (int k = 1; k <= 7 && w + 2 < len; k++) {
        int d = k % 7;
        if (days & (1u << d)) {
            if (w) out[w++] = ' ';
            out[w++] = LETTER[d];
        }
    }
    out[w] = '\0';
    return w;
}
