/*
 * Phase 4 faces:
 *  - Retrato: the user's photo (sent from the iPhone through Ajustes >
 *    Enviar al reloj) with the time over a soft shade.
 *  - Órbita: the day as a circle. The upper half is the daylight path
 *    (sunrise on the left, noon at the top, sunset on the right), the lower
 *    half the night; the sun sits where it is now, the moon shows its phase.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"

#define DEFAULT_LAT 4.61            /* until the weather lookup knows better */
#define DEFAULT_LON (-74.08)

/* ================================================================= Retrato */

#define SHADE_H 190

static struct {
    lv_obj_t *root, *img, *hint, *time, *date;
    lv_image_dsc_t dsc;
    uint32_t photo_version;
    int last_min;
} s_ret;

static void retrato_set_time(const avo_time_t *t)
{
    char buf[32];
    avo_fmt_hm(buf, sizeof buf, t, avo_settings()->h24);
    lv_label_set_text(s_ret.time, buf);
    avo_fmt_wday_day(buf, sizeof buf, t);
    lv_label_set_text(s_ret.date, buf);
}

static void retrato_set_photo(void)
{
    avo_photo_t ph;
    bool has = avo_hal_photo(&ph);
    s_ret.photo_version = ph.version;
    if (has) {
        s_ret.dsc = (lv_image_dsc_t){
            .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                        .w = ph.w, .h = ph.h, .stride = (uint32_t)ph.w * 2 },
            .data_size = (uint32_t)ph.w * ph.h * 2,
            .data = (const uint8_t *)ph.pixels,
        };
        lv_image_set_src(s_ret.img, &s_ret.dsc);
        lv_obj_invalidate(s_ret.img); /* same buffer, new pixels */
        lv_obj_remove_flag(s_ret.img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ret.hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ret.img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ret.hint, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *avo_face_retrato_create(lv_obj_t *parent, const avo_time_t *t)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *root = avo_face_root(parent);
    s_ret.root = root;
    lv_color_t top, bottom;
    avo_hue_grad(AVO_HUE_ROSE, &top, &bottom);
    lv_obj_set_style_bg_color(root, lv_color_mix(bottom, lv_color_black(), 70), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    s_ret.img = lv_image_create(root);
    lv_obj_set_pos(s_ret.img, 0, 0);
    lv_obj_remove_flag(s_ret.img, LV_OBJ_FLAG_CLICKABLE);

    s_ret.hint = lv_obj_create(root);
    lv_obj_remove_style_all(s_ret.hint);
    lv_obj_set_size(s_ret.hint, AVO_W - 2 * AVO_PAD - 20, LV_SIZE_CONTENT);
    lv_obj_align(s_ret.hint, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_flex_flow(s_ret.hint, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ret.hint, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_ret.hint, 10, 0);
    lv_obj_remove_flag(s_ret.hint, LV_OBJ_FLAG_CLICKABLE);
    avo_label(s_ret.hint, &avo_font_40, lv_color_white(), LV_SYMBOL_IMAGE);
    lv_obj_t *l = avo_label(s_ret.hint, &avo_font_22, lv_color_white(),
                            "Pon aquí tu foto: Ajustes › Enviar al reloj, desde el iPhone.");
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);

    /* shade so the time reads on any photo */
    lv_obj_t *shade = lv_obj_create(root);
    lv_obj_remove_style_all(shade);
    lv_obj_set_size(shade, AVO_W, SHADE_H);
    lv_obj_set_style_bg_opa(shade, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(shade, lv_color_black(), 0);
    lv_obj_set_style_bg_grad_color(shade, lv_color_black(), 0);
    lv_obj_set_style_bg_main_opa(shade, LV_OPA_70, 0);
    lv_obj_set_style_bg_grad_opa(shade, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_dir(shade, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(shade, LV_OBJ_FLAG_CLICKABLE);

    s_ret.date = avo_label(root, &avo_font_22, lv_color_white(), "");
    lv_obj_set_pos(s_ret.date, AVO_PAD + 18, 22);
    s_ret.time = avo_label(root, &avo_font_digits_76, lv_color_white(), "");
    lv_obj_set_pos(s_ret.time, AVO_PAD + 12, 48);
    (void)p;

    retrato_set_photo();
    retrato_set_time(t);
    s_ret.last_min = t->min;
    return root;
}

static void retrato_tick(const avo_time_t *t)
{
    if (!s_ret.root) {
        return;
    }
    if (t->min != s_ret.last_min) {
        s_ret.last_min = t->min;
        retrato_set_time(t);
    }
    avo_photo_t ph;
    avo_hal_photo(&ph);
    if (ph.version != s_ret.photo_version) {
        retrato_set_photo(); /* a new photo arrived, or it was removed */
    }
}

/* ================================================================= Órbita */

#define ORB_CX 205
#define ORB_CY 300
#define ORB_R 120
#define SUN_D 30
#define MOON_D 48

static struct {
    lv_obj_t *root, *time, *date, *sun, *rise, *set, *moon_lbl, *moon;
    lv_draw_buf_t *moon_buf;
    avo_sun_t sun_times;
    bool sun_ok;
    int last_min, last_day, moon_hour;
} s_orb;

static void location(double *lat, double *lon)
{
    if (!avo_hal_location(lat, lon)) {
        *lat = DEFAULT_LAT;
        *lon = DEFAULT_LON;
    }
}

static lv_obj_t *path_arc(lv_obj_t *root, int start, int end, lv_color_t color)
{
    lv_obj_t *a = lv_arc_create(root);
    lv_obj_set_size(a, 2 * ORB_R, 2 * ORB_R);
    lv_obj_set_pos(a, ORB_CX - ORB_R, ORB_CY - ORB_R);
    lv_arc_set_bg_angles(a, (lv_value_precise_t)start, (lv_value_precise_t)end);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_remove_style(a, NULL, LV_PART_INDICATOR);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, color, LV_PART_MAIN);
    return a;
}

/* Moon disc drawn pixel by pixel: lit where x is past the terminator. */
static void paint_moon(double age)
{
    const int d = MOON_D, ss = 2; /* 2x2 supersampling for smooth edges */
    double k = cos(2 * 3.14159265358979 * age / AVO_MOON_SYNODIC);
    bool waxing = avo_moon_waxing(age);
    lv_color_t lit = lv_color_hex(0xEDEBE3), dark = lv_color_hex(0x2A2B30);
    for (int py = 0; py < d; py++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s_orb.moon_buf->data + py * s_orb.moon_buf->header.stride);
        for (int px = 0; px < d; px++) {
            int in = 0, on = 0;
            for (int sy = 0; sy < ss; sy++) {
                for (int sx = 0; sx < ss; sx++) {
                    double x = ((px + (sx + 0.5) / ss) / d) * 2 - 1, y = ((py + (sy + 0.5) / ss) / d) * 2 - 1;
                    if (x * x + y * y > 1) continue;
                    in++;
                    double edge = k * sqrt(1 - y * y);
                    on += waxing ? (x >= edge) : (x <= -edge);
                }
            }
            lv_color_t c = on * 2 >= in ? lit : dark;
            uint8_t a = (uint8_t)(255 * in / (ss * ss));
            row[px] = (uint32_t)a << 24 | (uint32_t)c.red << 16 | (uint32_t)c.green << 8 | c.blue;
        }
    }
    lv_obj_invalidate(s_orb.moon);
}

static void moon_deleted_cb(lv_event_t *e)
{
    lv_draw_buf_t *buf = lv_event_get_user_data(e);
    if (buf == s_orb.moon_buf) {
        s_orb.moon_buf = NULL;
    }
    lv_draw_buf_destroy(buf);
}

static void orbit_set_sun(const avo_time_t *t)
{
    int minute = t->hour * 60 + t->min;
    double a = avo_sun_path_angle(&s_orb.sun_times, minute) * 3.14159265358979 / 180.0;
    lv_obj_set_pos(s_orb.sun, ORB_CX + (int32_t)lround(ORB_R * cos(a)) - SUN_D / 2,
                   ORB_CY + (int32_t)lround(ORB_R * sin(a)) - SUN_D / 2);
    bool up = s_orb.sun_times.polar_day ||
              (!s_orb.sun_times.polar_night && minute >= s_orb.sun_times.rise && minute <= s_orb.sun_times.set);
    lv_obj_set_style_bg_opa(s_orb.sun, up ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_shadow_opa(s_orb.sun, up ? LV_OPA_60 : LV_OPA_TRANSP, 0);
}

static void orbit_set_day(const avo_time_t *t)
{
    double lat, lon;
    location(&lat, &lon);
    s_orb.sun_ok = avo_sun_times(lat, lon, t->year, t->month, t->day, avo_settings()->utc_offset_min, &s_orb.sun_times);
    char buf[24];
    if (s_orb.sun_ok) {
        avo_time_t r = { .hour = s_orb.sun_times.rise / 60, .min = s_orb.sun_times.rise % 60 };
        avo_time_t st = { .hour = s_orb.sun_times.set / 60, .min = s_orb.sun_times.set % 60 };
        char hm[12];
        avo_fmt_hm(hm, sizeof hm, &r, avo_settings()->h24);
        snprintf(buf, sizeof buf, LV_SYMBOL_UP " %s", hm);
        lv_label_set_text(s_orb.rise, buf);
        avo_fmt_hm(hm, sizeof hm, &st, avo_settings()->h24);
        snprintf(buf, sizeof buf, LV_SYMBOL_DOWN " %s", hm);
        lv_label_set_text(s_orb.set, buf);
    } else {
        lv_label_set_text(s_orb.rise, s_orb.sun_times.polar_day ? "Sol de medianoche" : "Noche polar");
        lv_label_set_text(s_orb.set, "");
    }
    s_orb.last_day = t->day;
}

static void orbit_set_moon(const avo_time_t *t)
{
    double age = avo_moon_age(avo_time_to_epoch(t, avo_settings()->utc_offset_min));
    char buf[48];
    snprintf(buf, sizeof buf, "%s · %d %%", avo_moon_phase_name(age), (int)lround(avo_moon_illumination(age) * 100));
    lv_label_set_text(s_orb.moon_lbl, buf);
    if (s_orb.moon_buf) {
        paint_moon(age);
    }
    s_orb.moon_hour = t->hour;
}

static void orbit_set_time(const avo_time_t *t)
{
    char buf[32];
    avo_fmt_hm(buf, sizeof buf, t, avo_settings()->h24);
    lv_label_set_text(s_orb.time, buf);
    avo_fmt_wday_day(buf, sizeof buf, t);
    lv_label_set_text(s_orb.date, buf);
}

lv_obj_t *avo_face_orbit_create(lv_obj_t *parent, const avo_time_t *t)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *root = avo_face_root(parent);
    s_orb.root = root;
    lv_color_t solar = avo_theme_is_avocado() ? p->accent : avo_hue(AVO_HUE_SOLAR);

    s_orb.date = avo_label(root, &avo_font_22, solar, "");
    lv_obj_set_pos(s_orb.date, AVO_PAD + 18, 22);
    s_orb.time = avo_label(root, &avo_font_digits_76, p->label, "");
    lv_obj_set_pos(s_orb.time, AVO_PAD + 12, 48);

    path_arc(root, 180, 360, lv_color_mix(solar, lv_color_black(), 150));
    path_arc(root, 0, 180, lv_color_hex(0x2A2B30));
    static lv_point_precise_t horizon[2] = { { ORB_CX - ORB_R - 30, ORB_CY }, { ORB_CX + ORB_R + 30, ORB_CY } };
    lv_obj_t *line = lv_line_create(root);
    lv_line_set_points(line, horizon, 2);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, p->label2, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_50, 0);

    s_orb.rise = avo_label(root, &avo_font_22, p->label2, "");
    lv_obj_set_pos(s_orb.rise, AVO_PAD + 6, ORB_CY + 10);
    s_orb.set = avo_label(root, &avo_font_22, p->label2, "");
    lv_obj_align(s_orb.set, LV_ALIGN_TOP_RIGHT, -(AVO_PAD + 6), ORB_CY + 10);

    s_orb.moon_buf = lv_draw_buf_create(MOON_D, MOON_D, LV_COLOR_FORMAT_ARGB8888, 0);
    s_orb.moon = lv_image_create(root);
    if (s_orb.moon_buf) {
        lv_image_set_src(s_orb.moon, s_orb.moon_buf);
        /* the buffer lives exactly as long as its image */
        lv_obj_add_event_cb(s_orb.moon, moon_deleted_cb, LV_EVENT_DELETE, s_orb.moon_buf);
    }
    lv_obj_set_pos(s_orb.moon, ORB_CX - MOON_D / 2, ORB_CY + 44);
    s_orb.moon_lbl = avo_label(root, &avo_font_22, p->label2, "");
    lv_obj_align(s_orb.moon_lbl, LV_ALIGN_TOP_MID, 0, ORB_CY + ORB_R + 12);

    s_orb.sun = lv_obj_create(root);
    lv_obj_remove_style_all(s_orb.sun);
    lv_obj_set_size(s_orb.sun, SUN_D, SUN_D);
    lv_obj_set_style_radius(s_orb.sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_orb.sun, solar, 0);
    lv_obj_set_style_shadow_color(s_orb.sun, solar, 0);
    lv_obj_set_style_shadow_width(s_orb.sun, 24, 0);
    lv_obj_remove_flag(s_orb.sun, LV_OBJ_FLAG_CLICKABLE);

    orbit_set_time(t);
    orbit_set_day(t);
    orbit_set_sun(t);
    orbit_set_moon(t);
    s_orb.last_min = t->min;
    return root;
}

static void orbit_tick(const avo_time_t *t)
{
    if (!s_orb.root || t->min == s_orb.last_min) {
        return;
    }
    s_orb.last_min = t->min;
    orbit_set_time(t);
    if (t->day != s_orb.last_day) {
        orbit_set_day(t);
    }
    orbit_set_sun(t);
    if (t->hour != s_orb.moon_hour) {
        orbit_set_moon(t);
    }
}

/* ================================================================= shared */

void avo_faces_extra_tick(const avo_time_t *t)
{
    retrato_tick(t);
    orbit_tick(t);
}

void avo_faces_extra_forget(void)
{
    lv_memzero(&s_ret, sizeof s_ret);
    lv_memzero(&s_orb, sizeof s_orb);
    s_ret.last_min = s_orb.last_min = -1;
}
