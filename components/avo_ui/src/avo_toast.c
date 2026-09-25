/*
 * Toast: a short confirmation pill at the top of the screen ("Alarma
 * pospuesta…"), like the HUDs of watchOS. It never takes input and fades
 * away by itself; a newer toast replaces the one on screen.
 */
#include "avo_ui_internal.h"

#define TOAST_MS 2200
#define FADE_MS 180
#define TOAST_Y 18

static lv_obj_t *s_toast;

static void opa_cb(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }

static void gone_cb(lv_anim_t *a)
{
    lv_obj_t *t = a->var;
    if (t == s_toast) {
        s_toast = NULL;
    }
    lv_obj_delete_async(t);
}

void avo_toast(const char *symbol, const char *text)
{
    const avo_palette_t *p = avo_pal();
    if (s_toast) {
        lv_anim_delete(s_toast, opa_cb);
        lv_obj_delete(s_toast);
    }
    lv_obj_t *t = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(t, AVO_W - 2 * AVO_PAD, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, TOAST_Y);
    lv_obj_set_style_bg_color(t, p->surface, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(t, 1, 0);
    lv_obj_set_style_border_color(t, p->glass_edge, 0);
    lv_obj_set_style_pad_hor(t, 22, 0);
    lv_obj_set_style_pad_ver(t, 12, 0);
    lv_obj_set_style_pad_column(t, 10, 0);
    lv_obj_set_flex_flow(t, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if (symbol) {
        avo_label(t, &avo_font_22, avo_theme_is_avocado() ? p->accent : avo_hue(AVO_HUE_SOLAR), symbol);
    }
    lv_obj_t *l = avo_label(t, &avo_font_22, p->label, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    s_toast = t;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, t);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, FADE_MS);
    lv_anim_set_reverse_delay(&a, TOAST_MS);
    lv_anim_set_reverse_duration(&a, FADE_MS + 70);
    lv_anim_set_completed_cb(&a, gone_cb);
    lv_anim_start(&a);
}
