/*
 * Now Playing: shared by the Música app and the page under the Control
 * Center. Controls the iPhone's player through AMS. AMS carries no artwork,
 * so the cover is the album art fetched over Wi-Fi when available, else a
 * gradient generated from the track so every song keeps its own colour.
 */
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"

#define REFRESH_MS 300
#define RECENT_MS (10u * 60u * 1000u) /* keep Now Playing around 10 min after pausing */

static uint32_t s_last_playing_ms;
static bool s_ever_played;

void avo_media_tick(void)
{
    avo_media_t m;
    avo_hal_media(&m);
    if (m.available && m.playing) {
        s_last_playing_ms = avo_hal_millis();
        s_ever_played = true;
    }
}

bool avo_media_recent(void)
{
    avo_media_t m;
    avo_hal_media(&m);
    if (!m.available || !m.title[0]) {
        return false;
    }
    return m.playing || (s_ever_played && avo_hal_millis() - s_last_playing_ms < RECENT_MS);
}

/* ================================================================= widget */

typedef struct {
    lv_obj_t *root, *cover, *cover_img, *cover_glyph, *title, *artist, *source, *play, *vol_bar;
    lv_timer_t *timer;
    uint32_t version, art_version;
    bool had_art;
    lv_image_dsc_t art;
} np_t;

static uint32_t hash_str(const char *a, const char *b)
{
    uint32_t h = 2166136261u; /* FNV-1a */
    for (const char *p = a; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    for (const char *p = b; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    return h;
}

static void paint_generated_cover(np_t *np, const avo_media_t *m)
{
    static const avo_hue_t hues[] = { AVO_HUE_ROSE, AVO_HUE_IRIS, AVO_HUE_SKY, AVO_HUE_MINT, AVO_HUE_SOLAR, AVO_HUE_EMBER };
    uint32_t h = hash_str(m->title, m->artist);
    lv_color_t top, bottom;
    if (avo_theme_is_avocado()) {
        static const uint32_t avo[][2] = { { 0xE4EEAB, 0x7CB342 }, { 0x9ACD4B, 0x2B461C }, { 0xC9D98A, 0x7A4A29 } };
        const uint32_t *c = avo[h % 3];
        top = lv_color_hex(c[0]);
        bottom = lv_color_hex(c[1]);
    } else {
        lv_color_t unused;
        avo_hue_t a = hues[h % 6];
        avo_hue_t b = hues[(h >> 8) % 6] == a ? hues[(h % 6 + 2) % 6] : hues[(h >> 8) % 6];
        avo_hue_grad(a, &top, &unused);
        avo_hue_grad(b, &unused, &bottom);
    }
    lv_obj_set_style_bg_color(np->cover, top, 0);
    lv_obj_set_style_bg_grad_color(np->cover, bottom, 0);
    lv_obj_add_flag(np->cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(np->cover_glyph, LV_OBJ_FLAG_HIDDEN);
}

static void refresh(np_t *np, bool force)
{
    avo_media_t m;
    avo_hal_media(&m);
    avo_artwork_t art;
    bool has_art = avo_hal_artwork(&art) && art.pixels;
    if (!force && m.version == np->version && has_art == np->had_art && (!has_art || art.version == np->art_version)) {
        return;
    }
    np->version = m.version;
    np->had_art = has_art;
    if (!m.available) {
        lv_label_set_text(np->title, "Sin iPhone");
        lv_label_set_text(np->artist, "Empareja tu iPhone en Ajustes › Bluetooth para controlar su música.");
        lv_label_set_text(np->source, "");
        paint_generated_cover(np, &m);
        return;
    }
    lv_label_set_text(np->title, m.title[0] ? m.title : "Nada sonando");
    lv_label_set_text(np->artist, m.artist);
    lv_label_set_text(np->source, m.app[0] ? m.app : "iPhone");
    lv_label_set_text(lv_obj_get_child(np->play, 0), m.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_bar_set_value(np->vol_bar, m.volume, LV_ANIM_ON);
    if (has_art) {
        np->art_version = art.version;
        np->art = (lv_image_dsc_t){
            .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                        .w = art.w, .h = art.h, .stride = (uint32_t)art.w * 2 },
            .data_size = (uint32_t)art.w * art.h * 2,
            .data = (const uint8_t *)art.pixels,
        };
        lv_image_set_src(np->cover_img, &np->art);
        lv_obj_invalidate(np->cover_img); /* the buffer may be reused with new pixels */
        lv_obj_remove_flag(np->cover_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(np->cover_glyph, LV_OBJ_FLAG_HIDDEN);
    } else {
        paint_generated_cover(np, &m);
    }
}

static void timer_cb(lv_timer_t *t)
{
    refresh(lv_timer_get_user_data(t), false);
}

static void root_deleted_cb(lv_event_t *e)
{
    np_t *np = lv_event_get_user_data(e);
    lv_timer_delete(np->timer);
    lv_free(np);
}

static void cmd_cb(lv_event_t *e)
{
    avo_hal_click();
    avo_hal_media_command((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *round_btn(lv_obj_t *parent, int32_t d, const char *sym, uint8_t cmd, bool primary, const lv_font_t *f)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, primary ? (avo_theme_is_avocado() ? p->flesh : lv_color_white()) : p->surface, 0);
    lv_obj_set_style_transform_scale(b, 232, LV_STATE_PRESSED);
    lv_obj_set_style_transform_pivot_x(b, d / 2, 0);
    lv_obj_set_style_transform_pivot_y(b, d / 2, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_t *l = avo_label(b, f, primary ? (avo_theme_is_avocado() ? p->skin : lv_color_black()) : p->label, sym);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cmd_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)cmd);
    return b;
}

lv_obj_t *avo_now_playing_create(lv_obj_t *parent, int32_t top)
{
    const avo_palette_t *p = avo_pal();
    np_t *np = lv_malloc_zeroed(sizeof *np);
    np->root = lv_obj_create(parent);
    lv_obj_remove_style_all(np->root);
    lv_obj_set_size(np->root, AVO_W, AVO_H - top);
    lv_obj_set_pos(np->root, 0, top);
    lv_obj_remove_flag(np->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(np->root, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* cover (artwork or generated) + track info on its right */
    np->cover = lv_obj_create(np->root);
    lv_obj_remove_style_all(np->cover);
    lv_obj_set_size(np->cover, 132, 132);
    lv_obj_set_pos(np->cover, AVO_PAD + 6, 0);
    lv_obj_set_style_radius(np->cover, 24, 0);
    lv_obj_set_style_clip_corner(np->cover, true, 0);
    lv_obj_set_style_bg_opa(np->cover, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(np->cover, LV_GRAD_DIR_VER, 0);
    lv_obj_remove_flag(np->cover, LV_OBJ_FLAG_CLICKABLE);
    np->cover_glyph = avo_label(np->cover, &avo_font_40, lv_color_white(), LV_SYMBOL_AUDIO);
    lv_obj_center(np->cover_glyph);
    np->cover_img = lv_image_create(np->cover);
    lv_obj_set_size(np->cover_img, 132, 132);
    lv_image_set_inner_align(np->cover_img, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_add_flag(np->cover_img, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *info = lv_obj_create(np->root);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, AVO_W - 2 * AVO_PAD - 132 - 20, 132);
    lv_obj_set_pos(info, AVO_PAD + 6 + 132 + 14, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(info, 4, 0);
    lv_obj_remove_flag(info, LV_OBJ_FLAG_CLICKABLE);
    np->source = avo_label(info, &avo_font_22, p->label2, "");
    np->title = avo_label(info, &avo_font_26, p->label, "");
    lv_obj_set_size(np->title, lv_pct(100), 62); /* two lines, then "…" */
    lv_label_set_long_mode(np->title, LV_LABEL_LONG_MODE_DOTS);
    np->artist = avo_label(info, &avo_font_22, p->label2, "");
    lv_obj_set_width(np->artist, lv_pct(100));
    lv_obj_set_style_max_height(np->artist, 56, 0);
    lv_label_set_long_mode(np->artist, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *row = lv_obj_create(np->root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, AVO_W - 2 * AVO_PAD, 116);
    lv_obj_set_pos(row, AVO_PAD, 150);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    round_btn(row, 84, LV_SYMBOL_PREV, AVO_AMS_CMD_PREV, false, &avo_font_30);
    np->play = round_btn(row, 108, LV_SYMBOL_PLAY, AVO_AMS_CMD_TOGGLE, true, &avo_font_40);
    round_btn(row, 84, LV_SYMBOL_NEXT, AVO_AMS_CMD_NEXT, false, &avo_font_30);

    lv_obj_t *vol = lv_obj_create(np->root);
    lv_obj_remove_style_all(vol);
    lv_obj_set_size(vol, AVO_W - 2 * AVO_PAD, 64);
    lv_obj_set_pos(vol, AVO_PAD, 280);
    lv_obj_set_flex_flow(vol, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vol, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    round_btn(vol, 56, LV_SYMBOL_MINUS, AVO_AMS_CMD_VOL_DOWN, false, &avo_font_26);
    np->vol_bar = lv_bar_create(vol);
    lv_obj_set_size(np->vol_bar, 200, 10);
    lv_obj_add_style(np->vol_bar, &avo_sty()->track, 0);
    lv_obj_add_style(np->vol_bar, &avo_sty()->accent_fill, LV_PART_INDICATOR);
    round_btn(vol, 56, LV_SYMBOL_PLUS, AVO_AMS_CMD_VOL_UP, false, &avo_font_26);

    refresh(np, true);
    np->timer = lv_timer_create(timer_cb, REFRESH_MS, np);
    lv_obj_add_event_cb(np->root, root_deleted_cb, LV_EVENT_DELETE, np);
    return np->root;
}
