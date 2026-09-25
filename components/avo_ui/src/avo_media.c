/*
 * Now Playing: shared by the Música app and the page under the Control
 * Center. Controls the iPhone's player through AMS. AMS carries no artwork:
 * the cover is the album art fetched over Wi-Fi. Without one (covers off in
 * Ajustes, no Wi-Fi, not found) the page shows no placeholder: the text
 * takes the full width. Source, title and artist are one line each; a title
 * or artist that does not fit scrolls, like the iPhone's player, so the
 * layout never jumps.
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

#define COVER 132
#define INFO_H COVER            /* the text block keeps the cover's height */
#define CONTROLS_Y 150

typedef enum { LAYOUT_NONE, LAYOUT_COVER, LAYOUT_TEXT, LAYOUT_MESSAGE } layout_t;

typedef struct {
    lv_obj_t *root, *cover, *cover_img, *info, *title, *artist, *source, *play, *vol_bar;
    lv_timer_t *timer;
    uint32_t version, art_version;
    bool had_art;
    layout_t layout;
    lv_image_dsc_t art;
} np_t;

#define MARQUEE_PX_PER_S 38
#define MARQUEE_START_MS 1500   /* read the beginning first               */
#define MARQUEE_PAUSE_MS 2000   /* rest at the start between loops         */

/* Scrolling of long titles: still at first, then a slow loop, then a rest,
 * like the iPhone's Now Playing. */
static void marquee(lv_obj_t *label)
{
    static lv_anim_t tpl;
    static bool ready;
    if (!ready) {
        lv_anim_init(&tpl);
        tpl.act_time = -MARQUEE_START_MS; /* negative act_time = initial delay */
        tpl.repeat_cnt = LV_ANIM_REPEAT_INFINITE;
        tpl.repeat_delay = MARQUEE_PAUSE_MS;
        ready = true;
    }
    lv_obj_set_style_anim(label, &tpl, 0);
    lv_obj_set_style_anim_duration(label, lv_anim_speed(MARQUEE_PX_PER_S), 0);
}

/* Set text only when it changed: re-setting restarts the scrolling. */
static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

static void one_line(lv_obj_t *label, lv_label_long_mode_t mode, lv_text_align_t align)
{
    lv_label_set_long_mode(label, mode);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(label, LV_COORD_MAX, 0);
    lv_obj_set_style_text_align(label, align, 0);
    if (mode == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR) {
        marquee(label);
    }
}

static void apply_layout(np_t *np, layout_t layout)
{
    if (layout == np->layout) {
        return;
    }
    np->layout = layout;
    bool cover = layout == LAYOUT_COVER;
    int32_t x = cover ? AVO_PAD + 6 + COVER + 14 : AVO_PAD + 6;
    lv_obj_set_pos(np->info, x, 0);
    lv_obj_set_size(np->info, AVO_W - x - AVO_PAD - 6, INFO_H);
    if (cover) {
        lv_obj_remove_flag(np->cover, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(np->cover, LV_OBJ_FLAG_HIDDEN);
    }
    lv_text_align_t align = cover ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
    lv_obj_set_style_text_font(np->title, cover ? &avo_font_26 : &avo_font_30, 0);
    one_line(np->source, LV_LABEL_LONG_MODE_DOTS, align);
    one_line(np->title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR, align);
    if (layout == LAYOUT_MESSAGE) {
        /* "Sin iPhone": a sentence reads better wrapped than scrolling */
        one_line(np->artist, LV_LABEL_LONG_MODE_DOTS, align);
        lv_obj_set_style_max_height(np->artist, 84, 0);
    } else {
        one_line(np->artist, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR, align);
    }
}

static bool show_cover(const avo_artwork_t *art, bool has_art)
{
    return has_art && art->pixels && avo_settings()->artwork;
}

static void refresh(np_t *np, bool force)
{
    avo_media_t m;
    avo_hal_media(&m);
    avo_artwork_t art;
    bool has_art = avo_hal_artwork(&art);
    has_art = show_cover(&art, has_art);
    if (!force && m.version == np->version && has_art == np->had_art && (!has_art || art.version == np->art_version)) {
        return;
    }
    np->version = m.version;
    np->had_art = has_art;
    if (!m.available) {
        apply_layout(np, LAYOUT_MESSAGE);
        set_text(np->source, "");
        set_text(np->title, "Sin iPhone");
        set_text(np->artist, "Empareja tu iPhone en Ajustes › Bluetooth para controlar su música.");
        return;
    }
    apply_layout(np, has_art ? LAYOUT_COVER : LAYOUT_TEXT);
    set_text(np->title, m.title[0] ? m.title : "Nada sonando");
    set_text(np->artist, m.artist);
    set_text(np->source, m.app[0] ? m.app : "iPhone");
    set_text(lv_obj_get_child(np->play, 0), m.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
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

    /* cover (album art only; hidden when there is none) */
    np->cover = lv_obj_create(np->root);
    lv_obj_remove_style_all(np->cover);
    lv_obj_set_size(np->cover, COVER, COVER);
    lv_obj_set_pos(np->cover, AVO_PAD + 6, 0);
    lv_obj_set_style_radius(np->cover, 24, 0);
    lv_obj_set_style_clip_corner(np->cover, true, 0);
    lv_obj_set_style_bg_opa(np->cover, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(np->cover, p->surface, 0);
    lv_obj_remove_flag(np->cover, LV_OBJ_FLAG_CLICKABLE);
    np->cover_img = lv_image_create(np->cover);
    lv_obj_set_size(np->cover_img, COVER, COVER);
    lv_image_set_inner_align(np->cover_img, LV_IMAGE_ALIGN_STRETCH);

    /* track info: three single lines, vertically centred on the cover */
    np->info = lv_obj_create(np->root);
    lv_obj_remove_style_all(np->info);
    lv_obj_set_flex_flow(np->info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(np->info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(np->info, 6, 0);
    lv_obj_remove_flag(np->info, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    np->source = avo_label(np->info, &avo_font_22, p->label2, "");
    np->title = avo_label(np->info, &avo_font_26, p->label, "");
    np->artist = avo_label(np->info, &avo_font_22, p->label2, "");

    lv_obj_t *row = lv_obj_create(np->root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, AVO_W - 2 * AVO_PAD, 116);
    lv_obj_set_pos(row, AVO_PAD, CONTROLS_Y);
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
