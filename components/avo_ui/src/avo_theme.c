#include "avo_theme.h"

/* ------------------------------------------------------------------ */
/* Palettes                                                            */
/* ------------------------------------------------------------------ */

#define HEX(x) LV_COLOR_MAKE(((x) >> 16) & 0xFF, ((x) >> 8) & 0xFF, (x) & 0xFF)

static const avo_palette_t PAL_CLEAN = {
    .mode = AVO_THEME_CLEAN,
    .bg = HEX(0x000000), .surface = HEX(0x1C1C1F), .surface_hi = HEX(0x2C2C31),
    .label = HEX(0xFFFFFF), .label2 = HEX(0x9A9CA6),
    .accent = HEX(0x4DB8FF), .on_accent = HEX(0x04121C),
    .glass = HEX(0xFFFFFF), .glass_opa = 34, .glass_edge = HEX(0x3A3B42),
    .good = HEX(0x3DDC97), .warn = HEX(0xFFB547), .bad = HEX(0xFF5A4E),
    .skin = HEX(0x2B461C), .rind = HEX(0x7CB342), .flesh = HEX(0xE4EEAB), .pit = HEX(0x7A4A29),
};

static const avo_palette_t PAL_AVOCADO = {
    .mode = AVO_THEME_AVOCADO,
    .bg = HEX(0x000000), .surface = HEX(0x172112), .surface_hi = HEX(0x24321B),
    .label = HEX(0xF4F7E4), .label2 = HEX(0xA9B58C),
    .accent = HEX(0x9ACD4B), .on_accent = HEX(0x172112),
    .glass = HEX(0xE4EEAB), .glass_opa = 30, .glass_edge = HEX(0x3B5226),
    .good = HEX(0x9ACD4B), .warn = HEX(0xE0B04A), .bad = HEX(0xE8664F),
    .skin = HEX(0x2B461C), .rind = HEX(0x7CB342), .flesh = HEX(0xE4EEAB), .pit = HEX(0x7A4A29),
};

static const uint32_t HUE_CLEAN[AVO_HUE_COUNT] = {
    0xFF5A4E, 0xFFB547, 0xA6E34B, 0x3DDC97, 0x4DB8FF, 0x8B7CFF, 0xFF6FAE, 0x8E93A3,
};
/* Earthy counterparts that read well on the avocado flesh color. */
static const uint32_t HUE_AVOCADO[AVO_HUE_COUNT] = {
    0xC8553D, 0xC9922C, 0x5E9A2E, 0x3F8A5C, 0x3F7F99, 0x6D5FA8, 0xB85C7F, 0x55624A,
};

static const avo_palette_t *s_pal = &PAL_CLEAN;
static avo_styles_t s_sty;
static bool s_inited;

const avo_palette_t *avo_pal(void) { return s_pal; }
avo_styles_t *avo_sty(void) { return &s_sty; }
bool avo_theme_is_avocado(void) { return s_pal->mode == AVO_THEME_AVOCADO; }

lv_color_t avo_hue(avo_hue_t hue)
{
    const uint32_t *tbl = avo_theme_is_avocado() ? HUE_AVOCADO : HUE_CLEAN;
    return lv_color_hex(tbl[hue < AVO_HUE_COUNT ? hue : AVO_HUE_GRAPHITE]);
}

void avo_hue_grad(avo_hue_t hue, lv_color_t *top, lv_color_t *bottom)
{
    lv_color_t c = lv_color_hex(HUE_CLEAN[hue < AVO_HUE_COUNT ? hue : AVO_HUE_GRAPHITE]);
    *top = lv_color_lighten(c, 50);
    *bottom = lv_color_darken(c, 20);
}

/* ------------------------------------------------------------------ */
/* Styles                                                              */
/* ------------------------------------------------------------------ */

static void fill_styles(void)
{
    const avo_palette_t *p = s_pal;

    lv_style_set_bg_color(&s_sty.screen, p->bg);
    lv_style_set_bg_opa(&s_sty.screen, LV_OPA_COVER);
    lv_style_set_text_color(&s_sty.screen, p->label);
    lv_style_set_text_font(&s_sty.screen, &avo_font_26);
    lv_style_set_pad_all(&s_sty.screen, 0);
    lv_style_set_border_width(&s_sty.screen, 0);

    lv_style_set_text_font(&s_sty.title, &avo_font_30);

    lv_style_set_text_font(&s_sty.text, &avo_font_26);
    lv_style_set_text_color(&s_sty.text, p->label);

    lv_style_set_text_font(&s_sty.text2, &avo_font_22);
    lv_style_set_text_color(&s_sty.text2, p->label2);

    lv_style_set_bg_color(&s_sty.glass, p->glass);
    lv_style_set_bg_opa(&s_sty.glass, p->glass_opa);
    lv_style_set_border_color(&s_sty.glass, p->glass_edge);
    lv_style_set_border_width(&s_sty.glass, 1);
    lv_style_set_radius(&s_sty.glass, AVO_RADIUS_CARD);
    lv_style_set_pad_all(&s_sty.glass, 16);

    lv_style_set_bg_color(&s_sty.row, p->surface);
    lv_style_set_bg_opa(&s_sty.row, LV_OPA_COVER);
    lv_style_set_radius(&s_sty.row, 26);
    lv_style_set_border_width(&s_sty.row, 0);
    lv_style_set_pad_hor(&s_sty.row, 16);
    lv_style_set_pad_ver(&s_sty.row, 12);
    lv_style_set_pad_column(&s_sty.row, 14);
    lv_style_set_min_height(&s_sty.row, 76);

    lv_style_set_bg_color(&s_sty.row_pressed, p->surface_hi);
    lv_style_set_transform_scale(&s_sty.row_pressed, 246); /* subtle press-in */

    lv_style_set_bg_color(&s_sty.accent_fill, p->accent);
    lv_style_set_bg_opa(&s_sty.accent_fill, LV_OPA_COVER);

    lv_style_set_bg_color(&s_sty.knob, lv_color_white());
    lv_style_set_bg_opa(&s_sty.knob, LV_OPA_COVER);
    lv_style_set_pad_all(&s_sty.knob, -4);

    lv_style_set_bg_color(&s_sty.track, lv_color_hex(0x39393D));
    lv_style_set_bg_opa(&s_sty.track, LV_OPA_COVER);
}

void avo_theme_init(avo_theme_mode_t mode)
{
    if (!s_inited) {
        lv_style_t *all[] = {
            &s_sty.screen, &s_sty.title, &s_sty.text, &s_sty.text2, &s_sty.glass, &s_sty.row,
            &s_sty.row_pressed, &s_sty.accent_fill, &s_sty.knob, &s_sty.track,
        };
        for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
            lv_style_init(all[i]);
        }
        s_inited = true;
    }
    s_pal = (mode == AVO_THEME_AVOCADO) ? &PAL_AVOCADO : &PAL_CLEAN;
    fill_styles();
}

void avo_theme_apply(avo_theme_mode_t mode)
{
    avo_theme_init(mode);
    lv_obj_report_style_change(NULL); /* every object using the styles refreshes */
}

/* ------------------------------------------------------------------ */
/* Builders                                                            */
/* ------------------------------------------------------------------ */

lv_obj_t *avo_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_add_style(scr, &s_sty.screen, 0);
    lv_obj_set_size(scr, AVO_W, AVO_H);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *avo_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, txt ? txt : "");
    return l;
}

lv_obj_t *avo_glass(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_add_style(o, &s_sty.glass, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

const lv_font_t *avo_symbol_font(int32_t d)
{
    if (d >= 84) return &avo_font_40;
    if (d >= 58) return &avo_font_30;
    if (d >= 44) return &avo_font_26;
    return &avo_font_22;
}

static lv_obj_t *disc(lv_obj_t *parent, int32_t d)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* Coloured disc (or avocado slice); returns where the glyph goes and its colour. */
static lv_obj_t *icon_base(lv_obj_t *parent, avo_hue_t hue, int32_t d, lv_obj_t **glyph_parent_out,
                           lv_color_t *glyph_color)
{
    lv_obj_t *outer = disc(parent, d);
    lv_obj_t *glyph_parent = outer;
    if (avo_theme_is_avocado()) {
        /* sliced avocado: skin ring, thin rind line, flesh center */
        lv_obj_set_style_bg_color(outer, s_pal->skin, 0);
        int32_t inner_d = d - LV_MAX(4, d / 7) * 2;
        lv_obj_t *inner = disc(outer, inner_d);
        lv_obj_center(inner);
        lv_obj_set_style_bg_color(inner, s_pal->flesh, 0);
        lv_obj_set_style_border_color(inner, s_pal->rind, 0);
        lv_obj_set_style_border_width(inner, LV_MAX(2, d / 26), 0);
        lv_obj_remove_flag(inner, LV_OBJ_FLAG_CLICKABLE);
        glyph_parent = inner;
    } else {
        lv_color_t top, bottom;
        avo_hue_grad(hue, &top, &bottom);
        lv_obj_set_style_bg_color(outer, top, 0);
        lv_obj_set_style_bg_grad_color(outer, bottom, 0);
        lv_obj_set_style_bg_grad_dir(outer, LV_GRAD_DIR_VER, 0);
    }
    *glyph_color = avo_theme_is_avocado() ? avo_hue(hue) : lv_color_white();
    *glyph_parent_out = glyph_parent;
    return outer;
}

lv_obj_t *avo_app_icon(lv_obj_t *parent, avo_hue_t hue, const char *symbol, int32_t d)
{
    lv_obj_t *glyph_parent;
    lv_color_t color;
    lv_obj_t *outer = icon_base(parent, hue, d, &glyph_parent, &color);
    lv_obj_t *g = avo_label(glyph_parent, avo_symbol_font(d), color, symbol);
    lv_obj_center(g);
    return outer;
}

lv_obj_t *avo_mask_image(lv_obj_t *parent, const lv_image_dsc_t *a8, lv_color_t color)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, a8);
    lv_obj_set_style_image_recolor(img, color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
    return img;
}

lv_obj_t *avo_app_icon_image(lv_obj_t *parent, avo_hue_t hue, const lv_image_dsc_t *a8, int32_t d)
{
    lv_obj_t *glyph_parent;
    lv_color_t color;
    lv_obj_t *outer = icon_base(parent, hue, d, &glyph_parent, &color);
    lv_obj_center(avo_mask_image(glyph_parent, a8, color));
    return outer;
}

lv_obj_t *avo_page_create(lv_obj_t *screen, const char *title, avo_hue_t hue)
{
    lv_obj_t *t = avo_label(screen, &avo_font_30, avo_theme_is_avocado() ? s_pal->accent : avo_hue(hue), title);
    lv_obj_set_pos(t, AVO_PAD + 14, AVO_TOP + 6);

    lv_obj_t *page = lv_obj_create(screen);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, AVO_W, AVO_H - 64);
    lv_obj_set_pos(page, 0, 64);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(page, AVO_PAD - 4, 0);
    lv_obj_set_style_pad_top(page, 8, 0);
    lv_obj_set_style_pad_bottom(page, 60, 0);
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page, LV_DIR_VER);
    lv_obj_add_flag(page, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM);
    return page;
}

lv_obj_t *avo_section(lv_obj_t *parent, const char *caption)
{
    lv_obj_t *l = avo_label(parent, &avo_font_22, s_pal->label2, caption);
    lv_obj_set_style_pad_left(l, 14, 0);
    lv_obj_set_style_pad_top(l, 14, 0);
    return l;
}

static lv_obj_t *row_base(lv_obj_t *parent, avo_hue_t hue, const char *symbol, const char *text)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_add_style(r, &s_sty.row, 0);
    lv_obj_add_style(r, &s_sty.row_pressed, LV_STATE_PRESSED);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_height(r, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(r, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (symbol) {
        lv_obj_t *ic = avo_app_icon(r, hue, symbol, 44);
        lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *l = avo_label(r, &avo_font_26, s_pal->label, text);
    lv_obj_set_flex_grow(l, 1);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return r;
}

lv_obj_t *avo_row(lv_obj_t *parent, avo_hue_t hue, const char *symbol, const char *text, const char *value)
{
    lv_obj_t *r = row_base(parent, hue, symbol, text);
    if (value) {
        lv_obj_t *v = avo_label(r, &avo_font_22, s_pal->label2, value);
        lv_obj_set_style_max_width(v, 170, 0);
        lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_DOTS);
    }
    return r;
}

lv_obj_t *avo_switch(lv_obj_t *parent, bool on)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 70, 42);
    lv_obj_add_style(sw, &s_sty.track, 0);
    lv_obj_add_style(sw, &s_sty.accent_fill, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(sw, &s_sty.knob, LV_PART_KNOB);
    lv_obj_set_style_anim_duration(sw, 160, 0);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    return sw;
}

lv_obj_t *avo_row_switch(lv_obj_t *parent, avo_hue_t hue, const char *symbol, const char *text,
                         bool on, lv_event_cb_t cb, void *user)
{
    lv_obj_t *r = row_base(parent, hue, symbol, text);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *sw = avo_switch(r, on);
    if (cb) {
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, user);
    }
    return sw;
}

lv_obj_t *avo_slider(lv_obj_t *parent, int32_t min, int32_t max, int32_t val)
{
    lv_obj_t *s = lv_slider_create(parent);
    lv_obj_set_width(s, lv_pct(100));
    lv_obj_set_height(s, 14);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, val, LV_ANIM_OFF);
    lv_obj_add_style(s, &s_sty.track, 0);
    lv_obj_add_style(s, &s_sty.accent_fill, LV_PART_INDICATOR);
    lv_obj_add_style(s, &s_sty.knob, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s, 20);
    return s;
}
