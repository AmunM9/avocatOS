/*
 * Watch faces. Each face builds its widgets once and then only touches the
 * objects whose content changed (minute digits, second hand...), so the
 * display only re-sends small dirty areas at 1 Hz.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"

LV_IMAGE_DECLARE(avo_img_flux_0);
LV_IMAGE_DECLARE(avo_img_flux_1);
LV_IMAGE_DECLARE(avo_img_flux_2);
LV_IMAGE_DECLARE(avo_img_flux_3);
LV_IMAGE_DECLARE(avo_img_flux_4);
LV_IMAGE_DECLARE(avo_img_flux_5);
LV_IMAGE_DECLARE(avo_img_flux_6);
LV_IMAGE_DECLARE(avo_img_flux_7);
LV_IMAGE_DECLARE(avo_img_flux_8);
LV_IMAGE_DECLARE(avo_img_flux_9);

static const lv_image_dsc_t *const FLUX_DIGITS[10] = {
    &avo_img_flux_0, &avo_img_flux_1, &avo_img_flux_2, &avo_img_flux_3, &avo_img_flux_4,
    &avo_img_flux_5, &avo_img_flux_6, &avo_img_flux_7, &avo_img_flux_8, &avo_img_flux_9,
};

#define FACE_FLUX 0
#define FACE_MODULAR 1
#define FACE_CHRONO 2
#define FACE_HASS 3

#define BATTERY_REFRESH_S 30
#define FLUX_GAP 6
#define FLUX_ROW_Y 44
#define FLUX_ROW_GAP 14
#define CHRONO_SIZE 400
#define ARC_SIZE 112

static const char *const FACE_NAMES[AVO_FACE_MAX] = { "Flux", "Modular", "Cronógrafo", "Hass" };

/* live widgets (NULL when the face is not built) */
static struct {
    lv_obj_t *digit[4];
    lv_obj_t *date;
    int last_hm;
} s_flux;

static struct {
    lv_obj_t *time, *date, *long_date, *net;
    lv_obj_t *batt_arc, *batt_lbl, *act_arc, *act_lbl, *wx_lbl;
    int last_min;
} s_mod;

static struct {
    lv_obj_t *scale, *hour, *min, *sec, *date;
} s_chrono;

static struct {
    lv_obj_t *time, *date, *wday, *batt;
    int last_min;
} s_hass;

static int s_batt_countdown;

int avo_faces_count(void)
{
    return avo_theme_is_avocado() ? 4 : 3;
}

const char *avo_face_name(int index)
{
    return (index >= 0 && index < AVO_FACE_MAX) ? FACE_NAMES[index] : "";
}

void avo_faces_forget(void)
{
    lv_memzero(&s_flux, sizeof s_flux);
    lv_memzero(&s_mod, sizeof s_mod);
    lv_memzero(&s_chrono, sizeof s_chrono);
    lv_memzero(&s_hass, sizeof s_hass);
    s_flux.last_hm = -1;
    s_mod.last_min = -1;
    s_hass.last_min = -1;
}

static lv_obj_t *face_root(lv_obj_t *parent)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, AVO_W, AVO_H);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLL_CHAIN);
    return root;
}

static void read_battery(avo_battery_t *b, bool force)
{
    static avo_battery_t cached;
    if (force || s_batt_countdown <= 0) {
        avo_hal_battery(&cached);
        s_batt_countdown = BATTERY_REFRESH_S;
    }
    *b = cached;
}

static lv_color_t battery_color(const avo_battery_t *b)
{
    if (b->charging) return avo_pal()->good;
    if (b->percent >= 0 && b->percent <= 15) return avo_pal()->bad;
    if (b->percent >= 0 && b->percent <= 30) return avo_pal()->warn;
    return avo_pal()->good;
}

/* ================================================================= Flux */

static void flux_colors(int row, lv_color_t *top, lv_color_t *bottom)
{
    if (avo_theme_is_avocado()) {
        *top = row == 0 ? lv_color_hex(0xE4EEAB) : lv_color_hex(0xA6D65A);
        *bottom = row == 0 ? lv_color_hex(0x9ACD4B) : lv_color_hex(0x5E9A2E);
    } else {
        *top = row == 0 ? lv_color_hex(0xFFB547) : lv_color_hex(0x8B7CFF);
        *bottom = row == 0 ? lv_color_hex(0xFF6FAE) : lv_color_hex(0x4DB8FF);
    }
}

static void flux_set_digits(const avo_time_t *t)
{
    char hh[4], mm[4];
    avo_fmt_hour(hh, sizeof hh, t, avo_settings()->h24);
    avo_fmt_min(mm, sizeof mm, t);
    const char *rows[2] = { hh, mm };
    for (int row = 0; row < 2; row++) {
        const lv_image_dsc_t *a = FLUX_DIGITS[rows[row][0] - '0'];
        const lv_image_dsc_t *b = FLUX_DIGITS[rows[row][1] - '0'];
        int32_t total = a->header.w + FLUX_GAP + b->header.w;
        int32_t x = (AVO_W - total) / 2;
        int32_t y = FLUX_ROW_Y + row * (a->header.h + FLUX_ROW_GAP);
        lv_obj_t *oa = s_flux.digit[row * 2], *ob = s_flux.digit[row * 2 + 1];
        lv_obj_set_size(oa, a->header.w, a->header.h);
        lv_obj_set_style_bitmap_mask_src(oa, a, 0);
        lv_obj_set_pos(oa, x, y);
        lv_obj_set_size(ob, b->header.w, b->header.h);
        lv_obj_set_style_bitmap_mask_src(ob, b, 0);
        lv_obj_set_pos(ob, x + a->header.w + FLUX_GAP, y);
    }
    char d[16];
    avo_fmt_wday_day(d, sizeof d, t);
    lv_label_set_text(s_flux.date, d);
}

static lv_obj_t *flux_create(lv_obj_t *parent, const avo_time_t *t)
{
    lv_obj_t *root = face_root(parent);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *o = lv_obj_create(root);
        lv_obj_remove_style_all(o);
        lv_color_t top, bottom;
        flux_colors(i / 2, &top, &bottom);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(o, top, 0);
        lv_obj_set_style_bg_grad_color(o, bottom, 0);
        lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        s_flux.digit[i] = o;
    }
    s_flux.date = avo_label(root, &avo_font_22, avo_theme_is_avocado() ? avo_pal()->accent : lv_color_hex(0xFFB547), "");
    lv_obj_set_pos(s_flux.date, AVO_PAD + 18, 14);
    flux_set_digits(t);
    s_flux.last_hm = t->hour * 60 + t->min;
    return root;
}

static void flux_tick(const avo_time_t *t)
{
    int hm = t->hour * 60 + t->min;
    if (s_flux.digit[0] && hm != s_flux.last_hm) {
        s_flux.last_hm = hm;
        flux_set_digits(t);
    }
}

/* ================================================================= Modular */

static lv_obj_t *complication_arc(lv_obj_t *root, int32_t x, lv_color_t color, lv_obj_t **value, const char *caption)
{
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE);
    lv_obj_set_pos(arc, x, 190);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_mix(color, lv_color_black(), 70), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    *value = avo_label(arc, &avo_font_30, avo_pal()->label, "");
    lv_obj_center(*value);
    lv_obj_t *cap = avo_label(root, &avo_font_22, avo_pal()->label2, caption);
    lv_obj_align_to(cap, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, -8);
    return arc;
}

static void modular_refresh_status(bool force)
{
    avo_battery_t b;
    read_battery(&b, force);
    char buf[48];
    if (b.percent >= 0) {
        lv_arc_set_value(s_mod.batt_arc, b.percent);
        snprintf(buf, sizeof buf, "%d", b.percent);
    } else {
        lv_arc_set_value(s_mod.batt_arc, 0);
        snprintf(buf, sizeof buf, b.usb ? LV_SYMBOL_USB : "--");
    }
    lv_label_set_text(s_mod.batt_lbl, buf);
    lv_obj_set_style_arc_color(s_mod.batt_arc, battery_color(&b), LV_PART_INDICATOR);

    avo_activity_t a;
    avo_hal_activity(&a);
    uint32_t goal = avo_settings()->step_goal;
    lv_arc_set_value(s_mod.act_arc, goal ? (int32_t)LV_MIN(100u, a.steps * 100u / goal) : 0);
    if (a.steps >= 10000) {
        snprintf(buf, sizeof buf, "%uk", (unsigned)(a.steps / 1000));
    } else {
        avo_fmt_thousands(buf, sizeof buf, a.steps);
    }
    lv_label_set_text(s_mod.act_lbl, buf);

    avo_weather_t w;
    if (avo_hal_weather(&w)) {
        snprintf(buf, sizeof buf, "%d°", (int)lroundf(w.temp));
    } else {
        snprintf(buf, sizeof buf, "%s", AVO_SYM_CLOUD);
    }
    lv_label_set_text(s_mod.wx_lbl, buf);

    /* second line: next alarm, else the network, else the name */
    char hm[12], ssid[AVO_WIFI_SSID_MAX], ip[16];
    bool snoozed;
    avo_hal_wifi_info(ssid, sizeof ssid, ip, sizeof ip);
    if (avo_alarms_next_text(hm, sizeof hm, &snoozed)) {
        snprintf(buf, sizeof buf, AVO_SYM_CLOCK "  %s%s", snoozed ? "Pospuesta · " : "Alarma ", hm);
    } else if (ssid[0]) {
        snprintf(buf, sizeof buf, LV_SYMBOL_WIFI "  %s", ssid);
    } else {
        snprintf(buf, sizeof buf, "%s", avo_theme_is_avocado() ? AVO_SYM_LEAF "  avocatOS" : "avocatOS");
    }
    lv_label_set_text(s_mod.net, buf);
}

static void modular_set_time(const avo_time_t *t)
{
    char buf[40];
    avo_fmt_hm(buf, sizeof buf, t, avo_settings()->h24);
    lv_label_set_text(s_mod.time, buf);
    avo_fmt_wday_day(buf, sizeof buf, t);
    lv_label_set_text(s_mod.date, buf);
    avo_fmt_long_date(buf, sizeof buf, t);
    lv_label_set_text(s_mod.long_date, buf);
}

static lv_obj_t *modular_create(lv_obj_t *parent, const avo_time_t *t)
{
    lv_obj_t *root = face_root(parent);
    const avo_palette_t *p = avo_pal();
    s_mod.date = avo_label(root, &avo_font_26, p->accent, "");
    lv_obj_set_pos(s_mod.date, AVO_PAD + 18, 22);
    if (avo_theme_is_avocado()) {
        lv_obj_t *img = lv_image_create(root);
        lv_image_set_src(img, &avo_img_half_36);
        lv_obj_align(img, LV_ALIGN_TOP_RIGHT, -(AVO_PAD + 22), 18);
    }
    s_mod.time = avo_label(root, &avo_font_digits_76, p->label, "");
    lv_obj_set_pos(s_mod.time, AVO_PAD + 12, 62);

    int32_t gap = (AVO_W - 2 * AVO_PAD - 3 * ARC_SIZE) / 2;
    s_mod.batt_arc = complication_arc(root, AVO_PAD, p->good, &s_mod.batt_lbl, "Batería");
    s_mod.act_arc = complication_arc(root, AVO_PAD + ARC_SIZE + gap, avo_hue(AVO_HUE_EMBER), &s_mod.act_lbl, "Pasos");
    lv_obj_set_style_text_font(s_mod.act_lbl, &avo_font_26, 0);
    lv_obj_t *wx_arc = complication_arc(root, AVO_PAD + 2 * (ARC_SIZE + gap), avo_hue(AVO_HUE_SKY), &s_mod.wx_lbl, "Tiempo");
    lv_arc_set_value(wx_arc, 100);
    lv_obj_set_style_arc_opa(wx_arc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    lv_obj_t *card = avo_glass(root);
    lv_obj_set_size(card, AVO_W - 2 * AVO_PAD, 118);
    lv_obj_set_pos(card, AVO_PAD, 352);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_style_pad_left(card, 22, 0);
    s_mod.long_date = avo_label(card, &avo_font_26, p->label, "");
    s_mod.net = avo_label(card, &avo_font_22, p->label2, "");

    modular_set_time(t);
    modular_refresh_status(true);
    s_mod.last_min = t->min;
    return root;
}

static void modular_tick(const avo_time_t *t)
{
    if (!s_mod.time) {
        return;
    }
    if (t->min != s_mod.last_min) {
        s_mod.last_min = t->min;
        modular_set_time(t);
    }
    if (t->sec % 5 == 0) {
        modular_refresh_status(false);
    }
}

/* ================================================================= Chrono */

static const char *CHRONO_LABELS[] = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL };

static lv_obj_t *hand(lv_obj_t *scale, int32_t width, lv_color_t color)
{
    lv_obj_t *l = lv_line_create(scale);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_style_line_color(l, color, 0);
    return l;
}

static void chrono_set(const avo_time_t *t)
{
    int32_t r = CHRONO_SIZE / 2;
    lv_scale_set_line_needle_value(s_chrono.scale, s_chrono.hour, r * 52 / 100, (t->hour % 12) * 5 + t->min / 12);
    lv_scale_set_line_needle_value(s_chrono.scale, s_chrono.min, r * 80 / 100, t->min);
    lv_scale_set_line_needle_value(s_chrono.scale, s_chrono.sec, r * 88 / 100, t->sec);
}

static lv_obj_t *chrono_create(lv_obj_t *parent, const avo_time_t *t)
{
    const avo_palette_t *p = avo_pal();
    bool avo = avo_theme_is_avocado();
    lv_obj_t *root = face_root(parent);
    lv_obj_t *sc = lv_scale_create(root);
    s_chrono.scale = sc;
    lv_obj_set_size(sc, CHRONO_SIZE, CHRONO_SIZE);
    lv_obj_center(sc);
    lv_obj_remove_flag(sc, LV_OBJ_FLAG_CLICKABLE);
    lv_scale_set_mode(sc, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(sc, 0, 59);
    lv_scale_set_angle_range(sc, 354);
    lv_scale_set_rotation(sc, 270);
    lv_scale_set_total_tick_count(sc, 60);
    lv_scale_set_major_tick_every(sc, 5);
    lv_scale_set_label_show(sc, true);
    lv_scale_set_text_src(sc, CHRONO_LABELS);
    lv_obj_set_style_length(sc, 20, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(sc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(sc, avo ? p->flesh : p->label, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(sc, &avo_font_30, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(sc, p->label, LV_PART_INDICATOR);
    lv_obj_set_style_length(sc, 9, LV_PART_ITEMS);
    lv_obj_set_style_line_width(sc, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(sc, p->label2, LV_PART_ITEMS);
    lv_obj_set_style_arc_width(sc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sc, 6, 0);

    s_chrono.date = avo_label(sc, &avo_font_22, avo ? p->accent : avo_hue(AVO_HUE_SOLAR), "");
    lv_obj_align(s_chrono.date, LV_ALIGN_CENTER, 0, 84);

    s_chrono.hour = hand(sc, 10, p->label);
    s_chrono.min = hand(sc, 7, p->label);
    s_chrono.sec = hand(sc, 3, avo ? p->rind : avo_hue(AVO_HUE_EMBER));

    lv_obj_t *dot = lv_obj_create(sc);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 20, 20);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, avo ? p->pit : avo_hue(AVO_HUE_EMBER), 0);
    lv_obj_set_style_border_width(dot, 4, 0);
    lv_obj_set_style_border_color(dot, avo ? p->flesh : lv_color_black(), 0);

    char d[16];
    avo_fmt_wday_day(d, sizeof d, t);
    lv_label_set_text(s_chrono.date, d);
    chrono_set(t);
    return root;
}

static void chrono_tick(const avo_time_t *t)
{
    if (!s_chrono.scale) {
        return;
    }
    chrono_set(t);
    if (t->sec == 0) {
        char d[16];
        avo_fmt_wday_day(d, sizeof d, t);
        lv_label_set_text(s_chrono.date, d);
    }
}

/* ================================================================= Hass (avocado) */

static void hass_set(const avo_time_t *t)
{
    char buf[16];
    avo_fmt_hm(buf, sizeof buf, t, avo_settings()->h24);
    lv_label_set_text(s_hass.time, buf);
    snprintf(buf, sizeof buf, "%d", t->day);
    lv_label_set_text(s_hass.date, buf);
    avo_fmt_wday_day(buf, sizeof buf, t);
    char *space = strchr(buf, ' ');
    if (space) {
        *space = '\0'; /* keep only the weekday */
    }
    lv_label_set_text(s_hass.wday, buf);

    avo_battery_t b;
    read_battery(&b, false);
    char bb[24];
    if (b.percent >= 0) {
        snprintf(bb, sizeof bb, AVO_SYM_LEAF "  %d %%", b.percent);
    } else {
        snprintf(bb, sizeof bb, AVO_SYM_LEAF "  avocatOS");
    }
    lv_label_set_text(s_hass.batt, bb);
}

static lv_obj_t *hass_create(lv_obj_t *parent, const avo_time_t *t)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *root = face_root(parent);
    s_hass.time = avo_label(root, &avo_font_digits_76, p->flesh, "");
    lv_obj_align(s_hass.time, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *img = lv_image_create(root);
    lv_image_set_src(img, &avo_img_half_300);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 118);

    /* the pit sits at ~65% of the image height */
    lv_obj_t *pit = lv_obj_create(img);
    lv_obj_remove_style_all(pit);
    lv_obj_set_size(pit, 96, 96);
    lv_obj_align(pit, LV_ALIGN_TOP_MID, 0, 146);
    lv_obj_set_flex_flow(pit, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pit, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(pit, 0, 0);
    s_hass.wday = avo_label(pit, &avo_font_22, p->flesh, "");
    s_hass.date = avo_label(pit, &avo_font_40, p->flesh, "");

    s_hass.batt = avo_label(root, &avo_font_22, p->label2, "");
    lv_obj_align(s_hass.batt, LV_ALIGN_BOTTOM_MID, 0, -18);
    hass_set(t);
    s_hass.last_min = t->min;
    return root;
}

static void hass_tick(const avo_time_t *t)
{
    if (s_hass.time && t->min != s_hass.last_min) {
        s_hass.last_min = t->min;
        hass_set(t);
    }
}

/* ================================================================= registry */

lv_obj_t *avo_face_create(int index, lv_obj_t *parent)
{
    avo_time_t t;
    avo_hal_time_now(&t);
    switch (index) {
    case FACE_MODULAR: return modular_create(parent, &t);
    case FACE_CHRONO: return chrono_create(parent, &t);
    case FACE_HASS: return hass_create(parent, &t);
    case FACE_FLUX:
    default: return flux_create(parent, &t);
    }
}

void avo_faces_tick(const avo_time_t *t)
{
    s_batt_countdown--;
    flux_tick(t);
    modular_tick(t);
    chrono_tick(t);
    hass_tick(t);
}
