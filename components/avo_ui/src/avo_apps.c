/* Built-in apps: Cronómetro, Temporizador, Linterna, Nivel, Música, Esferas,
 * plus the app registry that feeds the honeycomb grid. */
#include <math.h>
#include <stdio.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define STOPWATCH_REFRESH_MS 50
#define MAX_LAPS 20
#define LEVEL_REFRESH_MS 33
#define LEVEL_RADIUS 150
#define TIMER_CHECK_MS 250
#define TIMER_ALARM_BEEPS 6

/* ------------------------------------------------------------------ common */

static lv_obj_t *circle_button(lv_obj_t *parent, int32_t d, lv_color_t fill, lv_color_t fg, const char *text)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, fill, 0);
    lv_obj_set_style_transform_scale(b, 236, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(b, d / 2, 0);
    lv_obj_set_style_transform_pivot_y(b, d / 2, 0);
    lv_obj_t *l = avo_label(b, &avo_font_26, fg, text);
    lv_obj_center(l);
    return b;
}

static lv_color_t tint(lv_color_t c)
{
    return lv_color_mix(c, lv_color_black(), 80);
}

/* ================================================================= Cronómetro */

static struct {
    bool running;
    uint32_t start_ms;
    uint32_t acc_ms;
    uint32_t laps[MAX_LAPS];
    int lap_count;
} sw_state;

static struct {
    lv_timer_t *timer;
    lv_obj_t *time, *left, *right, *laps;
} sw_ui;

bool avo_stopwatch_running(void) { return sw_state.running; }

uint32_t avo_stopwatch_elapsed(void)
{
    return sw_state.acc_ms + (sw_state.running ? avo_hal_millis() - sw_state.start_ms : 0);
}

static void sw_set_button(lv_obj_t *b, lv_color_t c, const char *text)
{
    lv_obj_set_style_bg_color(b, tint(c), 0);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, text);
}

static void sw_refresh_buttons(void)
{
    if (sw_state.running) {
        sw_set_button(sw_ui.right, avo_hue(AVO_HUE_EMBER), "Detener");
        sw_set_button(sw_ui.left, avo_pal()->label2, "Vuelta");
    } else {
        sw_set_button(sw_ui.right, avo_hue(AVO_HUE_MINT), "Iniciar");
        sw_set_button(sw_ui.left, avo_pal()->label2, "Reiniciar");
    }
}

static void sw_refresh_time(void)
{
    char buf[20];
    uint32_t ms = avo_stopwatch_elapsed();
    avo_fmt_stopwatch(buf, sizeof buf, ms);
    lv_obj_set_style_text_font(sw_ui.time, ms >= 3600000u ? &avo_font_40 : &avo_font_digits_76, 0);
    lv_label_set_text(sw_ui.time, buf);
}

static void sw_rebuild_laps(void)
{
    lv_obj_clean(sw_ui.laps);
    for (int i = sw_state.lap_count - 1; i >= 0; i--) {
        char k[24], v[20];
        snprintf(k, sizeof k, "Vuelta %d", i + 1);
        uint32_t prev = i > 0 ? sw_state.laps[i - 1] : 0;
        avo_fmt_stopwatch(v, sizeof v, sw_state.laps[i] - prev);
        lv_obj_t *r = avo_row(sw_ui.laps, AVO_HUE_SOLAR, NULL, k, v);
        lv_obj_set_style_min_height(r, 60, 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void sw_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (sw_state.running) {
        sw_refresh_time();
    }
}

static void sw_right_cb(lv_event_t *e)
{
    (void)e;
    uint32_t now = avo_hal_millis();
    if (sw_state.running) {
        sw_state.acc_ms += now - sw_state.start_ms;
        sw_state.running = false;
    } else {
        sw_state.start_ms = now;
        sw_state.running = true;
    }
    avo_hal_click();
    sw_refresh_buttons();
    sw_refresh_time();
}

static void sw_left_cb(lv_event_t *e)
{
    (void)e;
    avo_hal_click();
    if (sw_state.running) {
        if (sw_state.lap_count < MAX_LAPS) {
            sw_state.laps[sw_state.lap_count++] = avo_stopwatch_elapsed();
        }
    } else {
        lv_memzero(&sw_state, sizeof sw_state);
        sw_refresh_time();
    }
    sw_rebuild_laps();
}

static void stopwatch_build(lv_obj_t *scr)
{
    avo_header_clock(scr);
    lv_obj_t *page = avo_page_create(scr, "Cronómetro", AVO_HUE_SOLAR);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    sw_ui.time = avo_label(page, &avo_font_digits_76, avo_pal()->label, "");
    lv_obj_set_style_pad_ver(sw_ui.time, 18, 0);

    lv_obj_t *row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 10, 0);
    sw_ui.left = circle_button(row, 132, avo_pal()->surface, avo_pal()->label, "");
    sw_ui.right = circle_button(row, 132, avo_pal()->surface, avo_pal()->label, "");
    lv_obj_add_event_cb(sw_ui.left, sw_left_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(sw_ui.right, sw_right_cb, LV_EVENT_CLICKED, NULL);

    sw_ui.laps = lv_obj_create(page);
    lv_obj_remove_style_all(sw_ui.laps);
    lv_obj_set_size(sw_ui.laps, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sw_ui.laps, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sw_ui.laps, 6, 0);
    lv_obj_set_style_pad_top(sw_ui.laps, 12, 0);

    sw_refresh_time();
    sw_refresh_buttons();
    sw_rebuild_laps();
    sw_ui.timer = lv_timer_create(sw_timer_cb, STOPWATCH_REFRESH_MS, NULL);
}

static void stopwatch_leave(void)
{
    if (sw_ui.timer) {
        lv_timer_delete(sw_ui.timer);
    }
    lv_memzero(&sw_ui, sizeof sw_ui);
}

const avo_app_t AVO_APP_STOPWATCH = {
    .name = "Cronómetro", .symbol = AVO_SYM_STOPWATCH, .hue = AVO_HUE_SOLAR,
    .build = stopwatch_build, .leave = stopwatch_leave,
};

/* ================================================================= Temporizador */

static const uint16_t PRESET_MIN[] = { 1, 3, 5, 10, 15, 30 };
#define PRESET_N (sizeof PRESET_MIN / sizeof PRESET_MIN[0])

static struct {
    bool running;
    uint32_t end_ms;
    uint32_t total_ms;
    int beeps_left;
    lv_timer_t *watch; /* global: lives while a countdown exists */
} tm_state;

static struct {
    lv_obj_t *screen, *arc, *remain, *cancel;
    lv_obj_t *presets;
} tm_ui;

static void timer_build(lv_obj_t *scr);

static void tm_show(void)
{
    if (!tm_ui.screen) {
        return;
    }
    bool active = tm_state.running || tm_state.beeps_left > 0;
    if (active) {
        lv_obj_add_flag(tm_ui.presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(tm_ui.arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(tm_ui.cancel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(tm_ui.presets, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(tm_ui.arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(tm_ui.cancel, LV_OBJ_FLAG_HIDDEN);
    }
    if (!active) {
        return;
    }
    uint32_t now = avo_hal_millis();
    uint32_t left = tm_state.running && (int32_t)(tm_state.end_ms - now) > 0 ? tm_state.end_ms - now : 0;
    char buf[16];
    if (left == 0) {
        snprintf(buf, sizeof buf, "¡Listo!");
    } else {
        unsigned s = (unsigned)((left + 999) / 1000);
        snprintf(buf, sizeof buf, "%u:%02u", s / 60, s % 60);
    }
    lv_label_set_text(tm_ui.remain, buf);
    lv_arc_set_value(tm_ui.arc, tm_state.total_ms ? (int32_t)(left * 1000 / tm_state.total_ms) : 0);
}

static void tm_watch_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = avo_hal_millis();
    if (tm_state.running && (int32_t)(tm_state.end_ms - now) <= 0) {
        tm_state.running = false;
        tm_state.beeps_left = TIMER_ALARM_BEEPS;
        avo_ui_post_wake();
        if (!tm_ui.screen) {
            avo_nav_app(&AVO_APP_TIMER);
        }
    }
    if (tm_state.beeps_left > 0) {
        avo_hal_click();
        tm_state.beeps_left--;
    }
    tm_show();
    if (!tm_state.running && tm_state.beeps_left == 0) {
        lv_timer_delete(tm_state.watch);
        tm_state.watch = NULL;
        tm_show();
    }
}

static void tm_start(uint32_t minutes)
{
    tm_state.total_ms = minutes * 60000u;
    tm_state.end_ms = avo_hal_millis() + tm_state.total_ms;
    tm_state.running = true;
    tm_state.beeps_left = 0;
    if (!tm_state.watch) {
        tm_state.watch = lv_timer_create(tm_watch_cb, TIMER_CHECK_MS, NULL);
    }
    tm_show();
}

static void preset_cb(lv_event_t *e)
{
    avo_hal_click();
    tm_start(PRESET_MIN[(int)(intptr_t)lv_event_get_user_data(e)]);
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    avo_hal_click();
    tm_state.running = false;
    tm_state.beeps_left = 0;
    tm_show();
}

static void timer_build(lv_obj_t *scr)
{
    tm_ui.screen = scr;
    avo_header_clock(scr);
    lv_obj_t *t = avo_label(scr, &avo_font_30, avo_theme_is_avocado() ? avo_pal()->accent : avo_hue(AVO_HUE_IRIS), "Temporizador");
    lv_obj_set_pos(t, AVO_PAD + 14, AVO_TOP + 6);

    tm_ui.presets = lv_obj_create(scr);
    lv_obj_remove_style_all(tm_ui.presets);
    lv_obj_set_size(tm_ui.presets, AVO_W - 2 * AVO_PAD, LV_SIZE_CONTENT);
    lv_obj_align(tm_ui.presets, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_flex_flow(tm_ui.presets, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(tm_ui.presets, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(tm_ui.presets, 18, 0);
    lv_obj_add_flag(tm_ui.presets, LV_OBJ_FLAG_GESTURE_BUBBLE);
    for (size_t i = 0; i < PRESET_N; i++) {
        char buf[8];
        snprintf(buf, sizeof buf, "%u", PRESET_MIN[i]);
        lv_color_t c = avo_hue(AVO_HUE_IRIS);
        lv_obj_t *b = circle_button(tm_ui.presets, 108, tint(c), c, buf);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &avo_font_40, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(b, preset_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    tm_ui.arc = lv_arc_create(scr);
    lv_obj_set_size(tm_ui.arc, 300, 300);
    lv_obj_align(tm_ui.arc, LV_ALIGN_CENTER, 0, -10);
    lv_arc_set_rotation(tm_ui.arc, 270);
    lv_arc_set_bg_angles(tm_ui.arc, 0, 360);
    lv_arc_set_range(tm_ui.arc, 0, 1000);
    lv_obj_remove_style(tm_ui.arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(tm_ui.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(tm_ui.arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(tm_ui.arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(tm_ui.arc, tint(avo_hue(AVO_HUE_IRIS)), LV_PART_MAIN);
    lv_obj_set_style_arc_color(tm_ui.arc, avo_theme_is_avocado() ? avo_pal()->accent : avo_hue(AVO_HUE_IRIS), LV_PART_INDICATOR);
    tm_ui.remain = avo_label(tm_ui.arc, &avo_font_digits_76, avo_pal()->label, "");
    lv_obj_set_style_text_font(tm_ui.remain, &avo_font_40, 0);
    lv_obj_center(tm_ui.remain);

    lv_color_t red = avo_hue(AVO_HUE_EMBER);
    tm_ui.cancel = circle_button(scr, 96, tint(red), red, LV_SYMBOL_CLOSE);
    lv_obj_align(tm_ui.cancel, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_event_cb(tm_ui.cancel, cancel_cb, LV_EVENT_CLICKED, NULL);
    tm_show();
}

static void timer_leave(void)
{
    lv_memzero(&tm_ui, sizeof tm_ui);
}

const avo_app_t AVO_APP_TIMER = {
    .name = "Temporizador", .symbol = AVO_SYM_HOURGLASS, .hue = AVO_HUE_IRIS,
    .build = timer_build, .leave = timer_leave,
};

/* ================================================================= Linterna */

static void flashlight_build(lv_obj_t *scr)
{
    lv_obj_set_style_bg_color(scr, avo_theme_is_avocado() ? lv_color_hex(0xFBFDEB) : lv_color_white(), 0);
    lv_obj_t *hint = avo_label(scr, &avo_font_22, lv_color_hex(0x8A8A8A), "Desliza a la derecha para salir");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -26);
    avo_hal_display_brightness(100);
}

static void flashlight_leave(void)
{
    avo_hal_display_brightness(avo_settings()->brightness);
}

const avo_app_t AVO_APP_FLASHLIGHT = {
    .name = "Linterna", .symbol = AVO_SYM_BULB, .hue = AVO_HUE_SOLAR,
    .build = flashlight_build, .leave = flashlight_leave,
};

/* ================================================================= Nivel */

static struct {
    lv_timer_t *timer;
    lv_obj_t *bubble, *angles;
    float fx, fy;
} lvl;

static void level_timer_cb(lv_timer_t *t)
{
    (void)t;
    avo_accel_t a;
    avo_hal_accel(&a);
    if (!a.valid) {
        lv_label_set_text(lvl.angles, "Sensor no disponible");
        return;
    }
    /* low-pass filter, bubble moves opposite to the tilt */
    lvl.fx = lvl.fx * 0.7f + (-a.ax) * 0.3f;
    lvl.fy = lvl.fy * 0.7f + (a.ay) * 0.3f;
    float x = lvl.fx * LEVEL_RADIUS, y = lvl.fy * LEVEL_RADIUS;
    float r = sqrtf(x * x + y * y);
    if (r > LEVEL_RADIUS - 30) {
        x *= (LEVEL_RADIUS - 30) / r;
        y *= (LEVEL_RADIUS - 30) / r;
    }
    lv_obj_align(lvl.bubble, LV_ALIGN_CENTER, (int32_t)x, (int32_t)y + 10);
    float ax = asinf(fmaxf(-1.f, fminf(1.f, a.ax))) * 57.2958f;
    float ay = asinf(fmaxf(-1.f, fminf(1.f, a.ay))) * 57.2958f;
    char buf[32];
    snprintf(buf, sizeof buf, "%+.0f°   %+.0f°", ax, ay);
    lv_label_set_text(lvl.angles, buf);
    bool level = fabsf(ax) < 1.5f && fabsf(ay) < 1.5f;
    lv_obj_set_style_bg_color(lvl.bubble, level ? avo_pal()->good : (avo_theme_is_avocado() ? avo_pal()->flesh : lv_color_white()), 0);
}

static lv_obj_t *ring(lv_obj_t *scr, int32_t d, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_align(o, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, c, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void level_build(lv_obj_t *scr)
{
    lv_memzero(&lvl, sizeof lvl);
    lv_obj_t *t = avo_label(scr, &avo_font_30, avo_theme_is_avocado() ? avo_pal()->accent : avo_hue(AVO_HUE_MINT), "Nivel");
    lv_obj_set_pos(t, AVO_PAD + 14, AVO_TOP + 6);
    avo_header_clock(scr);
    lv_color_t c = avo_pal()->label2;
    ring(scr, LEVEL_RADIUS * 2, c);
    ring(scr, 70, avo_pal()->good);
    lvl.bubble = lv_obj_create(scr);
    lv_obj_remove_style_all(lvl.bubble);
    lv_obj_set_size(lvl.bubble, 56, 56);
    lv_obj_set_style_radius(lvl.bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(lvl.bubble, LV_OPA_COVER, 0);
    lv_obj_remove_flag(lvl.bubble, LV_OBJ_FLAG_CLICKABLE);
    lvl.angles = avo_label(scr, &avo_font_26, avo_pal()->label, "");
    lv_obj_align(lvl.angles, LV_ALIGN_BOTTOM_MID, 0, -18);
    lvl.timer = lv_timer_create(level_timer_cb, LEVEL_REFRESH_MS, NULL);
    level_timer_cb(NULL);
}

static void level_leave(void)
{
    if (lvl.timer) {
        lv_timer_delete(lvl.timer);
    }
    lv_memzero(&lvl, sizeof lvl);
}

const avo_app_t AVO_APP_LEVEL = {
    .name = "Nivel", .symbol = AVO_SYM_COMPASS, .hue = AVO_HUE_MINT,
    .build = level_build, .leave = level_leave,
};

/* ================================================================= Música (AMS) */

static void music_build(lv_obj_t *scr)
{
    avo_header_clock(scr);
    lv_obj_t *t = avo_label(scr, &avo_font_30, avo_theme_is_avocado() ? avo_pal()->accent : avo_hue(AVO_HUE_ROSE), "Música");
    lv_obj_set_pos(t, AVO_PAD + 14, AVO_TOP + 6);
    avo_now_playing_create(scr, 84);
}

const avo_app_t AVO_APP_MUSIC = {
    .name = "Música", .symbol = LV_SYMBOL_AUDIO, .hue = AVO_HUE_ROSE,
    .build = music_build, .leave = NULL,
};

/* ================================================================= Esferas */

static void face_pick_cb(lv_event_t *e)
{
    avo_settings()->face = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    avo_settings_commit();
    avo_hal_click();
    avo_nav_face_select(avo_settings()->face);
    avo_nav_face();
}

static void faces_build(lv_obj_t *scr)
{
    avo_header_clock(scr);
    lv_obj_t *page = avo_page_create(scr, "Esferas", AVO_HUE_IRIS);
    static const avo_hue_t hues[AVO_FACE_MAX] = { AVO_HUE_ROSE, AVO_HUE_SKY, AVO_HUE_SOLAR, AVO_HUE_LIME };
    static const char *const syms[AVO_FACE_MAX] = { AVO_SYM_CLOCK, LV_SYMBOL_LIST, AVO_SYM_STOPWATCH, AVO_SYM_LEAF };
    for (int i = 0; i < avo_faces_count(); i++) {
        lv_obj_t *r = avo_row(page, hues[i], syms[i], avo_face_name(i),
                              i == avo_settings()->face ? LV_SYMBOL_OK : NULL);
        lv_obj_add_event_cb(r, face_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    avo_note(page, avo_theme_is_avocado() ? "Hass es exclusiva del Modo Avocado."
                                          : "Activa el Modo Avocado para desbloquear la esfera Hass.");
    avo_note(page, "También puedes deslizar a los lados sobre la esfera.");
}

const avo_app_t AVO_APP_FACES = {
    .name = "Esferas", .symbol = LV_SYMBOL_IMAGE, .hue = AVO_HUE_IRIS,
    .build = faces_build, .leave = NULL,
};

/* ================================================================= registry */

const avo_app_t *const AVO_APPS[] = {
    &AVO_APP_SETTINGS, &AVO_APP_MUSIC, &AVO_APP_STOPWATCH, &AVO_APP_TIMER,
    &AVO_APP_FLASHLIGHT, &AVO_APP_LEVEL, &AVO_APP_FACES,
};
const int AVO_APP_COUNT = (int)(sizeof AVO_APPS / sizeof AVO_APPS[0]);
