/*
 * Battery: the history behind "time left", the low-battery warnings, and
 * Ajustes > Batería (level, estimate, Ahorro de batería).
 */
#include <stdio.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define SAMPLE_EVERY_S 300      /* one sample every 5 minutes             */
#define WARN_FIRST 20           /* %                                     */
#define WARN_LAST 10

static avo_batt_hist_t s_hist;
static uint32_t s_seconds;
static int s_warned = 101;      /* lowest level already warned about      */

int avo_battery_minutes_left(void) { return avo_batt_minutes_left(&s_hist); }

static void warn(int level)
{
    char msg[64];
    if (avo_settings()->low_power) {
        snprintf(msg, sizeof msg, "Batería al %d %%", level);
    } else {
        snprintf(msg, sizeof msg, "Batería al %d %% · activa Ahorro en Ajustes", level);
    }
    avo_nav_wake();
    avo_toast(LV_SYMBOL_BATTERY_1, msg);
    if (avo_settings()->sounds) {
        avo_hal_sound_play(AVO_SOUND_NOTIFY);
    }
}

void avo_battery_tick(void)
{
    if (s_seconds++ % SAMPLE_EVERY_S != 0) {
        return;
    }
    avo_battery_t b;
    avo_hal_battery(&b);
    avo_batt_hist_add(&s_hist, s_seconds / 60, b.percent, b.charging || b.usb);
    if (b.usb || b.percent < 0) {
        s_warned = 101; /* plugged in: warn again on the next discharge */
        return;
    }
    int level = b.percent <= WARN_LAST ? WARN_LAST : b.percent <= WARN_FIRST ? WARN_FIRST : 101;
    if (level < s_warned) {
        s_warned = level;
        warn(b.percent);
    }
}

/* ================================================================= Ajustes > Batería */

static void low_power_cb(lv_event_t *e)
{
    avo_settings()->low_power = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_settings_commit();
    avo_hal_click();
    avo_toast(AVO_SYM_LEAF, avo_settings()->low_power ? "Ahorro de batería activado" : "Ahorro de batería desactivado");
}

static void time_left_text(char *buf, size_t len, const avo_battery_t *b)
{
    int left = avo_battery_minutes_left();
    if (b->usb) {
        snprintf(buf, len, "%s", b->charging ? "Cargando" : "Conectado");
    } else if (left < 0) {
        snprintf(buf, len, "Calculando…");
    } else if (left >= 48 * 60) {
        snprintf(buf, len, "Más de %d días", left / (24 * 60));
    } else {
        snprintf(buf, len, "Unas %d h %02d min", left / 60, left % 60);
    }
}

void avo_settings_battery_page(lv_obj_t *scr)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *page = avo_subpage_create(scr, "Batería", AVO_HUE_LIME);
    avo_battery_t b;
    avo_hal_battery(&b);

    lv_obj_t *card = avo_glass(page);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    char buf[48];
    lv_color_t level = b.percent >= 0 && b.percent <= 20 && !b.usb ? p->bad : p->good;
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 6, 0);
    if (b.percent >= 0) {
        snprintf(buf, sizeof buf, "%d", b.percent); /* the 76 px font has digits only */
        avo_label(row, &avo_font_digits_76, level, buf);
        lv_obj_set_style_pad_bottom(avo_label(row, &avo_font_40, level, "%"), 10, 0);
    } else {
        avo_label(row, &avo_font_40, level, "—");
    }
    time_left_text(buf, sizeof buf, &b);
    avo_label(card, &avo_font_26, p->label, buf);

    avo_row_switch(page, AVO_HUE_LIME, AVO_SYM_LEAF, "Ahorro de batería", avo_settings()->low_power, low_power_cb, NULL);
    avo_note(page, "Apaga la pantalla siempre activa, el Wi-Fi (tiempo y portadas) y el giro de muñeca, "
                   "limita el brillo y apaga la pantalla antes. El iPhone, las alarmas y los pasos siguen "
                   "funcionando. Tus ajustes vuelven al desactivarlo.");
    if (b.millivolts > 0) {
        snprintf(buf, sizeof buf, "%d.%02d V", b.millivolts / 1000, (b.millivolts % 1000) / 10);
        avo_info_row(page, "Voltaje", buf);
    }
}
