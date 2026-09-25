/*
 * Motion gestures and step counting from the QMI8658 sampled at 100 Hz.
 * All times are in samples (10 ms each).
 *
 * Double tap: two sharp knocks on the case 80-450 ms apart, on a wrist that
 * was still before and stays still after (so walking or typing never fires).
 * Wrist flick: a fast turn away and back from a still wrist (watchOS 26).
 * Steps: peaks of the smoothed acceleration magnitude crossing a threshold
 * that follows the last second of motion, counted only while a walk
 * detector says the motion is strong and periodic.
 * Raise to wake: see avo_raise_feed().
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

/* ---------------------------------------------------------------- steps
 * Windowed peak detection gated by a walk detector, the combination that
 * worked best for wrist devices in published comparisons: the last 2.56 s
 * must be strong enough (standard deviation) and periodic at a walking or
 * running rhythm (normalized autocorrelation), otherwise no step counts.
 * Desk work produces jolts, but not a steady rhythm. */
#define STEP_LP_ALPHA 0.2f      /* ~3.5 Hz low-pass                          */
#define STEP_WINDOW 100         /* threshold follows the last second         */
#define STEP_MIN_SPAN 0.18f     /* g peak to peak: less is not walking       */
#define STEP_HYST 0.15f         /* of the span, around the threshold         */
#define STEP_MIN_GAP 25         /* at most 4 steps per second                */
#define STEP_MAX_GAP 200        /* a 2 s pause ends the walk                 */
#define STEP_CONFIRM 7          /* steps in a row before counting starts     */
#define STEP_PENDING_MAX 10
#define STEP_MAX_CV 0.2f        /* interval variation of a real walk         */
#define WALK_DECIMATE 2         /* detector runs at 50 Hz                    */
#define WALK_EVERY 50           /* re-evaluated every 0.5 s (input samples)  */
#define WALK_MIN_STD 0.07f      /* g                                         */
#define WALK_MIN_CORR 0.5f
#define WALK_MIN_LAG 15         /* 0.3 s: fastest step                       */
#define WALK_MAX_LAG 65         /* 1.3 s: slowest stride (two steps)         */

void avo_steps_reset(avo_steps_t *s)
{
    memset(s, 0, sizeof *s);
}

float avo_autocorr_peak(const float *x, int n, int min_lag, int max_lag, int *best_lag)
{
    float mean = 0;
    for (int i = 0; i < n; i++) mean += x[i];
    mean /= (float)n;
    float best = 0;
    *best_lag = 0;
    for (int k = min_lag; k <= max_lag && k < n; k++) {
        float num = 0, e0 = 0, e1 = 0;
        for (int i = 0; i + k < n; i++) {
            float a = x[i] - mean, b = x[i + k] - mean;
            num += a * b;
            e0 += a * a;
            e1 += b * b;
        }
        if (e0 <= 1e-9f || e1 <= 1e-9f) {
            continue;
        }
        float r = num / sqrtf(e0 * e1);
        if (r > best) {
            best = r;
            *best_lag = k;
        }
    }
    return best;
}

static void walk_update(avo_steps_t *s)
{
    if (s->filled < AVO_STEPS_WIN) {
        s->walking = false;
        return;
    }
    float ordered[AVO_STEPS_WIN], mean = 0, var = 0;
    for (int i = 0; i < AVO_STEPS_WIN; i++) {
        ordered[i] = s->buf[(s->head + i) % AVO_STEPS_WIN]; /* oldest first */
        mean += ordered[i];
    }
    mean /= AVO_STEPS_WIN;
    for (int i = 0; i < AVO_STEPS_WIN; i++) var += (ordered[i] - mean) * (ordered[i] - mean);
    float std = sqrtf(var / AVO_STEPS_WIN);
    int lag;
    s->walking = std >= WALK_MIN_STD &&
                 avo_autocorr_peak(ordered, AVO_STEPS_WIN, WALK_MIN_LAG, WALK_MAX_LAG, &lag) >= WALK_MIN_CORR;
}

#define GAPS_N 6

static void push_gap(avo_steps_t *s, uint32_t gap)
{
    memmove(&s->gaps[1], &s->gaps[0], (GAPS_N - 1) * sizeof s->gaps[0]);
    s->gaps[0] = (uint16_t)gap;
    if (s->ngaps < GAPS_N) s->ngaps++;
}

static float mean_gap(const avo_steps_t *s)
{
    float m = 0;
    for (int i = 0; i < s->ngaps; i++) m += s->gaps[i];
    return s->ngaps ? m / s->ngaps : 0;
}

/* Walking has a steady rhythm: the last intervals vary by < STEP_MAX_CV. */
static bool regular(const avo_steps_t *s)
{
    if (s->ngaps < GAPS_N) {
        return false;
    }
    float m = mean_gap(s), v = 0;
    for (int i = 0; i < GAPS_N; i++) v += (s->gaps[i] - m) * (s->gaps[i] - m);
    return sqrtf(v / GAPS_N) < STEP_MAX_CV * m;
}

static void restart_walk(avo_steps_t *s)
{
    s->counting = false;
    s->pending = 1;
    s->ngaps = 0;
}

static uint32_t step_candidate(avo_steps_t *s)
{
    bool first = s->last_step_at == 0;
    uint32_t gap = s->n - s->last_step_at;
    if (!first && gap < STEP_MIN_GAP) {
        return 0; /* too soon: same step */
    }
    s->last_step_at = s->n;
    if (first || gap > STEP_MAX_GAP) {
        restart_walk(s);
        return 0;
    }
    if (s->counting) {
        float m = mean_gap(s);
        push_gap(s, gap);
        if (s->walking && gap > m * 0.6f && gap < m * 1.6f) {
            return 1;
        }
        restart_walk(s); /* rhythm broken: confirm again */
        push_gap(s, gap);
        return 0;
    }
    push_gap(s, gap);
    if (s->pending < STEP_PENDING_MAX) {
        s->pending++;
    }
    if (s->walking && s->pending >= STEP_CONFIRM && regular(s)) {
        uint32_t add = s->pending; /* the steps held back while checking */
        s->counting = true;
        s->pending = 0;
        return add;
    }
    return 0;
}

uint32_t avo_steps_feed(avo_steps_t *s, const float a[3])
{
    float m = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (s->n == 0) {
        s->lp = s->win_max = s->win_min = m;
    }
    s->n++;
    s->lp += STEP_LP_ALPHA * (m - s->lp);
    if (s->n % WALK_DECIMATE == 0) {
        s->buf[s->head] = s->lp;
        s->head = (uint16_t)((s->head + 1) % AVO_STEPS_WIN);
        if (s->filled < AVO_STEPS_WIN) s->filled++;
    }
    if (s->n % WALK_EVERY == 0) {
        walk_update(s);
    }
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

/* ---------------------------------------------------------------- raise to wake */
#define RAISE_LP 0.35f          /* gravity low-pass at 25 Hz                  */
#define RAISE_VIEW_COS 0.6f     /* screen within 53 deg of facing up          */
#define RAISE_TURN_COS 0.819f   /* turned >= 35 deg ...                       */
#define RAISE_TURN_FROM 10      /* ... compared with 0.4-1.0 s ago            */
#define RAISE_STEADY 5          /* then 200 ms still                          */
#define RAISE_STEADY_G 0.12f
#define RAISE_COOLDOWN 50       /* 2 s                                        */

static float norm3(const float v[3]) { return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

bool avo_raise_feed(avo_raise_t *r, const float a[3])
{
    if (!r->primed) {
        memcpy(r->g, a, sizeof r->g);
        r->primed = true;
    }
    float d[3], mag = norm3(a);
    for (int k = 0; k < 3; k++) {
        d[k] = a[k] - r->g[k];
        r->g[k] += RAISE_LP * d[k];
    }
    bool steady = norm3(d) < RAISE_STEADY_G && mag > 0.8f && mag < 1.2f;
    r->steady = steady ? (uint8_t)(r->steady < 255 ? r->steady + 1 : 255) : 0;
    float gn = norm3(r->g);
    float u[3] = { r->g[0] / gn, r->g[1] / gn, r->g[2] / gn };
    memcpy(r->hist[r->head], u, sizeof u);
    r->head = (uint8_t)((r->head + 1) % AVO_RAISE_HIST);
    if (r->filled < AVO_RAISE_HIST) r->filled++;
    if (r->cooldown) {
        r->cooldown--;
        return false;
    }
    if (r->filled < AVO_RAISE_HIST || r->steady < RAISE_STEADY || fabsf(u[2]) < RAISE_VIEW_COS) {
        return false;
    }
    /* oldest entries = 1.0 s ago; look back to 0.4 s ago */
    for (int i = 0; i < AVO_RAISE_HIST - RAISE_TURN_FROM; i++) {
        const float *h = r->hist[(r->head + i) % AVO_RAISE_HIST];
        if (u[0] * h[0] + u[1] * h[1] + u[2] * h[2] < RAISE_TURN_COS) {
            r->cooldown = RAISE_COOLDOWN;
            return true;
        }
    }
    return false;
}
