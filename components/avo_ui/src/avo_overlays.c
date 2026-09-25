/*
 * Overlays drawn on lv_layer_top, above whatever screen is active:
 *  - notification banner (slides down, tap opens it, swipe up / timeout hides)
 *  - incoming call (accept / decline through ANCS)
 *  - charging animation when USB is plugged in (ring fills to the level)
 * Only one overlay exists at a time; a newer one replaces the older.
 */
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"

#define BANNER_MS 5000
#define CHARGE_MS 2800
#define CHARGE_RING 260
#define CALL_BTN 104

typedef enum { OV_NONE, OV_BANNER, OV_CALL, OV_CHARGE } ov_kind_t;

static struct {
    ov_kind_t kind;
    lv_obj_t *root;
    uint32_t uid;
    uint32_t until_ms;
} ov;

bool avo_overlay_active(void) { return ov.kind != OV_NONE; }

static void close_now(void)
{
    if (ov.root) {
        lv_obj_delete(ov.root);
    }
    memset(&ov, 0, sizeof ov);
}

bool avo_overlay_dismiss(void)
{
    if (ov.kind == OV_NONE) {
        return false;
    }
    close_now();
    return true;
}

static lv_obj_t *overlay_root(ov_kind_t kind, bool full_screen)
{
    close_now();
    lv_obj_t *r = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(r);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    if (full_screen) {
        lv_obj_set_size(r, AVO_W, AVO_H);
        lv_obj_set_style_bg_color(r, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); /* opaque: no per-frame blending */
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);    /* block the screen below */
    }
    ov.kind = kind;
    ov.root = r;
    return r;
}

static void slide_y_cb(void *obj, int32_t v) { lv_obj_set_y(obj, v); }


/* ================================================================= banner */

static void banner_click_cb(lv_event_t *e)
{
    (void)e;
    uint32_t uid = ov.uid;
    close_now();
    avo_nav_notifications();
    avo_notif_open_detail(uid);
}

void avo_overlay_banner(const avo_notif_t *n)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *r = overlay_root(OV_BANNER, false);
    ov.uid = n->uid;
    ov.until_ms = avo_hal_millis() + BANNER_MS;
    lv_obj_set_size(r, AVO_W - 2 * AVO_PAD + 8, LV_SIZE_CONTENT);
    lv_obj_set_x(r, AVO_PAD - 4);
    lv_obj_set_style_bg_color(r, p->surface, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(r, AVO_RADIUS_CARD, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, p->glass_edge, 0);
    lv_obj_set_style_pad_all(r, 16, 0);
    lv_obj_set_style_pad_column(r, 14, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(r, banner_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ic = avo_app_icon(r, avo_notif_hue(n->category), avo_notif_symbol(n->category), 52);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *col = lv_obj_create(r);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    avo_label(col, &avo_font_22, p->label2, n->app);
    lv_obj_t *t = avo_label(col, &avo_font_26, p->label, n->title[0] ? n->title : n->app);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    if (n->message[0]) {
        lv_obj_t *m = avo_label(col, &avo_font_22, p->label, n->message);
        lv_obj_set_width(m, lv_pct(100));
        lv_obj_set_height(m, 56); /* two lines */
        lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_DOTS);
    }

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, r);
    lv_anim_set_exec_cb(&a, slide_y_cb);
    lv_anim_set_values(&a, -180, 14);
    lv_anim_set_duration(&a, AVO_ANIM_MS + 60);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    avo_hal_click();
}

/* ================================================================= incoming call */

static void call_btn_cb(lv_event_t *e)
{
    bool accept = (bool)(intptr_t)lv_event_get_user_data(e);
    avo_hal_notif_action(ov.uid, accept);
    close_now();
}

static lv_obj_t *call_button(lv_obj_t *parent, lv_color_t color, const char *sym, bool accept)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, CALL_BTN, CALL_BTN);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_set_style_transform_scale(b, 236, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(b, CALL_BTN / 2, 0);
    lv_obj_set_style_transform_pivot_y(b, CALL_BTN / 2, 0);
    lv_obj_t *l = avo_label(b, &avo_font_40, lv_color_white(), sym);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, call_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)accept);
    return b;
}

void avo_overlay_call(const avo_notif_t *n)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *r = overlay_root(OV_CALL, true);
    ov.uid = n->uid;
    ov.until_ms = 0; /* stays until answered or the call ends */

    lv_obj_t *who = avo_label(r, &avo_font_40, p->label, n->title[0] ? n->title : "Llamada");
    lv_obj_set_width(who, AVO_W - 2 * AVO_PAD);
    lv_obj_set_style_text_align(who, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(who, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(who, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_t *what = avo_label(r, &avo_font_26, p->label2, n->app[0] ? n->app : "iPhone");
    lv_obj_align_to(what, who, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_t *ic = avo_app_icon(r, AVO_HUE_MINT, LV_SYMBOL_CALL, 64);
    lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 26);

    lv_obj_t *row = lv_obj_create(r);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, AVO_W, CALL_BTN + 10);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    call_button(row, avo_hue(AVO_HUE_EMBER), LV_SYMBOL_CLOSE, false);
    call_button(row, lv_color_hex(0x30D158), LV_SYMBOL_CALL, true);
}

/* ================================================================= charging */

static void arc_value_cb(void *arc, int32_t v) { lv_arc_set_value(arc, v); }
static void zoom_cb(void *obj, int32_t v) { lv_obj_set_style_transform_scale(obj, v, 0); }

void avo_overlay_charging(const avo_battery_t *b)
{
    const avo_palette_t *p = avo_pal();
    bool avo = avo_theme_is_avocado();
    lv_obj_t *r = overlay_root(OV_CHARGE, true);
    ov.until_ms = avo_hal_millis() + CHARGE_MS;
    lv_color_t ring = avo ? p->rind : lv_color_hex(0x30D158);

    lv_obj_t *arc = lv_arc_create(r);
    lv_obj_set_size(arc, CHARGE_RING, CHARGE_RING);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, -30);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 18, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 18, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_mix(ring, lv_color_black(), 60), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, ring, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    lv_obj_t *center;
    if (avo) {
        /* the avocado fills up: the pit carries the bolt */
        center = lv_image_create(arc);
        lv_image_set_src(center, &avo_img_mark_160);
        lv_image_set_scale(center, 190);
        lv_obj_t *bolt = avo_label(center, &avo_font_30, p->flesh, LV_SYMBOL_CHARGE);
        lv_obj_align(bolt, LV_ALIGN_CENTER, 0, 30);
    } else {
        center = avo_label(arc, &avo_font_40, ring, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_font(center, &avo_font_40, 0);
    }
    lv_obj_center(center);
    lv_obj_set_style_transform_pivot_x(center, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(center, lv_pct(50), 0);

    char buf[32];
    if (b->percent >= 0) {
        snprintf(buf, sizeof buf, "%d %%", b->percent);
    } else {
        snprintf(buf, sizeof buf, "USB");
    }
    lv_obj_t *pct = avo_label(r, &avo_font_40, p->label, buf);
    lv_obj_align_to(pct, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);
    lv_obj_t *cap = avo_label(r, &avo_font_22, avo ? p->accent : ring, avo ? "Madurando la batería" : "Cargando");
    lv_obj_align_to(cap, pct, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_value_cb);
    lv_anim_set_values(&a, 0, b->percent >= 0 ? b->percent : 100);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_t z;
    lv_anim_init(&z);
    lv_anim_set_var(&z, center);
    lv_anim_set_exec_cb(&z, zoom_cb);
    lv_anim_set_values(&z, 160, 256);
    lv_anim_set_duration(&z, 420);
    lv_anim_set_delay(&z, 120);
    lv_anim_set_path_cb(&z, lv_anim_path_overshoot);
    lv_anim_start(&z);
}

/* ================================================================= housekeeping */

void avo_overlays_tick(void)
{
    if (ov.kind == OV_NONE) {
        return;
    }
    if (ov.until_ms && (int32_t)(avo_hal_millis() - ov.until_ms) >= 0) {
        close_now();
        return;
    }
    if (ov.kind == OV_CALL) {
        /* the call ended or was answered on the iPhone: its notification is gone */
        avo_notif_t list[AVO_NOTIF_MAX];
        int n = avo_hal_notif_list(list, AVO_NOTIF_MAX);
        bool alive = false;
        for (int i = 0; i < n; i++) {
            alive |= list[i].uid == ov.uid;
        }
        if (!alive) {
            close_now();
        }
    }
}
