/* Notification Center: iPhone notifications received over ANCS. */
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"

#define REFRESH_MS 500
#define CARD_MSG_H 56 /* two lines of message in the list */

static struct {
    lv_obj_t *screen, *list, *sheet;
    lv_timer_t *timer;
    uint32_t version;
    avo_notif_t items[AVO_NOTIF_MAX];
    int count;
} nc;

avo_hue_t avo_notif_hue(uint8_t cat)
{
    switch (cat) {
    case AVO_ANCS_CAT_INCOMING_CALL:
    case AVO_ANCS_CAT_MISSED_CALL:
    case AVO_ANCS_CAT_VOICEMAIL: return AVO_HUE_MINT;
    case AVO_ANCS_CAT_SOCIAL: return AVO_HUE_LIME;
    case AVO_ANCS_CAT_SCHEDULE: return AVO_HUE_EMBER;
    case AVO_ANCS_CAT_EMAIL: return AVO_HUE_SKY;
    case AVO_ANCS_CAT_HEALTH: return AVO_HUE_ROSE;
    case AVO_ANCS_CAT_NEWS: return AVO_HUE_SOLAR;
    default: return AVO_HUE_IRIS;
    }
}

const char *avo_notif_symbol(uint8_t cat)
{
    switch (cat) {
    case AVO_ANCS_CAT_INCOMING_CALL:
    case AVO_ANCS_CAT_MISSED_CALL:
    case AVO_ANCS_CAT_VOICEMAIL: return LV_SYMBOL_CALL;
    case AVO_ANCS_CAT_SOCIAL: return LV_SYMBOL_ENVELOPE;
    case AVO_ANCS_CAT_SCHEDULE: return AVO_SYM_CLOCK;
    case AVO_ANCS_CAT_EMAIL: return LV_SYMBOL_ENVELOPE;
    case AVO_ANCS_CAT_HEALTH: return AVO_SYM_HEART;
    case AVO_ANCS_CAT_NEWS: return LV_SYMBOL_LIST;
    case AVO_ANCS_CAT_LOCATION: return LV_SYMBOL_GPS;
    case AVO_ANCS_CAT_ENTERTAINMENT: return LV_SYMBOL_AUDIO;
    default: return LV_SYMBOL_BELL;
    }
}

static const avo_notif_t *find(uint32_t uid)
{
    for (int i = 0; i < nc.count; i++) {
        if (nc.items[i].uid == uid) {
            return &nc.items[i];
        }
    }
    return NULL;
}

/* ================================================================= detail sheet */

static void sheet_close(void)
{
    if (nc.sheet) {
        lv_obj_delete(nc.sheet);
        nc.sheet = NULL;
    }
    avo_nav_set_modal(false);
    avo_nav_set_scroller(nc.list);
}

static void sheet_action_cb(lv_event_t *e)
{
    intptr_t act = (intptr_t)lv_event_get_user_data(e);
    uint32_t uid = (uint32_t)(uintptr_t)lv_obj_get_user_data(nc.sheet);
    if (act == 0) {
        avo_hal_notif_action(uid, false);   /* clear / decline, also on the iPhone */
    } else if (act == 1) {
        avo_hal_notif_action(uid, true);    /* accept call */
    }
    avo_hal_click();
    sheet_close();
}

static lv_obj_t *pill(lv_obj_t *parent, const char *text, lv_color_t fill, lv_color_t fg, intptr_t act)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, lv_pct(100), 64);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, fill, 0);
    lv_obj_set_style_transform_scale(b, 244, LV_STATE_PRESSED);
    lv_obj_t *l = avo_label(b, &avo_font_26, fg, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, sheet_action_cb, LV_EVENT_CLICKED, (void *)act);
    return b;
}

static void open_sheet(const avo_notif_t *n)
{
    const avo_palette_t *p = avo_pal();
    sheet_close();
    nc.sheet = lv_obj_create(nc.screen);
    lv_obj_remove_style_all(nc.sheet);
    lv_obj_set_size(nc.sheet, AVO_W, AVO_H);
    lv_obj_set_style_bg_color(nc.sheet, p->bg, 0);
    lv_obj_set_style_bg_opa(nc.sheet, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(nc.sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(nc.sheet, AVO_PAD + 4, 0);
    lv_obj_set_style_pad_top(nc.sheet, AVO_TOP + 6, 0);
    lv_obj_set_style_pad_bottom(nc.sheet, 40, 0);
    lv_obj_set_style_pad_row(nc.sheet, 12, 0);
    lv_obj_set_scrollbar_mode(nc.sheet, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_user_data(nc.sheet, (void *)(uintptr_t)n->uid);

    lv_obj_t *head = lv_obj_create(nc.sheet);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 12, 0);
    avo_app_icon(head, avo_notif_hue(n->category), avo_notif_symbol(n->category), 48);
    avo_label(head, &avo_font_26, p->label2, n->app);

    lv_obj_t *t = avo_label(nc.sheet, &avo_font_30, p->label, n->title[0] ? n->title : n->app);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *m = avo_label(nc.sheet, &avo_font_26, p->label, n->message);
    lv_obj_set_width(m, lv_pct(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_WRAP);

    if (n->category == AVO_ANCS_CAT_INCOMING_CALL) {
        pill(nc.sheet, "Aceptar", lv_color_hex(0x30D158), lv_color_white(), 1);
        pill(nc.sheet, "Rechazar", avo_hue(AVO_HUE_EMBER), lv_color_white(), 0);
    } else {
        pill(nc.sheet, "Descartar", p->surface, p->label, 0);
    }
    pill(nc.sheet, "Cerrar", p->bg, p->label2, 2);
    avo_nav_set_modal(true);
}

void avo_notif_open_detail(uint32_t uid)
{
    const avo_notif_t *n = find(uid);
    if (n && nc.screen) {
        open_sheet(n);
    }
}

/* ================================================================= list */

static void card_click_cb(lv_event_t *e)
{
    const avo_notif_t *n = find((uint32_t)(uintptr_t)lv_event_get_user_data(e));
    if (n) {
        avo_hal_click();
        open_sheet(n);
    }
}

static void clear_all_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < nc.count; i++) {
        avo_hal_notif_action(nc.items[i].uid, false);
    }
    avo_hal_click();
}

static void add_card(const avo_notif_t *n)
{
    const avo_palette_t *p = avo_pal();
    lv_obj_t *card = avo_glass(nc.list);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(card, p->glass_opa + 20, LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)n->uid);

    lv_obj_t *head = lv_obj_create(card);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 10, 0);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ic = avo_app_icon(head, avo_notif_hue(n->category), avo_notif_symbol(n->category), 36);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *app = avo_label(head, &avo_font_22, p->label2, n->app);
    lv_obj_set_flex_grow(app, 1);
    char hm[12];
    avo_fmt_hm(hm, sizeof hm, &n->when, avo_settings()->h24);
    avo_label(head, &avo_font_22, p->label2, hm);

    lv_obj_t *t = avo_label(card, &avo_font_26, p->label, n->title[0] ? n->title : n->app);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    if (n->message[0]) {
        lv_obj_t *m = avo_label(card, &avo_font_22, p->label, n->message);
        lv_obj_set_width(m, lv_pct(100));
        lv_obj_set_height(m, CARD_MSG_H);
        lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_DOTS);
    }
}

static void add_empty_state(void)
{
    const avo_palette_t *p = avo_pal();
    bool paired = avo_hal_phone_state() == AVO_PHONE_READY;
    lv_obj_t *box = lv_obj_create(nc.list);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 14, 0);
    lv_obj_set_style_pad_top(box, 60, 0);
    lv_obj_t *ic = avo_app_icon(box, AVO_HUE_EMBER, LV_SYMBOL_BELL, 96);
    lv_obj_remove_flag(ic, LV_OBJ_FLAG_CLICKABLE);
    avo_label(box, &avo_font_30, p->label, "Todo al día");
    lv_obj_t *hint = avo_label(box, &avo_font_22, p->label2,
                               paired ? "Las notificaciones de tu iPhone aparecerán aquí."
                                      : "Empareja tu iPhone en Ajustes › Bluetooth para ver sus avisos.");
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
}

static void rebuild(void)
{
    nc.version = avo_hal_notif_version();
    nc.count = avo_hal_notif_list(nc.items, AVO_NOTIF_MAX);
    lv_obj_clean(nc.list);
    if (nc.count == 0) {
        add_empty_state();
        return;
    }
    for (int i = 0; i < nc.count; i++) {
        add_card(&nc.items[i]);
    }
    lv_obj_t *clear = avo_row(nc.list, AVO_HUE_EMBER, LV_SYMBOL_TRASH, "Borrar todo", NULL);
    lv_obj_add_event_cb(clear, clear_all_cb, LV_EVENT_CLICKED, NULL);
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    if (avo_hal_notif_version() != nc.version) {
        uint32_t open_uid = nc.sheet ? (uint32_t)(uintptr_t)lv_obj_get_user_data(nc.sheet) : 0;
        rebuild();
        if (nc.sheet && !find(open_uid)) {
            sheet_close(); /* cleared on the iPhone meanwhile */
        }
    }
}

static void screen_deleted_cb(lv_event_t *e)
{
    (void)e;
    if (nc.timer) {
        lv_timer_delete(nc.timer);
    }
    memset(&nc, 0, sizeof nc);
}

void avo_notif_build(lv_obj_t *screen)
{
    memset(&nc, 0, sizeof nc);
    nc.screen = screen;
    lv_obj_t *t = avo_label(screen, &avo_font_30, avo_pal()->label, "Notificaciones");
    lv_obj_set_pos(t, AVO_PAD + 14, AVO_TOP + 6);
    avo_header_clock(screen);

    nc.list = lv_obj_create(screen);
    lv_obj_remove_style_all(nc.list);
    lv_obj_set_size(nc.list, AVO_W, AVO_H - 64);
    lv_obj_set_pos(nc.list, 0, 64);
    lv_obj_set_flex_flow(nc.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(nc.list, AVO_PAD - 4, 0);
    lv_obj_set_style_pad_row(nc.list, 10, 0);
    lv_obj_set_style_pad_bottom(nc.list, 60, 0);
    lv_obj_set_scrollbar_mode(nc.list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(nc.list, LV_DIR_VER);
    avo_nav_set_scroller(nc.list);

    rebuild();
    nc.timer = lv_timer_create(refresh_cb, REFRESH_MS, NULL);
    lv_obj_add_event_cb(screen, screen_deleted_cb, LV_EVENT_DELETE, NULL);
}
