/* Alarmas: list with on/off switches, and an editor (hour, minute, days). */
#include <stdio.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define ROLLER_W 130
#define ROLLER_ROWS 3
#define DAY_CHIP 46

static int s_edit;               /* index being edited, -1 = new alarm */
static avo_alarm_t s_draft;
static lv_obj_t *s_hour, *s_min;
static lv_obj_t *s_chips[7];

static void alarms_build(lv_obj_t *scr);

/* ---------------------------------------------------------------- editor */

static lv_obj_t *roller(lv_obj_t *parent, int count, int value)
{
    static char hours[24 * 3], mins[60 * 3];
    char *opts = count == 24 ? hours : mins;
    if (!opts[0]) {
        char *w = opts;
        for (int i = 0; i < count; i++) {
            w += sprintf(w, i ? "\n%02d" : "%02d", i);
        }
    }
    const avo_palette_t *p = avo_pal();
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(r, ROLLER_ROWS);
    lv_roller_set_selected(r, (uint32_t)value, LV_ANIM_OFF);
    lv_obj_set_width(r, ROLLER_W);
    lv_obj_set_style_text_font(r, &avo_font_40, 0);
    lv_obj_set_style_text_color(r, p->label2, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(r, p->surface, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_radius(r, 16, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, avo_theme_is_avocado() ? p->accent : avo_hue(AVO_HUE_SOLAR), LV_PART_SELECTED);
    return r;
}

static void chip_style(int d)
{
    bool on = s_draft.days & (1u << d);
    lv_color_t c = avo_theme_is_avocado() ? avo_pal()->accent : avo_hue(AVO_HUE_SOLAR);
    lv_obj_set_style_bg_color(s_chips[d], on ? c : avo_pal()->surface, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(s_chips[d], 0), on ? lv_color_black() : avo_pal()->label, 0);
}

static void chip_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_draft.days ^= (uint8_t)(1u << d);
    chip_style(d);
    avo_hal_click();
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    avo_alarms_t *s = avo_alarms();
    s_draft.hour = (uint8_t)lv_roller_get_selected(s_hour);
    s_draft.min = (uint8_t)lv_roller_get_selected(s_min);
    s_draft.enabled = true;
    if (s_edit >= 0 && s_edit < s->count) {
        s->list[s_edit] = s_draft;
    } else if (s->count < AVO_ALARM_MAX) {
        s->list[s->count++] = s_draft;
    }
    avo_alarms_commit();
    avo_hal_click();
    avo_nav_back();
}

static void delete_cb(lv_event_t *e)
{
    (void)e;
    avo_alarms_t *s = avo_alarms();
    if (s_edit >= 0 && s_edit < s->count) {
        for (int i = s_edit; i < s->count - 1; i++) {
            s->list[i] = s->list[i + 1];
        }
        s->count--;
        avo_alarms_commit();
    }
    avo_hal_click();
    avo_nav_back();
}

static void editor_build(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, s_edit < 0 ? "Nueva alarma" : "Alarma", AVO_HUE_SOLAR);
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    s_hour = roller(row, 24, s_draft.hour);
    avo_label(row, &avo_font_40, avo_pal()->label, ":");
    s_min = roller(row, 60, s_draft.min);

    avo_section(page, "Repetir");
    lv_obj_t *days = lv_obj_create(page);
    lv_obj_remove_style_all(days);
    lv_obj_set_size(days, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(days, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(days, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    static const char *const LETTERS = "DLMXJVS";
    for (int k = 1; k <= 7; k++) {
        int d = k % 7; /* Monday first */
        lv_obj_t *c = lv_obj_create(days);
        lv_obj_remove_style_all(c);
        lv_obj_set_size(c, DAY_CHIP, DAY_CHIP);
        lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        char txt[2] = { LETTERS[d], 0 };
        lv_obj_center(avo_label(c, &avo_font_22, avo_pal()->label, txt));
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(c, 4);
        lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, (void *)(intptr_t)d);
        s_chips[d] = c;
        chip_style(d);
    }
    avo_note(page, "Sin días marcados suena una sola vez.");

    lv_obj_t *save = avo_row(page, AVO_HUE_MINT, LV_SYMBOL_OK, "Guardar", NULL);
    lv_obj_add_event_cb(save, save_cb, LV_EVENT_CLICKED, NULL);
    if (s_edit >= 0) {
        lv_obj_t *del = avo_row(page, AVO_HUE_EMBER, LV_SYMBOL_TRASH, "Eliminar", NULL);
        lv_obj_add_event_cb(del, delete_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void editor_leave(void)
{
    s_hour = s_min = NULL;
    lv_memzero(s_chips, sizeof s_chips);
}

static void open_editor(int index)
{
    s_edit = index;
    if (index >= 0) {
        s_draft = avo_alarms()->list[index];
    } else {
        avo_time_t now;
        avo_hal_time_now(&now);
        s_draft = (avo_alarm_t){ .hour = (uint8_t)now.hour, .min = 0, .days = 0, .enabled = true };
    }
    avo_hal_click();
    avo_nav_push(editor_build, editor_leave);
}

/* ---------------------------------------------------------------- list */

static void row_cb(lv_event_t *e) { open_editor((int)(intptr_t)lv_event_get_user_data(e)); }
static void add_cb(lv_event_t *e) { (void)e; open_editor(-1); }

static void toggle_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    avo_alarms()->list[i].enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_alarms_commit();
    avo_hal_click();
}

static void alarm_row(lv_obj_t *page, int i)
{
    const avo_alarm_t *a = &avo_alarms()->list[i];
    avo_time_t t = { .hour = a->hour, .min = a->min };
    char hm[12], days[32];
    avo_fmt_hm(hm, sizeof hm, &t, avo_settings()->h24);
    avo_fmt_alarm_days(days, sizeof days, a->days);
    /* row: [ 07:30 / Entre semana ][ switch ]; tapping the row edits it */
    lv_obj_t *r = avo_row(page, AVO_HUE_SOLAR, NULL, "", NULL);
    lv_obj_delete(lv_obj_get_child(r, 0));
    lv_obj_t *col = lv_obj_create(r);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    avo_label(col, &avo_font_40, a->enabled ? avo_pal()->label : avo_pal()->label2, hm);
    avo_label(col, &avo_font_22, avo_pal()->label2, days);
    lv_obj_t *sw = avo_switch(r, a->enabled);
    lv_obj_add_event_cb(sw, toggle_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(r, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
}

static void alarms_build(lv_obj_t *scr)
{
    avo_header_clock(scr);
    lv_obj_t *page = avo_page_create(scr, "Alarmas", AVO_HUE_SOLAR);
    const avo_alarms_t *s = avo_alarms();
    for (int i = 0; i < s->count; i++) {
        alarm_row(page, i);
    }
    if (s->count == 0) {
        avo_note(page, "No hay alarmas. Suenan aunque Sonidos esté desactivado.");
    }
    if (s->count < AVO_ALARM_MAX) {
        lv_obj_t *add = avo_row(page, AVO_HUE_SOLAR, LV_SYMBOL_PLUS, "Añadir alarma", NULL);
        lv_obj_add_event_cb(add, add_cb, LV_EVENT_CLICKED, NULL);
    }
    avo_note(page, "Posponer: doble toque en el reloj o giro de muñeca.");
}

const avo_app_t AVO_APP_ALARMS = {
    .name = "Alarmas", .symbol = AVO_SYM_CLOCK, .hue = AVO_HUE_SOLAR,
    .build = alarms_build, .leave = NULL,
};
