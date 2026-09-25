/*
 * Motion gestures and step counting from the QMI8658 sampled at 100 Hz.
 * All times are in samples (10 ms each).
 *
 * Double tap: two sharp knocks on the case 80-450 ms apart, on a wrist that
 * was still before and stays still after (so walking or typing never fires).
 * Wrist flick: a fast turn away and back from a still wrist (watchOS 26).
 * Steps: peaks of the smoothed acceleration magnitude crossing a threshold
 * that follows the last second of motion; four regular steps must happen
 * before counting starts, which rejects isolated shakes.
 */
#include <math.h>
#include <string.h>
#include "avo_core.h"

/* ---------------------------------------------------------------- double tap */
#define TAP_HIGH 0.35f          /* g change per sample that marks a knock      */
#define TAP_QUIET 0.10f         /* g change per sample of a still wrist        */
#define TAP_CALM_BEFORE 20      /* 200 ms still before the first knock         */
#define TAP_SAME 6              /* 60 ms: ringing of the same knock            */
#define TAP_GAP_MIN 8           /* 80 ms                                       */
#define TAP_GAP_MAX 45          /* 450 ms                                      */
#define TAP_CALM_AFTER 25       /* 250 ms still after (rejects triple taps)    */
#define TAP_COOLDOWN 50

enum { TAP_IDLE, TAP_ONE, TAP_TWO };

static float max_abs_diff(const float a[3], const float b[3])
{
    float m = 0;
    for (int i = 0; i < 3; i++) {
        float d = fabsf(a[i] - b[i]);
        if (d > m) {
            m = d;
        }
    }
    return m;
}

bool avo_tap_feed(avo_tap_t *t, const float a[3])
{
    t->n++;
    if (!t->primed) {
        memcpy(t->prev, a, sizeof t->prev);
        t->primed = true;
        return false;
    }
    float j = max_abs_diff(a, t->prev);
    memcpy(t->prev, a, sizeof t->prev);
    bool high = j > TAP_HIGH;
    bool knock = high && !t->high; /* rising edge: one knock spans a few samples */
    t->high = high;
    uint16_t calm_before = t->calm;
    t->calm = j < TAP_QUIET ? (uint16_t)(t->calm + 1) : 0;
    if (t->n < t->cooldown_until) {
        return false;
    }
    bool new_knock = knock && t->n - t->last_peak_at > TAP_SAME;
    switch (t->state) {
    case TAP_IDLE:
        if (knock && calm_before >= TAP_CALM_BEFORE) {
            t->state = TAP_ONE;
            t->first_at = t->last_peak_at = t->n;
        }
        break;
    case TAP_ONE:
        if (new_knock) {
            uint32_t gap = t->n - t->first_at;
            t->state = (gap >= TAP_GAP_MIN && gap <= TAP_GAP_MAX) ? TAP_TWO : TAP_IDLE;
            t->last_peak_at = t->n;
        } else if (t->n - t->first_at > TAP_GAP_MAX) {
            t->state = TAP_IDLE;
        }
        break;
    case TAP_TWO:
        if (new_knock) {
            t->state = TAP_IDLE; /* a third knock: not a double tap */
        } else if (t->calm >= TAP_CALM_AFTER) {
            t->state = TAP_IDLE;
            t->cooldown_until = t->n + TAP_COOLDOWN;
            return true;
        }
        break;
    }
    return false;
}

/* ---------------------------------------------------------------- wrist flick */
#define FLICK_OUT_DPS 300.0f
#define FLICK_BACK_DPS 200.0f
#define FLICK_CALM_DPS 60.0f
#define FLICK_CALM_BEFORE 30    /* 300 ms still before                  */
#define FLICK_RISE_MAX 12       /* still -> fast turn within 120 ms     */
#define FLICK_OUT_MAX 35        /* the turn must reverse within 350 ms  */
#define FLICK_BACK_MAX 60       /* and settle within 600 ms             */
#define FLICK_CALM_AFTER 20
#define FLICK_COOLDOWN 80

enum { FLICK_IDLE, FLICK_OUT, FLICK_BACK };

bool avo_flick_feed(avo_flick_t *f, const float g[3])
{
    f->n++;
    float peak = 0;
    int axis = 0;
    for (int i = 0; i < 3; i++) {
        if (fabsf(g[i]) > peak) {
            peak = fabsf(g[i]);
            axis = i;
        }
    }
    bool calm = peak < FLICK_CALM_DPS;
    if (!calm && f->calm >= FLICK_CALM_BEFORE) {
        f->armed_at = f->n; /* a still wrist starts to turn */
    }
    f->calm = calm ? (uint16_t)(f->calm + 1) : 0;
    if (f->n < f->cooldown_until) {
        return false;
    }
    float along = f->sign * g[f->axis];
    switch (f->state) {
    case FLICK_IDLE:
        if (peak > FLICK_OUT_DPS && f->armed_at && f->n - f->armed_at <= FLICK_RISE_MAX) {
            f->state = FLICK_OUT;
            f->axis = (uint8_t)axis;
            f->sign = g[axis] > 0 ? 1 : -1;
            f->start_at = f->n;
        }
        break;
    case FLICK_OUT:
        if (along < -FLICK_BACK_DPS) {
            f->state = FLICK_BACK;
            f->back_at = f->n;
        } else if (f->n - f->start_at > FLICK_OUT_MAX) {
            f->state = FLICK_IDLE;
        }
        break;
    case FLICK_BACK:
        if (along > FLICK_OUT_DPS) {
            f->state = FLICK_IDLE; /* shaking back and forth */
        } else if (f->calm >= FLICK_CALM_AFTER) {
            f->state = FLICK_IDLE;
            f->cooldown_until = f->n + FLICK_COOLDOWN;
            return true;
        } else if (f->n - f->back_at > FLICK_BACK_MAX) {
            f->state = FLICK_IDLE;
        }
        break;
    }
    return false;
}

/* ---------------------------------------------------------------- steps */
#define STEP_LP_ALPHA 0.2f      /* ~3.5 Hz low-pass                          */
#define STEP_WINDOW 100         /* threshold follows the last second         */
#define STEP_MIN_SPAN 0.12f     /* g peak to peak: less is not walking       */
#define STEP_HYST 0.15f         /* of the span, around the threshold         */
#define STEP_MIN_GAP 25         /* at most 4 steps per second                */
#define STEP_MAX_GAP 200        /* a 2 s pause ends the walk                 */
#define STEP_CONFIRM 4

void avo_steps_reset(avo_steps_t *s)
{
    memset(s, 0, sizeof *s);
}

static uint32_t step_candidate(avo_steps_t *s)
{
    uint32_t gap = s->n - s->last_step_at;
    if (s->last_step_at != 0 && gap < STEP_MIN_GAP) {
        return 0; /* too soon: same step */
    }
    uint32_t add = 0;
    if (s->last_step_at == 0 || gap > STEP_MAX_GAP) {
        s->counting = false;
        s->pending = 1;
    } else if (s->counting) {
        add = 1;
    } else if (++s->pending >= STEP_CONFIRM) {
        s->counting = true;
        add = s->pending;
        s->pending = 0;
    }
    s->last_step_at = s->n;
    return add;
}

uint32_t avo_steps_feed(avo_steps_t *s, const float a[3])
{
    float m = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (s->n == 0) {
        s->lp = s->win_max = s->win_min = m;
    }
    s->n++;
    s->lp += STEP_LP_ALPHA * (m - s->lp);
    if (s->lp > s->win_max) s->win_max = s->lp;
    if (s->lp < s->win_min) s->win_min = s->lp;
    if (s->n % STEP_WINDOW == 0) {
        s->thresh = (s->win_max + s->win_min) / 2;
        s->span = s->win_max - s->win_min;
        s->win_max = s->win_min = s->lp;
    }
    if (s->span < STEP_MIN_SPAN) {
        s->above = false;
        return 0;
    }
    float h = s->span * STEP_HYST;
    if (!s->above && s->lp > s->thresh + h) {
        s->above = true;
    } else if (s->above && s->lp < s->thresh - h) {
        s->above = false;
        return step_candidate(s);
    }
    return 0;
}
