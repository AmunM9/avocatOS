/*
 * Actividad: three rings like watchOS, measured with the accelerometer only
 * (no heart rate sensor): steps, exercise minutes (a minute with a brisk
 * walking cadence) and stand hours (hours with a minute of moving around).
 */
#include <stdio.h>
#include "avo_ui_internal.h"

#define RING_W 26
#define RING_GAP 6
#define RING_OUTER 262
#define REFRESH_MS 2000

enum { RING_STEPS, RING_EXERCISE, RING_STAND, RING_N };

static const avo_hue_t RING_HUE[RING_N] = { AVO_HUE_EMBER, AVO_HUE_LIME, AVO_HUE_SKY };

static struct {
    lv_timer_t *timer;
    lv_obj_t *ring[RING_N];
    lv_obj_t *value[RING_N];
} ui;

static lv_obj_t *ring(lv_obj_t *parent, int i)
{
    int32_t d = RING_OUTER - i * 2 * (RING_W + RING_GAP);
    lv_color_t c = avo_hue(RING_HUE[i]);
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, d, d);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 70 + i * (RING_W + RING_GAP));
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 1000);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, RING_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, RING_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_mix(c, lv_color_black(), 60), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

static lv_obj_t *legend(lv_obj_t *parent, int i, const char *symbol, const char *name)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *top = avo_label(r, &avo_font_22, avo_hue(RING_HUE[i]), "");
    lv_label_set_text_fmt(top, "%s %s", symbol, name);
    ui.value[i] = avo_label(r, &avo_font_26, avo_pal()->label, "");
    return r;
}

static int permille(uint32_t v, uint32_t goal)
{
    uint32_t p = goal ? v * 1000u / goal : 0;
    return (int)(p > 1000 ? 1000 : p); /* the ring closes at 100 % */
}

static void refresh(void)
{
    avo_activity_t a;
    avo_hal_activity(&a);
    uint32_t goal = avo_settings()->step_goal;
    int stand = avo_activity_stand_hours(&a);
    lv_arc_set_value(ui.ring[RING_STEPS], permille(a.steps, goal));
    lv_arc_set_value(ui.ring[RING_EXERCISE], permille(a.exercise_min, AVO_EXERCISE_GOAL_MIN));
    lv_arc_set_value(ui.ring[RING_STAND], permille((uint32_t)stand, AVO_STAND_GOAL_H));
    char n[16];
    avo_fmt_thousands(n, sizeof n, a.steps);
    lv_label_set_text(ui.value[RING_STEPS], n);
    lv_label_set_text_fmt(ui.value[RING_EXERCISE], "%u/%d min", a.exercise_min, AVO_EXERCISE_GOAL_MIN);
    lv_label_set_text_fmt(ui.value[RING_STAND], "%d/%d h", stand, AVO_STAND_GOAL_H);
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    refresh();
}

static void activity_build(lv_obj_t *scr)
{
    avo_header_clock(scr);
    lv_obj_t *t = avo_label(scr, &avo_font_30, avo_theme_is_avocado() ? avo_pal()->accent : avo_hue(AVO_HUE_EMBER),
                            "Actividad");
    lv_obj_set_pos(t, AVO_PAD + 14, AVO_TOP + 6);
    for (int i = 0; i < RING_N; i++) {
        ui.ring[i] = ring(scr, i);
    }
    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, AVO_W - 2 * AVO_PAD, LV_SIZE_CONTENT);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    legend(row, RING_STEPS, AVO_SYM_WALK, "Pasos");
    legend(row, RING_EXERCISE, AVO_SYM_RUN, "Ejercicio");
    legend(row, RING_STAND, AVO_SYM_CHILD, "De pie");
    refresh();
    ui.timer = lv_timer_create(timer_cb, REFRESH_MS, NULL);
}

static void activity_leave(void)
{
    if (ui.timer) {
        lv_timer_delete(ui.timer);
    }
    lv_memzero(&ui, sizeof ui);
}

const avo_app_t AVO_APP_ACTIVITY = {
    .name = "Actividad", .symbol = AVO_SYM_RUN, .hue = AVO_HUE_EMBER,
    .build = activity_build, .leave = activity_leave,
};
