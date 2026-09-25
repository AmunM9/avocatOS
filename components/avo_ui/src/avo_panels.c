/* Control Center, Smart Stack and the Always-On screen. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"

#define CC_TOGGLE_D 92
#define CC_CELL_W 118
#define STACK_CARD_H 128
#define AOD_SHIFT_RANGE 9 /* px, anti burn-in */

/* ================================================================= helpers */

static lv_obj_t *stretch_column(lv_obj_t *parent, int32_t top)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, AVO_W, AVO_H - top);
    lv_obj_set_pos(col, 0, top);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(col, AVO_PAD, 0);
    lv_obj_set_style_pad_row(col, 12, 0);
    lv_obj_set_style_pad_bottom(col, 40, 0);
    lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    lv_obj_add_flag(col, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return col;
}

static void battery_text(char *buf, size_t len, const avo_battery_t *b)
{
    if (b->percent >= 0) {
        snprintf(buf, len, "%s  %d %%", b->charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_3, b->percent);
    } else if (b->usb) {
        snprintf(buf, len, LV_SYMBOL_USB "  Alimentado por USB");
    } else {
        snprintf(buf, len, LV_SYMBOL_BATTERY_EMPTY "  Sin batería");
    }
}

/* ================================================================= Control Center */

typedef enum { TG_BT, TG_WIFI, TG_AVOCADO, TG_AOD, TG_SOUNDS, TG_LIGHT, TG_COUNT } toggle_id_t;

static const struct {
    const char *symbol;
    const char *label;
} TOGGLES[TG_COUNT] = {
    [TG_BT] = { LV_SYMBOL_BLUETOOTH, "Bluetooth" },
    [TG_WIFI] = { LV_SYMBOL_WIFI, "Wi-Fi" },
    [TG_AVOCADO] = { AVO_SYM_LEAF, "Avocado" },
    [TG_AOD] = { AVO_SYM_SUN, "Siempre activa" },  /* dim face when the wrist is down */
    [TG_SOUNDS] = { LV_SYMBOL_BELL, "Sonidos" },
    [TG_LIGHT] = { AVO_SYM_BULB, "Linterna" },
};

static bool toggle_is_on(toggle_id_t id)
{
    const avo_settings_t *s = avo_settings();
    switch (id) {
    case TG_BT: return s->bluetooth;
    case TG_WIFI: return s->wifi;
    case TG_AVOCADO: return s->theme == AVO_THEME_AVOCADO;
    case TG_AOD: return s->aod;
    case TG_SOUNDS: return s->sounds;
    default: return false;
    }
}

static void style_toggle(lv_obj_t *btn, bool on)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *glyph = lv_obj_get_child(btn, 0);
    if (on) {
        lv_color_t fill = avo_theme_is_avocado() ? p->flesh : lv_color_white();
        lv_obj_set_style_bg_color(btn, fill, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(glyph, avo_theme_is_avocado() ? p->skin : lv_color_black(), 0);
    } else {
        lv_obj_set_style_bg_color(btn, p->glass, 0);
        lv_obj_set_style_bg_opa(btn, p->glass_opa + 10, 0);
        lv_obj_set_style_text_color(glyph, p->label, 0);
    }
}

static void toggle_cb(lv_event_t *e)
{
    toggle_id_t id = (toggle_id_t)(intptr_t)lv_event_get_user_data(e);
    avo_settings_t *s = avo_settings();
    avo_hal_click();
    switch (id) {
    case TG_BT: s->bluetooth = !s->bluetooth; break;
    case TG_WIFI: s->wifi = !s->wifi; break;
    case TG_AVOCADO: s->theme = (s->theme == AVO_THEME_AVOCADO) ? AVO_THEME_CLEAN : AVO_THEME_AVOCADO; break;
    case TG_AOD: s->aod = !s->aod; break;
    case TG_SOUNDS: s->sounds = !s->sounds; break;
    case TG_LIGHT: avo_nav_app(&AVO_APP_FLASHLIGHT); return;
    default: return;
    }
    style_toggle(lv_event_get_target(e), toggle_is_on(id));
    avo_settings_commit(); /* theme change rebuilds this screen */
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    avo_settings()->brightness = (uint8_t)lv_slider_get_value(sl);
    avo_hal_display_brightness(avo_settings()->brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        avo_settings_commit();
    }
}

static void np_hint_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_user_data(e);
    avo_hal_click();
    lv_tileview_set_tile_by_index(tv, 0, 1, LV_ANIM_ON);
}

/* Swipe up on the Control Center reveals Now Playing when music is active. */
static lv_obj_t *cc_with_now_playing(lv_obj_t *screen)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *tv = lv_tileview_create(screen);
    lv_obj_set_size(tv, AVO_W, AVO_H);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *cc = lv_tileview_add_tile(tv, 0, 0, LV_DIR_BOTTOM);
    lv_obj_t *np = lv_tileview_add_tile(tv, 0, 1, LV_DIR_TOP);
    lv_obj_remove_flag(cc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(np, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = avo_label(np, &avo_font_30, avo_theme_is_avocado() ? p->accent : avo_hue(AVO_HUE_ROSE), "Reproduciendo");
    lv_obj_set_pos(head, AVO_PAD + 14, AVO_TOP + 6);
    avo_now_playing_create(np, 84);

    avo_media_t m;
    avo_hal_media(&m);
    char buf[96];
    snprintf(buf, sizeof buf, LV_SYMBOL_UP "  %s", m.title);
    lv_obj_t *hint = avo_label(cc, &avo_font_22, p->label2, buf);
    lv_obj_set_width(hint, AVO_W - 2 * AVO_PAD - 40);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(hint, 12);
    lv_obj_add_event_cb(hint, np_hint_cb, LV_EVENT_CLICKED, tv);

    avo_nav_set_scroller(tv);
    return cc;
}

void avo_cc_build(lv_obj_t *screen)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *host = avo_media_recent() ? cc_with_now_playing(screen) : screen;
    lv_obj_t *col = stretch_column(host, 22);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE); /* swipe down always closes it */

    avo_battery_t b;
    avo_hal_battery(&b);
    char buf[40];
    battery_text(buf, sizeof buf, &b);
    lv_obj_t *pill = avo_glass(col);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, 52);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(pill, 24, 0);
    lv_obj_set_style_pad_ver(pill, 0, 0);
    lv_obj_t *pl = avo_label(pill, &avo_font_26, b.charging ? p->good : p->label, buf);
    lv_obj_center(pl);

    lv_obj_t *grid = lv_obj_create(col);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_GESTURE_BUBBLE);
    for (int i = 0; i < TG_COUNT; i++) {
        /* circle + caption, so every toggle says what it does */
        lv_obj_t *cell = lv_obj_create(grid);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, CC_CELL_W, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(cell, 4, 0);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *btn = lv_obj_create(cell);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, CC_TOGGLE_D, CC_TOGGLE_D);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_transform_scale(btn, 236, LV_STATE_PRESSED);
        lv_obj_set_style_transform_pivot_x(btn, CC_TOGGLE_D / 2, 0);
        lv_obj_set_style_transform_pivot_y(btn, CC_TOGGLE_D / 2, 0);
        lv_obj_t *g = avo_label(btn, &avo_font_40, p->label, TOGGLES[i].symbol);
        lv_obj_center(g);
        style_toggle(btn, toggle_is_on((toggle_id_t)i));
        lv_obj_add_event_cb(btn, toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *cap = avo_label(cell, &avo_font_22, p->label2, TOGGLES[i].label);
        lv_obj_set_width(cap, CC_CELL_W);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(cap, LV_LABEL_LONG_MODE_WRAP);
    }

    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 64);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_style_pad_hor(row, 12, 0);
    avo_label(row, &avo_font_22, p->label2, AVO_SYM_MOON);
    lv_obj_t *sl = avo_slider(row, 5, 100, avo_settings()->brightness);
    lv_obj_set_flex_grow(sl, 1);
    lv_obj_add_event_cb(sl, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, brightness_cb, LV_EVENT_RELEASED, NULL);
    avo_label(row, &avo_font_26, p->label, AVO_SYM_SUN);
}

/* ================================================================= Smart Stack */

static struct {
    lv_timer_t *timer;
    lv_obj_t *day_bar, *day_lbl, *sw_lbl, *batt_lbl, *music_lbl;
    lv_obj_t *wx_lbl, *act_lbl, *alarm_lbl;
    uint32_t media_version;
} st;

static void card_open_cb(lv_event_t *e)
{
    avo_nav_app(lv_event_get_user_data(e));
}

static lv_obj_t *stack_card(lv_obj_t *col, avo_hue_t hue, const char *symbol, const char *title,
                            lv_obj_t **value, const avo_app_t *opens)
{
    lv_obj_t *card = avo_glass(col);
    lv_obj_set_size(card, lv_pct(100), STACK_CARD_H);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 16, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *ic = avo_app_icon(card, hue, symbol, 64);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *txt = lv_obj_create(card);
    lv_obj_remove_style_all(txt);
    lv_obj_set_flex_grow(txt, 1);
    lv_obj_set_height(txt, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(txt, LV_OBJ_FLAG_CLICKABLE);
    avo_label(txt, &avo_font_22, avo_pal()->label2, title);
    *value = avo_label(txt, &avo_font_30, avo_pal()->label, "");
    lv_label_set_long_mode(*value, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(*value, lv_pct(100));
    if (opens) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, card_open_cb, LV_EVENT_CLICKED, (void *)opens);
    }
    return card;
}

static void stack_refresh(void)
{
    avo_time_t t;
    avo_hal_time_now(&t);
    int day_pct = (t.hour * 60 + t.min) * 100 / (24 * 60);
    char buf[96];
    lv_bar_set_value(st.day_bar, day_pct, LV_ANIM_OFF);
    avo_fmt_long_date(buf, sizeof buf, &t);
    lv_label_set_text(st.day_lbl, buf);

    if (avo_stopwatch_running()) {
        avo_fmt_stopwatch(buf, sizeof buf, avo_stopwatch_elapsed());
    } else {
        snprintf(buf, sizeof buf, "Listo");
    }
    lv_label_set_text(st.sw_lbl, buf);

    avo_battery_t b;
    avo_hal_battery(&b);
    battery_text(buf, sizeof buf, &b);
    lv_label_set_text(st.batt_lbl, buf);

    avo_weather_t w;
    if (avo_hal_weather(&w)) {
        if (w.days > 0) {
            snprintf(buf, sizeof buf, "%s %d°  Máx %d°", avo_wx_symbol(w.code, w.is_day), (int)lroundf(w.temp),
                     (int)lroundf(w.day_max[0]));
        } else {
            snprintf(buf, sizeof buf, "%s %d°", avo_wx_symbol(w.code, w.is_day), (int)lroundf(w.temp));
        }
    } else {
        snprintf(buf, sizeof buf, "%s", avo_settings()->weather ? "Sin datos" : "Desactivado");
    }
    lv_label_set_text(st.wx_lbl, buf);

    avo_activity_t a;
    avo_hal_activity(&a);
    char n[16];
    avo_fmt_thousands(n, sizeof n, a.steps);
    snprintf(buf, sizeof buf, "%s pasos · %u min", n, a.exercise_min);
    lv_label_set_text(st.act_lbl, buf);

    bool snoozed;
    char hm[12];
    if (avo_alarms_next_text(hm, sizeof hm, &snoozed)) {
        snprintf(buf, sizeof buf, "%s%s", snoozed ? "Pospuesta · " : "", hm);
    } else {
        snprintf(buf, sizeof buf, "Sin alarmas");
    }
    lv_label_set_text(st.alarm_lbl, buf);

    avo_media_t m;
    avo_hal_media(&m);
    if (m.version != st.media_version || !m.available) {
        st.media_version = m.version;
        if (!m.available) {
            lv_label_set_text(st.music_lbl, "Sin iPhone");
        } else if (m.title[0]) {
            snprintf(buf, sizeof buf, "%s %s", m.playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE, m.title);
            lv_label_set_text(st.music_lbl, buf);
        } else {
            lv_label_set_text(st.music_lbl, "Nada sonando");
        }
    }
}

static void stack_timer_cb(lv_timer_t *t)
{
    (void)t;
    stack_refresh();
}

static void stack_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (st.timer) {
        lv_timer_delete(st.timer);
    }
    lv_memzero(&st, sizeof st);
}

void avo_stack_build(lv_obj_t *screen)
{
    lv_memzero(&st, sizeof st);
    st.media_version = UINT32_MAX;
    avo_header_clock(screen);
    lv_obj_t *col = stretch_column(screen, 70);
    avo_nav_set_scroller(col);

    lv_obj_t *today = avo_glass(col);
    lv_obj_set_size(today, lv_pct(100), STACK_CARD_H);
    lv_obj_set_flex_flow(today, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(today, 12, 0);
    lv_obj_add_flag(today, LV_OBJ_FLAG_GESTURE_BUBBLE);
    avo_label(today, &avo_font_22, avo_pal()->label2, avo_theme_is_avocado() ? AVO_SYM_LEAF "  Hoy" : "Hoy");
    st.day_lbl = avo_label(today, &avo_font_26, avo_pal()->label, "");
    st.day_bar = lv_bar_create(today);
    lv_obj_set_size(st.day_bar, lv_pct(100), 10);
    lv_obj_add_style(st.day_bar, &avo_sty()->track, 0);
    lv_obj_add_style(st.day_bar, &avo_sty()->accent_fill, LV_PART_INDICATOR);

    stack_card(col, AVO_HUE_ROSE, LV_SYMBOL_AUDIO, "Música", &st.music_lbl, &AVO_APP_MUSIC);
    stack_card(col, AVO_HUE_SKY, AVO_SYM_CLOUD_SUN, "Tiempo", &st.wx_lbl, &AVO_APP_WEATHER);
    stack_card(col, AVO_HUE_EMBER, AVO_SYM_RUN, "Actividad", &st.act_lbl, &AVO_APP_ACTIVITY);
    stack_card(col, AVO_HUE_SOLAR, AVO_SYM_CLOCK, "Alarma", &st.alarm_lbl, &AVO_APP_ALARMS);
    stack_card(col, AVO_HUE_SOLAR, AVO_SYM_STOPWATCH, "Cronómetro", &st.sw_lbl, &AVO_APP_STOPWATCH);

    stack_card(col, AVO_HUE_LIME, LV_SYMBOL_BATTERY_FULL, "Batería", &st.batt_lbl, &AVO_APP_SETTINGS);

    stack_refresh();
    st.timer = lv_timer_create(stack_timer_cb, 1000, NULL);
    lv_obj_add_event_cb(screen, stack_deleted_cb, LV_EVENT_DELETE, NULL);
}

/* ================================================================= Always On */

static struct {
    lv_obj_t *box, *time, *date, *batt;
    int last_min;
} aod;

static void aod_deleted_cb(lv_event_t *e)
{
    (void)e;
    lv_memzero(&aod, sizeof aod);
}

static void aod_set(const avo_time_t *t)
{
    char buf[40];
    avo_fmt_hm(buf, sizeof buf, t, avo_settings()->h24);
    lv_label_set_text(aod.time, buf);
    avo_fmt_wday_day(buf, sizeof buf, t);
    lv_label_set_text(aod.date, buf);
    avo_battery_t b;
    avo_hal_battery(&b);
    if (b.percent >= 0) {
        snprintf(buf, sizeof buf, "%s %d %%", b.charging ? LV_SYMBOL_CHARGE : "", b.percent);
    } else {
        buf[0] = '\0';
    }
    lv_label_set_text(aod.batt, buf);
    if (b.charging) {
        lv_obj_set_style_text_color(aod.batt, lv_color_mix(avo_pal()->good, lv_color_black(), 170), 0);
    }
    /* move the whole block a few pixels every minute against burn-in */
    int32_t dx = (t->min % AOD_SHIFT_RANGE) - AOD_SHIFT_RANGE / 2;
    int32_t dy = ((t->min / AOD_SHIFT_RANGE) % AOD_SHIFT_RANGE) - AOD_SHIFT_RANGE / 2;
    lv_obj_align(aod.box, LV_ALIGN_CENTER, dx, dy - 20);
}

void avo_aod_build(lv_obj_t *screen)
{
    lv_memzero(&aod, sizeof aod);
    const bool avo = avo_theme_is_avocado();
    lv_color_t dim = avo ? lv_color_hex(0x8C9A6B) : lv_color_hex(0x80838F);
    aod.box = lv_obj_create(screen);
    lv_obj_remove_style_all(aod.box);
    lv_obj_set_size(aod.box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(aod.box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(aod.box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(aod.box, 8, 0);
    if (avo) {
        lv_obj_t *img = lv_image_create(aod.box);
        lv_image_set_src(img, &avo_img_half_36);
        lv_obj_set_style_image_opa(img, LV_OPA_60, 0);
    }
    aod.date = avo_label(aod.box, &avo_font_22, dim, "");
    aod.time = avo_label(aod.box, &avo_font_digits_76, dim, "");
    aod.batt = avo_label(aod.box, &avo_font_22, lv_color_mix(dim, lv_color_black(), 160), "");
    avo_time_t t;
    avo_hal_time_now(&t);
    aod_set(&t);
    aod.last_min = t.min;
    lv_obj_add_event_cb(screen, aod_deleted_cb, LV_EVENT_DELETE, NULL);
}

void avo_aod_tick(const avo_time_t *t)
{
    if (aod.time && t->min != aod.last_min) {
        aod.last_min = t->min;
        aod_set(t);
    }
}
