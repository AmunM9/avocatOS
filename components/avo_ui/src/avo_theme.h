/* avocatOS design tokens and shared LVGL styles (internal to avo_ui). */
#pragma once

#include "lvgl.h"
#include "avo_core.h"

/* Screen geometry of the 2.06" AMOLED */
#define AVO_W 410
#define AVO_H 502
#define AVO_PAD 20        /* side margin inside the rounded glass        */
#define AVO_TOP 18        /* top inset for titles / status                */
#define AVO_RADIUS_CARD 28
#define AVO_ANIM_MS 220   /* default transition                           */

LV_FONT_DECLARE(avo_font_22);
LV_FONT_DECLARE(avo_font_26);
LV_FONT_DECLARE(avo_font_30);
LV_FONT_DECLARE(avo_font_40);
LV_FONT_DECLARE(avo_font_digits_76);

LV_IMAGE_DECLARE(avo_img_mark_160);
LV_IMAGE_DECLARE(avo_img_mark_56);
LV_IMAGE_DECLARE(avo_img_half_300);
LV_IMAGE_DECLARE(avo_img_half_36);
LV_IMAGE_DECLARE(avo_img_phone_27); /* mirrored handset, A8 */
LV_IMAGE_DECLARE(avo_img_phone_36);

/* Semantic app colors: each app keeps one color everywhere. */
typedef enum {
    AVO_HUE_EMBER = 0, /* red       */
    AVO_HUE_SOLAR,     /* amber     */
    AVO_HUE_LIME,
    AVO_HUE_MINT,
    AVO_HUE_SKY,
    AVO_HUE_IRIS,
    AVO_HUE_ROSE,
    AVO_HUE_GRAPHITE,
    AVO_HUE_COUNT,
} avo_hue_t;

typedef struct {
    avo_theme_mode_t mode;
    lv_color_t bg;          /* always black: AMOLED pixels off           */
    lv_color_t surface;     /* list rows, cards                           */
    lv_color_t surface_hi;  /* pressed rows                               */
    lv_color_t label;
    lv_color_t label2;      /* secondary text                             */
    lv_color_t accent;      /* system tint                                */
    lv_color_t on_accent;   /* text/icon on accent fill                   */
    lv_color_t glass;       /* translucent panels (use with glass_opa)    */
    lv_opa_t glass_opa;
    lv_color_t glass_edge;
    lv_color_t good, warn, bad;
    /* avocado anatomy, used for the winks in avocado mode */
    lv_color_t skin, rind, flesh, pit;
} avo_palette_t;

typedef struct {
    lv_style_t screen;      /* black screen, no padding, no scrollbars   */
    lv_style_t title;       /* 30px accent title                         */
    lv_style_t text;        /* 26px primary                              */
    lv_style_t text2;       /* 22px secondary                            */
    lv_style_t glass;       /* translucent rounded panel                 */
    lv_style_t row;         /* grouped list row                          */
    lv_style_t row_pressed;
    lv_style_t accent_fill; /* switches, sliders, primary buttons        */
    lv_style_t knob;
    lv_style_t track;
} avo_styles_t;

void avo_theme_init(avo_theme_mode_t mode);
void avo_theme_apply(avo_theme_mode_t mode); /* updates styles live */
const avo_palette_t *avo_pal(void);
avo_styles_t *avo_sty(void);
bool avo_theme_is_avocado(void);

/* Icon fill for an app hue in the current theme. */
lv_color_t avo_hue(avo_hue_t hue);
/* Bright/dark pair for gradients in the current theme. */
void avo_hue_grad(avo_hue_t hue, lv_color_t *top, lv_color_t *bottom);

/* ---------------------------------------------------------------- builders */
lv_obj_t *avo_screen_create(void);
lv_obj_t *avo_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *txt);
lv_obj_t *avo_glass(lv_obj_t *parent);
/* Round app icon: colored disc + white symbol; avocado mode draws it as a
 * sliced avocado (dark rind ring, flesh fill, colored glyph). */
/* Glyph size that fits a round icon of diameter d. */
const lv_font_t *avo_symbol_font(int32_t d);
lv_obj_t *avo_app_icon(lv_obj_t *parent, avo_hue_t hue, const char *symbol, int32_t diameter);
/* Same disc with an A8 image (e.g. the mirrored phone) instead of a glyph. */
lv_obj_t *avo_app_icon_image(lv_obj_t *parent, avo_hue_t hue, const lv_image_dsc_t *a8, int32_t diameter);
/* An A8 image drawn in `color`. */
lv_obj_t *avo_mask_image(lv_obj_t *parent, const lv_image_dsc_t *a8, lv_color_t color);
/* Scrollable page with a colored title, used by every app. */
lv_obj_t *avo_page_create(lv_obj_t *screen, const char *title, avo_hue_t hue);
/* Grouped list row: symbol + text (+ optional value on the right). */
lv_obj_t *avo_row(lv_obj_t *parent, avo_hue_t hue, const char *symbol, const char *text, const char *value);
lv_obj_t *avo_row_switch(lv_obj_t *parent, avo_hue_t hue, const char *symbol, const char *text,
                         bool on, lv_event_cb_t cb, void *user);
lv_obj_t *avo_section(lv_obj_t *parent, const char *caption);
lv_obj_t *avo_switch(lv_obj_t *parent, bool on);
lv_obj_t *avo_slider(lv_obj_t *parent, int32_t min, int32_t max, int32_t val);
