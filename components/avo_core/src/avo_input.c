/* Swipe recognizer + calendar helpers. */
#include <stdlib.h>
#include "avo_core.h"

#define SWIPE_MIN_PX 36          /* shortest accepted travel                    */
#define SWIPE_SURE_PX 80         /* this long is a swipe at any speed           */
#define SWIPE_MIN_SPEED_X1000 120 /* 0.12 px/ms for short flicks                */
#define SWIPE_DOMINANCE_X10 14   /* major axis >= 1.4x the minor axis           */
#define SWIPE_MAX_MS 1500        /* holding longer is not a swipe               */

void avo_swipe_reset(avo_swipe_tracker_t *t)
{
    *t = (avo_swipe_tracker_t){ 0 };
}

static bool classify(const avo_swipe_tracker_t *t, uint32_t dur, avo_swipe_t *out)
{
    int dx = t->x - t->x0;
    int dy = t->y - t->y0;
    int ax = abs(dx), ay = abs(dy);
    int major = ax > ay ? ax : ay;
    int minor = ax > ay ? ay : ax;
    if (dur > SWIPE_MAX_MS || major < SWIPE_MIN_PX || major * 10 < minor * SWIPE_DOMINANCE_X10) {
        return false;
    }
    uint32_t speed_x1000 = (uint32_t)major * 1000u / (dur ? dur : 1);
    if (major < SWIPE_SURE_PX && speed_x1000 < SWIPE_MIN_SPEED_X1000) {
        return false;
    }
    *out = (avo_swipe_t){
        .dir = ax > ay ? (dx > 0 ? AVO_SWIPE_RIGHT : AVO_SWIPE_LEFT) : (dy > 0 ? AVO_SWIPE_DOWN : AVO_SWIPE_UP),
        .start_x = t->x0, .start_y = t->y0,
        .dx = (int16_t)dx, .dy = (int16_t)dy,
        .duration_ms = dur,
    };
    return true;
}

bool avo_swipe_feed(avo_swipe_tracker_t *t, bool pressed, int16_t x, int16_t y, uint32_t now_ms, avo_swipe_t *out)
{
    if (pressed) {
        if (!t->down) {
            t->down = true;
            t->x0 = x;
            t->y0 = y;
            t->t0 = now_ms;
        }
        t->x = x;
        t->y = y;
        return false;
    }
    if (!t->down) {
        return false;
    }
    t->down = false;
    return classify(t, now_ms - t->t0, out);
}

/* Howard Hinnant's days_from_civil: days since 1970-01-01. */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t avo_time_to_epoch(const avo_time_t *t, int16_t offset_min)
{
    int64_t days = days_from_civil(t->year, t->month, t->day);
    return days * 86400 + t->hour * 3600 + t->min * 60 + t->sec - (int64_t)offset_min * 60;
}

int avo_weekday(int year, int month, int day)
{
    int64_t days = days_from_civil(year, month, day);
    return (int)(((days % 7) + 11) % 7); /* 1970-01-01 was a Thursday (4) */
}
