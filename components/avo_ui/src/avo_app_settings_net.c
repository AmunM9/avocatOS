/* Ajustes > Bluetooth and Ajustes > Wi-Fi (scan, password entry, connect). */
#include <stdio.h>
#include <string.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define POLL_MS 500

/* ================================================================= Bluetooth + iPhone */

static struct {
    lv_timer_t *timer;
    lv_obj_t *state, *phone, *help;
    bool forget_armed;
} s_bt;

static const char *phone_text(void)
{
    switch (avo_hal_phone_state()) {
    case AVO_PHONE_OFF: return "Bluetooth apagado";
    case AVO_PHONE_PAIRING: return "Emparejando…";
    case AVO_PHONE_READY: return "Conectado";
    default: return avo_hal_phone_bonded() ? "Esperando al iPhone" : "Sin emparejar";
    }
}

static const char *bt_state_text(void)
{
    switch (avo_hal_bt_state()) {
    case AVO_LINK_OFF: return "Apagado";
    case AVO_LINK_BUSY: return "Visible";
    case AVO_LINK_CONNECTED: return "Conectado";
    case AVO_LINK_ERROR: return "Error";
    default: return "Activado";
    }
}

static void bt_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_label_set_text(s_bt.state, bt_state_text());
    lv_label_set_text(s_bt.phone, phone_text());
}

static void bt_sw_cb(lv_event_t *e)
{
    avo_settings()->bluetooth = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_hal_click();
    avo_settings_commit();
}

static void forget_cb(lv_event_t *e)
{
    lv_obj_t *label = lv_obj_get_child(lv_event_get_target(e), 1);
    if (!s_bt.forget_armed) {
        s_bt.forget_armed = true;
        lv_label_set_text(label, "Toca otra vez para olvidar");
        return;
    }
    s_bt.forget_armed = false;
    avo_hal_phone_forget();
    lv_label_set_text(label, "iPhone olvidado");
    avo_hal_click();
}

void avo_settings_bt_page(lv_obj_t *scr)
{
    memset(&s_bt, 0, sizeof s_bt);
    lv_obj_t *page = avo_subpage_create(scr, "Bluetooth", AVO_HUE_SKY);
    avo_row_switch(page, AVO_HUE_SKY, LV_SYMBOL_BLUETOOTH, "Bluetooth", avo_settings()->bluetooth, bt_sw_cb, NULL);
    avo_info_row(page, "Nombre", avo_hal_bt_name());
    s_bt.state = avo_info_row(page, "Estado", bt_state_text());

    avo_section(page, "iPhone");
    s_bt.phone = avo_info_row(page, "iPhone", phone_text());
    avo_note(page, "Para emparejar (solo la primera vez): instala «nRF Connect» o «LightBlue» en el iPhone, "
                   "busca «avocatOS», toca Conectar y acepta «Enlazar» y «Permitir notificaciones».");
    avo_note(page, "Desde iOS 18 el iPhone no muestra relojes de otras marcas en Ajustes › Bluetooth. "
                   "Tras emparejar, se reconecta solo y ya no necesitas la app.");
    avo_note(page, "Con el iPhone conectado verás sus notificaciones, podrás contestar llamadas, "
                   "controlar la música y la hora se sincroniza sola.");
    lv_obj_t *r = avo_row(page, AVO_HUE_EMBER, LV_SYMBOL_TRASH, "Olvidar iPhone", NULL);
    lv_obj_add_event_cb(r, forget_cb, LV_EVENT_CLICKED, NULL);
    avo_note(page, "Si olvidas el iPhone aquí, olvídalo también en el iPhone antes de volver a emparejar.");
    s_bt.timer = lv_timer_create(bt_timer_cb, POLL_MS, NULL);
}

void avo_settings_bt_leave(void)
{
    if (s_bt.timer) {
        lv_timer_delete(s_bt.timer);
    }
    memset(&s_bt, 0, sizeof s_bt);
}

/* ================================================================= Wi-Fi */

static struct {
    lv_timer_t *timer;
    lv_obj_t *screen, *status, *ip, *list, *scan_lbl;
    bool scanning;
    avo_wifi_ap_t aps[AVO_WIFI_MAX_RESULTS];
    int ap_count;
    /* password sheet */
    lv_obj_t *sheet, *ta;
    char pending_ssid[AVO_WIFI_SSID_MAX];
} w;

static const char *wifi_state_text(void)
{
    switch (avo_hal_wifi_state()) {
    case AVO_LINK_OFF: return "Apagado";
    case AVO_LINK_BUSY: return "Conectando…";
    case AVO_LINK_CONNECTED: return "Conectado";
    case AVO_LINK_ERROR: return "No se pudo conectar";
    default: return "Sin conexión";
    }
}

static void connect_to(const char *ssid, const char *pass)
{
    avo_settings_t *s = avo_settings();
    snprintf(s->wifi_ssid, sizeof s->wifi_ssid, "%s", ssid);
    snprintf(s->wifi_pass, sizeof s->wifi_pass, "%s", pass ? pass : "");
    avo_settings_commit();
    avo_hal_wifi_connect(s->wifi_ssid, s->wifi_pass);
}

/* ---------------------------------------------------------------- password sheet */

static void sheet_close(void)
{
    if (w.sheet) {
        lv_obj_delete(w.sheet);
        w.sheet = NULL;
        w.ta = NULL;
    }
    avo_nav_set_modal(false);
}

static void sheet_btn_cb(lv_event_t *e)
{
    bool connect = (bool)(intptr_t)lv_event_get_user_data(e);
    if (connect && w.ta) {
        connect_to(w.pending_ssid, lv_textarea_get_text(w.ta));
    }
    avo_hal_click();
    sheet_close();
}

static void kb_ready_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        connect_to(w.pending_ssid, lv_textarea_get_text(w.ta));
        sheet_close();
    } else if (code == LV_EVENT_CANCEL) {
        sheet_close();
    }
}

static lv_obj_t *pill_button(lv_obj_t *parent, const char *text, bool primary)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 150, 54);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(b, primary ? &avo_sty()->accent_fill : &avo_sty()->track, 0);
    lv_obj_set_style_transform_scale(b, 240, LV_STATE_PRESSED);
    lv_obj_t *l = avo_label(b, &avo_font_26, primary ? avo_pal()->on_accent : avo_pal()->label, text);
    lv_obj_center(l);
    return b;
}

static void open_password_sheet(const char *ssid)
{
    const avo_palette_t *p = avo_pal();
    snprintf(w.pending_ssid, sizeof w.pending_ssid, "%s", ssid);
    w.sheet = lv_obj_create(w.screen);
    lv_obj_remove_style_all(w.sheet);
    lv_obj_set_size(w.sheet, AVO_W, AVO_H);
    lv_obj_set_style_bg_color(w.sheet, p->bg, 0);
    lv_obj_set_style_bg_opa(w.sheet, LV_OPA_COVER, 0);
    lv_obj_remove_flag(w.sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(w.sheet, LV_OBJ_FLAG_CLICKABLE); /* block touches to the page below */

    lv_obj_t *title = avo_label(w.sheet, &avo_font_26, p->label2, "Contraseña para");
    lv_obj_set_pos(title, AVO_PAD + 14, AVO_TOP + 2);
    lv_obj_t *name = avo_label(w.sheet, &avo_font_30, p->label, ssid);
    lv_obj_set_width(name, AVO_W - 2 * AVO_PAD - 28);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(name, AVO_PAD + 14, AVO_TOP + 34);

    w.ta = lv_textarea_create(w.sheet);
    lv_obj_set_size(w.ta, AVO_W - 2 * AVO_PAD, 60);
    lv_obj_set_pos(w.ta, AVO_PAD, 88);
    lv_textarea_set_one_line(w.ta, true);
    lv_textarea_set_password_mode(w.ta, true);
    lv_textarea_set_max_length(w.ta, AVO_WIFI_PASS_MAX - 1);
    lv_obj_add_style(w.ta, &avo_sty()->row, 0);
    lv_obj_set_style_min_height(w.ta, 0, 0);
    lv_obj_set_style_text_font(w.ta, &avo_font_26, 0);
    lv_obj_set_style_text_color(w.ta, p->label, 0);
    lv_obj_set_style_border_width(w.ta, 0, 0);
    lv_obj_set_style_bg_color(w.ta, p->accent, LV_PART_CURSOR);

    lv_obj_t *btns = lv_obj_create(w.sheet);
    lv_obj_remove_style_all(btns);
    lv_obj_set_size(btns, AVO_W, 62);
    lv_obj_set_pos(btns, 0, 156);
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(pill_button(btns, "Cancelar", false), sheet_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)false);
    lv_obj_add_event_cb(pill_button(btns, "Conectar", true), sheet_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)true);

    lv_obj_t *kb = lv_keyboard_create(w.sheet);
    lv_obj_set_size(kb, AVO_W, AVO_H - 226);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, w.ta);
    lv_obj_set_style_bg_color(kb, p->bg, 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(kb, 6, 0);
    lv_obj_set_style_pad_gap(kb, 5, 0);
    lv_obj_set_style_bg_color(kb, p->surface, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, p->label, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &avo_font_22, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 12, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_ready_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_ready_cb, LV_EVENT_CANCEL, NULL);
    avo_nav_set_modal(true); /* typing: swipes must not leave the page */
}

/* ---------------------------------------------------------------- network list */

static void ap_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= w.ap_count) {
        return;
    }
    avo_hal_click();
    const avo_wifi_ap_t *ap = &w.aps[idx];
    if (ap->secure) {
        open_password_sheet(ap->ssid);
    } else {
        connect_to(ap->ssid, "");
    }
}

static const char *signal_text(int8_t rssi)
{
    if (rssi > -55) return "Excelente";
    if (rssi > -67) return "Buena";
    if (rssi > -78) return "Débil";
    return "Muy débil";
}

static void rebuild_list(void)
{
    lv_obj_clean(w.list);
    if (w.ap_count == 0) {
        avo_note(w.list, w.scanning ? "Buscando redes…" : "No se encontraron redes. Toca Buscar redes.");
        return;
    }
    for (int i = 0; i < w.ap_count; i++) {
        char val[32];
        snprintf(val, sizeof val, "%s%s", signal_text(w.aps[i].rssi), w.aps[i].secure ? "  " AVO_SYM_LOCK : "");
        lv_obj_t *r = avo_row(w.list, AVO_HUE_SKY, LV_SYMBOL_WIFI, w.aps[i].ssid, val);
        lv_obj_add_event_cb(r, ap_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void start_scan(void)
{
    if (!avo_settings()->wifi || w.scanning) {
        return;
    }
    w.scanning = avo_hal_wifi_scan_start();
    w.ap_count = 0;
    lv_label_set_text(w.scan_lbl, w.scanning ? "Buscando…" : "Buscar redes");
    rebuild_list();
}

static void scan_cb(lv_event_t *e)
{
    (void)e;
    avo_hal_click();
    start_scan();
}

static void wifi_poll_cb(lv_timer_t *t)
{
    (void)t;
    char ssid[AVO_WIFI_SSID_MAX], ip[16], buf[64];
    avo_hal_wifi_info(ssid, sizeof ssid, ip, sizeof ip);
    if (ssid[0]) {
        snprintf(buf, sizeof buf, "%s · %s", wifi_state_text(), ssid);
    } else {
        snprintf(buf, sizeof buf, "%s", wifi_state_text());
    }
    lv_label_set_text(w.status, buf);
    lv_label_set_text(w.ip, ip[0] ? ip : "—");
    if (w.scanning) {
        int n = avo_hal_wifi_scan_results(w.aps, AVO_WIFI_MAX_RESULTS);
        if (n >= 0) {
            w.scanning = false;
            w.ap_count = n;
            lv_label_set_text(w.scan_lbl, "Buscar redes");
            rebuild_list();
        }
    }
}

static void wifi_sw_cb(lv_event_t *e)
{
    avo_settings()->wifi = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_hal_click();
    avo_settings_commit();
    if (avo_settings()->wifi) {
        start_scan();
    } else {
        w.ap_count = 0;
        w.scanning = false;
        rebuild_list();
    }
}

void avo_settings_wifi_page(lv_obj_t *scr)
{
    lv_memzero(&w, sizeof w);
    w.screen = scr;
    lv_obj_t *page = avo_subpage_create(scr, "Wi-Fi", AVO_HUE_SKY);
    avo_row_switch(page, AVO_HUE_SKY, LV_SYMBOL_WIFI, "Wi-Fi", avo_settings()->wifi, wifi_sw_cb, NULL);
    if (avo_settings()->low_power) {
        avo_note(page, "En pausa por Ahorro de batería (Ajustes › Batería).");
    }
    w.status = avo_info_row(page, "Estado", wifi_state_text());
    w.ip = avo_info_row(page, "Dirección IP", "—");

    avo_section(page, "Redes");
    lv_obj_t *scan = avo_row(page, AVO_HUE_MINT, LV_SYMBOL_REFRESH, "Buscar redes", NULL);
    w.scan_lbl = lv_obj_get_child(scan, 1);
    lv_obj_add_event_cb(scan, scan_cb, LV_EVENT_CLICKED, NULL);

    w.list = lv_obj_create(page);
    lv_obj_remove_style_all(w.list);
    lv_obj_set_size(w.list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(w.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w.list, 8, 0);
    avo_note(page, "La contraseña se guarda en el reloj para reconectar al encender.");

    rebuild_list();
    start_scan();
    wifi_poll_cb(NULL);
    w.timer = lv_timer_create(wifi_poll_cb, POLL_MS, NULL);
}

void avo_settings_wifi_leave(void)
{
    if (w.timer) {
        lv_timer_delete(w.timer);
    }
    lv_memzero(&w, sizeof w);
}
