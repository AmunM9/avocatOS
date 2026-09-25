/*
 * Honeycomb app grid. Bubbles sit on a hexagonal spiral (avo_hc_slot) and
 * are resized with a fisheye curve (avo_hc_scale) around the screen center.
 * Dragging is handled here (not with LVGL scrolling) so that resizing the
 * bubbles never changes scroll bounds; release keeps some momentum.
 */
#include <stdlib.h>
#include "avo_ui_internal.h"

#define SPACING 124
#define BASE_D 108
#define GLYPH_MIN_D 40
#define CLOCK_MIN_D 70
#define TAP_SLOP 14
#define FRICTION_PCT 90
#define MOMENTUM_MS 16
#define MAX_BUBBLES (1 + 12)

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *glyph;          /* app symbol or the clock text            */
    const lv_font_t *font;    /* current glyph font, changed on thresholds */
    avo_pt_t base;
    const avo_app_t *app; /* NULL for the clock bubble */
} bubble_t;

static struct {
    lv_obj_t *root;
    bubble_t b[MAX_BUBBLES];
    int count;
    int32_t off_x, off_y;   /* content offset (drag)             */
    int32_t lim_x, lim_y;   /* max |offset|                      */
    int32_t vel_x, vel_y;   /* px per frame, x16 fixed point     */
    int32_t drag_total;
    lv_timer_t *momentum;
    lv_obj_t *clock_lbl;
} g;

static const avo_hc_cfg_t FISHEYE = { .full_radius = 110, .fade_radius = 300, .min_scale = 110 };

static void layout(void)
{
    for (int i = 0; i < g.count; i++) {
        bubble_t *b = &g.b[i];
        int32_t cx = b->base.x + g.off_x;
        int32_t cy = b->base.y + g.off_y;
        uint16_t s = avo_hc_scale(cx, cy * 5 / 4, &FISHEYE); /* taller screen: squash y */
        int32_t d = BASE_D * s / AVO_SCALE_ONE;
        /* pull shrinking bubbles toward the center to close the gaps */
        int32_t k = 150 + 106 * s / AVO_SCALE_ONE;  /* /256 */
        int32_t px = AVO_W / 2 + cx * k / 256 - d / 2;
        int32_t py = AVO_H / 2 + cy * k / 256 - d / 2;
        bool visible = px > -d && px < AVO_W && py > -d && py < AVO_H;
        if (!visible) {
            lv_obj_add_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(b->obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(b->obj, px, py);
        lv_obj_set_size(b->obj, d, d);
        if (!b->glyph) {
            continue;
        }
        /* content shrinks with the bubble so it never spills over a neighbour */
        bool is_clock = b->app == NULL;
        if (d < (is_clock ? CLOCK_MIN_D : GLYPH_MIN_D)) {
            lv_obj_add_flag(b->glyph, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(b->glyph, LV_OBJ_FLAG_HIDDEN);
        const lv_font_t *f = is_clock ? (d >= 92 ? &avo_font_26 : &avo_font_22)
                                      : avo_symbol_font(avo_theme_is_avocado() ? d * 3 / 4 : d);
        if (f != b->font) {
            b->font = f;
            lv_obj_set_style_text_font(b->glyph, f, 0);
        }
    }
}

static int32_t clamp32(int32_t v, int32_t lim)
{
    return v < -lim ? -lim : (v > lim ? lim : v);
}

static void momentum_cb(lv_timer_t *t)
{
    (void)t;
    g.vel_x = g.vel_x * FRICTION_PCT / 100;
    g.vel_y = g.vel_y * FRICTION_PCT / 100;
    g.off_x = clamp32(g.off_x + g.vel_x / 16, g.lim_x);
    g.off_y = clamp32(g.off_y + g.vel_y / 16, g.lim_y);
    layout();
    if (abs(g.vel_x) < 16 && abs(g.vel_y) < 16) {
        lv_timer_pause(g.momentum);
    }
}

static void drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    if (code == LV_EVENT_PRESSED) {
        g.drag_total = 0;
        g.vel_x = g.vel_y = 0;
        lv_timer_pause(g.momentum);
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        if (v.x == 0 && v.y == 0) {
            return;
        }
        g.drag_total += abs(v.x) + abs(v.y);
        g.off_x = clamp32(g.off_x + v.x, g.lim_x);
        g.off_y = clamp32(g.off_y + v.y, g.lim_y);
        g.vel_x = (g.vel_x + v.x * 16) / 2;
        g.vel_y = (g.vel_y + v.y * 16) / 2;
        layout();
    } else if (code == LV_EVENT_RELEASED) {
        if (g.drag_total > TAP_SLOP) {
            lv_timer_resume(g.momentum);
        }
    }
}

static void bubble_click_cb(lv_event_t *e)
{
    if (g.drag_total > TAP_SLOP) {
        return; /* it was a drag, not a tap */
    }
    bubble_t *b = lv_event_get_user_data(e);
    avo_hal_click();
    if (b->app) {
        avo_nav_app(b->app);
    } else {
        avo_nav_face();
    }
}

static void grid_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (g.momentum) {
        lv_timer_delete(g.momentum);
    }
    lv_memzero(&g, sizeof g);
}

static lv_obj_t *clock_bubble(lv_obj_t *parent)
{
    const avo_palette_t *p = avo_pal();
    bool avo = avo_theme_is_avocado();
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, avo ? p->pit : lv_color_white(), 0);
    if (avo) {
        lv_obj_set_style_border_width(o, 5, 0);
        lv_obj_set_style_border_color(o, p->rind, 0);
    }
    avo_time_t t;
    avo_hal_time_now(&t);
    char buf[8];
    avo_fmt_hm(buf, sizeof buf, &t, avo_settings()->h24);
    g.clock_lbl = avo_label(o, &avo_font_26, avo ? p->flesh : lv_color_black(), buf);
    lv_obj_center(g.clock_lbl);
    return o;
}

void avo_grid_build(lv_obj_t *screen)
{
    lv_memzero(&g, sizeof g);
    g.root = lv_obj_create(screen);
    lv_obj_remove_style_all(g.root);
    lv_obj_set_size(g.root, AVO_W, AVO_H);
    lv_obj_remove_flag(g.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g.root, drag_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(g.root, grid_deleted_cb, LV_EVENT_DELETE, NULL);

    g.count = LV_MIN(1 + AVO_APP_COUNT, MAX_BUBBLES);
    for (int i = 0; i < g.count; i++) {
        bubble_t *b = &g.b[i];
        b->base = avo_hc_slot(i, SPACING);
        g.lim_x = LV_MAX(g.lim_x, abs(b->base.x));
        g.lim_y = LV_MAX(g.lim_y, abs(b->base.y));
        if (i == 0) {
            b->obj = clock_bubble(g.root);
            b->glyph = g.clock_lbl;
        } else {
            b->app = AVO_APPS[i - 1];
            b->obj = avo_app_icon(g.root, b->app->hue, b->app->symbol, BASE_D);
            lv_obj_t *inner_or_glyph = lv_obj_get_child(b->obj, 0);
            if (avo_theme_is_avocado()) {
                /* inner flesh disc follows the bubble size */
                lv_obj_set_size(inner_or_glyph, lv_pct(78), lv_pct(78));
                lv_obj_center(inner_or_glyph);
                b->glyph = lv_obj_get_child(inner_or_glyph, 0);
            } else {
                b->glyph = inner_or_glyph;
            }
        }
        lv_obj_add_flag(b->obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(b->obj, bubble_click_cb, LV_EVENT_CLICKED, b);
    }
    g.momentum = lv_timer_create(momentum_cb, MOMENTUM_MS, NULL);
    lv_timer_pause(g.momentum);
    layout();
}
