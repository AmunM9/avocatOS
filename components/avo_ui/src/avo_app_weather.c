/* Tiempo: current conditions and the next days, from Open-Meteo over Wi-Fi. */
#include <math.h>
#include <stdio.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define REFRESH_MS 5000

static const char *const WDAY[7] = { "Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb" };

static struct {
    lv_timer_t *timer;
    uint32_t shown_ms;     /* updated_ms of the forecast on screen */
} ui;

static void weather_build(lv_obj_t *scr);

static int deg(float c) { return (int)lroundf(c); }

static const char *empty_reason(void)
{
    if (!avo_settings()->weather) return "El tiempo está desactivado.";
    if (!avo_settings()->wifi || avo_hal_wifi_state() != AVO_LINK_CONNECTED) return "Conecta el Wi-Fi para ver el tiempo.";
    return "Buscando el pronóstico…";
}

static void weather_sw_cb(lv_event_t *e)
{
    avo_settings()->weather = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_settings_commit();
    avo_hal_click();
}

static void now_card(lv_obj_t *page, const avo_weather_t *w)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *card = avo_glass(page);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *col = lv_obj_create(card);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    char buf[48];
    snprintf(buf, sizeof buf, "%s  %s", AVO_SYM_PIN, w->city[0] ? w->city : "Aquí");
    avo_label(col, &avo_font_22, p->label2, buf);
    snprintf(buf, sizeof buf, "%d°", deg(w->temp));
    avo_label(col, &avo_font_digits_76, p->label, buf);
    avo_label(col, &avo_font_22, p->label, avo_wmo_text(w->code));
    if (w->days > 0) {
        snprintf(buf, sizeof buf, "Máx %d°  Mín %d°", deg(w->day_max[0]), deg(w->day_min[0]));
        avo_label(col, &avo_font_22, p->label2, buf);
    }
    lv_obj_t *ic = avo_label(card, &avo_font_40, avo_hue(AVO_HUE_SKY), avo_wx_symbol(w->code, w->is_day));
    lv_obj_set_style_transform_scale(ic, 400, 0); /* a big glyph without a bigger font */
    lv_obj_set_style_transform_pivot_x(ic, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(ic, lv_pct(50), 0);
    lv_obj_set_style_pad_right(card, 56, 0);
}

static void days_list(lv_obj_t *page, const avo_weather_t *w)
{
    avo_time_t now;
    avo_hal_time_now(&now);
    avo_section(page, "Próximos días");
    for (int i = 1; i < w->days; i++) {
        char v[24];
        snprintf(v, sizeof v, "%d° / %d°", deg(w->day_max[i]), deg(w->day_min[i]));
        lv_obj_t *r = avo_row(page, AVO_HUE_SKY, avo_wx_symbol(w->day_code[i], true), WDAY[(now.wday + i) % 7], v);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    avo_weather_t w;
    avo_hal_weather(&w);
    if (w.updated_ms != ui.shown_ms) {
        avo_nav_reload(); /* a new forecast arrived: rebuild */
    }
}

static void weather_build(lv_obj_t *scr)
{
    avo_header_clock(scr);
    lv_obj_t *page = avo_page_create(scr, "Tiempo", AVO_HUE_SKY);
    avo_weather_t w;
    if (avo_hal_weather(&w)) {
        now_card(page, &w);
        days_list(page, &w);
        uint32_t min = (avo_hal_millis() - w.updated_ms) / 60000u;
        char buf[48];
        snprintf(buf, sizeof buf, min < 1 ? "Actualizado ahora" : "Actualizado hace %u min", (unsigned)min);
        avo_note(page, buf);
    } else {
        avo_note(page, empty_reason());
    }
    ui.shown_ms = w.updated_ms;
    avo_section(page, "Ajustes");
    avo_row_switch(page, AVO_HUE_SKY, AVO_SYM_CLOUD_SUN, "Tiempo por Wi-Fi", avo_settings()->weather, weather_sw_cb, NULL);
    avo_note(page, "La ubicación aproximada sale de tu IP (ipwho.is) y el pronóstico de Open-Meteo.");
    ui.timer = lv_timer_create(timer_cb, REFRESH_MS, NULL);
}

static void weather_leave(void)
{
    if (ui.timer) {
        lv_timer_delete(ui.timer);
    }
    lv_memzero(&ui, sizeof ui);
}

const avo_app_t AVO_APP_WEATHER = {
    .name = "Tiempo", .symbol = AVO_SYM_CLOUD_SUN, .hue = AVO_HUE_SKY,
    .build = weather_build, .leave = weather_leave,
};
