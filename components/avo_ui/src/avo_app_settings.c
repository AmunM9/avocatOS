/* Ajustes: the system settings app (main list + display, appearance, time,
 * information and developer pages). Network pages live in
 * avo_app_settings_net.c. */
#include <math.h>
#include <stdio.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define TIMEOUT_STEPS_N 6
static const uint16_t TIMEOUT_STEPS[TIMEOUT_STEPS_N] = { 5, 10, 15, 30, 60, 120 };
#define UTC_STEP_MIN 30
#define INFO_REFRESH_MS 2000

/* ================================================================= shared */

static void back_cb(lv_event_t *e)
{
    (void)e;
    avo_nav_back();
}

lv_obj_t *avo_subpage_create(lv_obj_t *screen, const char *title, avo_hue_t hue)
{
    avo_header_clock(screen);
    char buf[48];
    snprintf(buf, sizeof buf, LV_SYMBOL_LEFT "  %s", title);
    lv_obj_t *page = avo_page_create(screen, buf, hue);
    lv_obj_t *t = lv_obj_get_child(screen, lv_obj_get_index(page) - 1);
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(t, 16);
    lv_obj_add_event_cb(t, back_cb, LV_EVENT_CLICKED, NULL);
    return page;
}

lv_obj_t *avo_info_row(lv_obj_t *parent, const char *key, const char *value)
{
    lv_obj_t *r = avo_row(parent, AVO_HUE_GRAPHITE, NULL, key, value);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_min_height(r, 64, 0);
    return lv_obj_get_child(r, -1);
}

lv_obj_t *avo_note(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = avo_label(parent, &avo_font_22, avo_pal()->label2, text);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_hor(l, 14, 0);
    return l;
}

static void commit_switch(lv_event_t *e, bool *field)
{
    *field = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_hal_click();
    avo_settings_commit();
}

/* ================================================================= Pantalla */

static void aod_sw_cb(lv_event_t *e) { commit_switch(e, &avo_settings()->aod); }
static void raise_sw_cb(lv_event_t *e) { commit_switch(e, &avo_settings()->raise_to_wake); }

static void display_bright_cb(lv_event_t *e)
{
    avo_settings()->brightness = (uint8_t)lv_slider_get_value(lv_event_get_target(e));
    avo_hal_display_brightness(avo_settings()->brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        avo_settings_commit();
    }
}

static void timeout_text(char *buf, size_t len)
{
    snprintf(buf, len, "%u s", (unsigned)avo_settings()->screen_timeout_s);
}

static void timeout_cb(lv_event_t *e)
{
    avo_settings_t *s = avo_settings();
    int next = 0;
    for (int i = 0; i < TIMEOUT_STEPS_N; i++) {
        if (TIMEOUT_STEPS[i] == s->screen_timeout_s) {
            next = (i + 1) % TIMEOUT_STEPS_N;
        }
    }
    s->screen_timeout_s = TIMEOUT_STEPS[next];
    avo_settings_commit();
    char buf[16];
    timeout_text(buf, sizeof buf);
    lv_label_set_text(lv_obj_get_child(lv_event_get_target(e), -1), buf);
    avo_hal_click();
}

static void page_display(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, "Pantalla", AVO_HUE_SKY);
    lv_obj_t *card = lv_obj_create(page);
    lv_obj_remove_style_all(card);
    lv_obj_add_style(card, &avo_sty()->row, 0);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 16, 0);
    lv_obj_set_style_pad_ver(card, 18, 0);
    avo_label(card, &avo_font_26, avo_pal()->label, "Brillo");
    lv_obj_t *sl = avo_slider(card, 5, 100, avo_settings()->brightness);
    lv_obj_add_event_cb(sl, display_bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, display_bright_cb, LV_EVENT_RELEASED, NULL);

    avo_row_switch(page, AVO_HUE_SOLAR, AVO_SYM_SUN, "Siempre activa", avo_settings()->aod, aod_sw_cb, NULL);
    avo_note(page, "Con la muñeca abajo muestra una esfera tenue que cambia de posición cada minuto.");
    avo_row_switch(page, AVO_HUE_MINT, AVO_SYM_WALK, "Levantar para activar", avo_settings()->raise_to_wake, raise_sw_cb, NULL);
    char buf[16];
    timeout_text(buf, sizeof buf);
    lv_obj_t *r = avo_row(page, AVO_HUE_IRIS, AVO_SYM_HOURGLASS, "Apagar tras", buf);
    lv_obj_add_event_cb(r, timeout_cb, LV_EVENT_CLICKED, NULL);
}

/* ================================================================= Apariencia */

static void avocado_sw_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_settings()->theme = on ? AVO_THEME_AVOCADO : AVO_THEME_CLEAN;
    avo_hal_click();
    avo_settings_commit(); /* rebuilds this page with the new theme */
}

static void open_faces_cb(lv_event_t *e)
{
    (void)e;
    avo_nav_app(&AVO_APP_FACES);
}

static void page_appearance(lv_obj_t *scr)
{
    const avo_palette_t *p = avo_pal();
    bool avo = avo_theme_is_avocado();
    lv_obj_t *page = avo_subpage_create(scr, "Apariencia", AVO_HUE_LIME);

    lv_obj_t *hero = avo_glass(page);
    lv_obj_set_size(hero, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, 16, 0);
    lv_obj_t *img = lv_image_create(hero);
    lv_image_set_src(img, &avo_img_mark_56);
    if (!avo) {
        lv_obj_set_style_image_opa(img, LV_OPA_50, 0);
    }
    lv_obj_t *txt = lv_obj_create(hero);
    lv_obj_remove_style_all(txt);
    lv_obj_set_flex_grow(txt, 1);
    lv_obj_set_height(txt, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    avo_label(txt, &avo_font_30, avo ? p->accent : p->label, "Modo Avocado");
    lv_obj_t *d = avo_label(txt, &avo_font_22, p->label2,
                            avo ? "Iconos de aguacate, verdes y la esfera Hass." : "Diseño limpio. Actívalo para añadir guiños de aguacate.");
    lv_obj_set_width(d, lv_pct(100));
    lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);

    avo_row_switch(page, AVO_HUE_LIME, AVO_SYM_LEAF, "Modo Avocado", avo, avocado_sw_cb, NULL);
    lv_obj_t *r = avo_row(page, AVO_HUE_IRIS, LV_SYMBOL_IMAGE, "Esferas", avo_face_name(avo_settings()->face));
    lv_obj_add_event_cb(r, open_faces_cb, LV_EVENT_CLICKED, NULL);
}

/* ================================================================= Hora */

static lv_obj_t *s_utc_lbl;

static void h24_sw_cb(lv_event_t *e) { commit_switch(e, &avo_settings()->h24); }

static void utc_text(char *buf, size_t len)
{
    int m = avo_settings()->utc_offset_min;
    char sign = m < 0 ? '-' : '+';
    m = m < 0 ? -m : m;
    if (m % 60) {
        snprintf(buf, len, "UTC%c%d:%02d", sign, m / 60, m % 60);
    } else {
        snprintf(buf, len, "UTC%c%d", sign, m / 60);
    }
}

static void utc_step_cb(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    avo_settings()->utc_offset_min = (int16_t)(avo_settings()->utc_offset_min + step);
    avo_settings_commit();
    char buf[16];
    utc_text(buf, sizeof buf);
    lv_label_set_text(s_utc_lbl, buf);
    avo_hal_click();
}

static lv_obj_t *round_button(lv_obj_t *parent, const char *sym)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 56, 56);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(b, &avo_sty()->track, 0);
    lv_obj_add_style(b, &avo_sty()->accent_fill, LV_STATE_PRESSED);
    lv_obj_t *l = avo_label(b, &avo_font_30, avo_pal()->label, sym);
    lv_obj_center(l);
    return b;
}

static void time_source_text(char *buf, size_t len)
{
    uint32_t ago = avo_hal_time_since_sync_s();
    const char *src = "Reloj interno";
    switch (avo_hal_time_source()) {
    case AVO_TIME_SRC_PHONE: src = "iPhone"; break;
    case AVO_TIME_SRC_NTP: src = "Internet (Wi-Fi)"; break;
    default: break;
    }
    if (ago == UINT32_MAX) {
        snprintf(buf, len, "%s", src);
    } else if (ago < 60) {
        snprintf(buf, len, "%s · ahora", src);
    } else {
        snprintf(buf, len, "%s · hace %u min", src, (unsigned)(ago / 60));
    }
}

static void page_time(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, "Hora", AVO_HUE_SOLAR);
    avo_row_switch(page, AVO_HUE_SOLAR, AVO_SYM_CLOCK, "Formato 24 horas", avo_settings()->h24, h24_sw_cb, NULL);

    avo_section(page, "Zona horaria");
    lv_obj_t *row = lv_obj_create(page);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &avo_sty()->row, 0);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *minus = round_button(row, LV_SYMBOL_MINUS);
    char buf[16];
    utc_text(buf, sizeof buf);
    s_utc_lbl = avo_label(row, &avo_font_30, avo_pal()->label, buf);
    lv_obj_t *plus = round_button(row, LV_SYMBOL_PLUS);
    lv_obj_add_event_cb(minus, utc_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-UTC_STEP_MIN);
    lv_obj_add_event_cb(plus, utc_step_cb, LV_EVENT_CLICKED, (void *)(intptr_t)UTC_STEP_MIN);

    avo_section(page, "Sincronización");
    char src[48];
    time_source_text(src, sizeof src);
    avo_info_row(page, "Origen", src);
    avo_note(page, "La hora se toma del iPhone cuando está conectado por Bluetooth, o de internet (NTP) "
                   "cuando hay Wi-Fi. Con el iPhone, la zona horaria también se ajusta sola.");
}

/* ================================================================= Información */

static struct {
    lv_timer_t *timer;
    lv_obj_t *batt, *volt, *power, *heap, *psram, *temp, *uptime;
} s_info;

static void info_refresh(void)
{
    char buf[40];
    avo_battery_t b;
    avo_hal_battery(&b);
    snprintf(buf, sizeof buf, b.percent >= 0 ? "%d %%" : "—", b.percent);
    lv_label_set_text(s_info.batt, buf);
    snprintf(buf, sizeof buf, b.millivolts > 0 ? "%.2f V" : "—", b.millivolts / 1000.0);
    lv_label_set_text(s_info.volt, buf);
    lv_label_set_text(s_info.power, b.charging ? "Cargando" : b.usb ? "USB" : b.present ? "Batería" : "—");

    avo_sysinfo_t si;
    avo_hal_sysinfo(&si);
    snprintf(buf, sizeof buf, "%u KB", (unsigned)(si.heap_internal_free / 1024));
    lv_label_set_text(s_info.heap, buf);
    snprintf(buf, sizeof buf, "%.1f MB", si.heap_psram_free / 1048576.0);
    lv_label_set_text(s_info.psram, buf);
    if (isnan(si.cpu_temp_c)) {
        snprintf(buf, sizeof buf, "—");
    } else {
        snprintf(buf, sizeof buf, "%.0f °C", si.cpu_temp_c);
    }
    lv_label_set_text(s_info.temp, buf);
    unsigned up = (unsigned)si.uptime_s;
    snprintf(buf, sizeof buf, "%uh %02um %02us", up / 3600, (up / 60) % 60, up % 60);
    lv_label_set_text(s_info.uptime, buf);
}

static void info_timer_cb(lv_timer_t *t)
{
    (void)t;
    info_refresh();
}

static void info_leave(void)
{
    if (s_info.timer) {
        lv_timer_delete(s_info.timer);
    }
    lv_memzero(&s_info, sizeof s_info);
}

static void page_info(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, "Información", AVO_HUE_GRAPHITE);
    avo_sysinfo_t si;
    avo_hal_sysinfo(&si);
    char buf[40];

    avo_section(page, "Batería");
    s_info.batt = avo_info_row(page, "Nivel", "");
    s_info.volt = avo_info_row(page, "Voltaje", "");
    s_info.power = avo_info_row(page, "Fuente", "");

    avo_section(page, "Reloj");
    char ver[40];
    snprintf(ver, sizeof ver, "avocatOS %s", avo_hal_fw_version());
    avo_info_row(page, "Sistema", ver);
    avo_info_row(page, "Modelo", "AMOLED 2.06");
    avo_info_row(page, "Chip", si.chip);
    avo_info_row(page, "MAC", si.mac);
    snprintf(buf, sizeof buf, "%u MB", (unsigned)si.flash_mb);
    avo_info_row(page, "Flash", buf);
    snprintf(buf, sizeof buf, "%u MB", (unsigned)si.psram_mb);
    avo_info_row(page, "PSRAM", buf);
    avo_info_row(page, "ESP-IDF", si.idf_version);

    avo_section(page, "En vivo");
    s_info.heap = avo_info_row(page, "RAM libre", "");
    s_info.psram = avo_info_row(page, "PSRAM libre", "");
    s_info.temp = avo_info_row(page, "Temperatura", "");
    s_info.uptime = avo_info_row(page, "Encendido", "");
    info_refresh();
    s_info.timer = lv_timer_create(info_timer_cb, INFO_REFRESH_MS, NULL);
}

/* ================================================================= Desarrollador */

static void fps_sw_cb(lv_event_t *e) { commit_switch(e, &avo_settings()->show_fps); }

static bool s_restart_armed;

static void restart_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_target(e);
    if (!s_restart_armed) {
        s_restart_armed = true;
        lv_label_set_text(lv_obj_get_child(row, 1), "Toca otra vez para reiniciar");
        return;
    }
    avo_hal_restart();
}

static void page_developer(lv_obj_t *scr)
{
    s_restart_armed = false;
    lv_obj_t *page = avo_subpage_create(scr, "Desarrollador", AVO_HUE_IRIS);
    avo_row_switch(page, AVO_HUE_IRIS, AVO_SYM_CHIP, "FPS y CPU", avo_settings()->show_fps, fps_sw_cb, NULL);
    avo_note(page, "Muestra fotogramas por segundo y uso de CPU en la esquina. Úsalo para medir fluidez.");
    lv_obj_t *r = avo_row(page, AVO_HUE_EMBER, LV_SYMBOL_REFRESH, "Reiniciar", NULL);
    lv_obj_add_event_cb(r, restart_cb, LV_EVENT_CLICKED, NULL);
}

/* ================================================================= main list */

static void push_cb(lv_event_t *e)
{
    void (*build)(lv_obj_t *) = (void (*)(lv_obj_t *))lv_event_get_user_data(e);
    void (*leave)(void) = NULL;
    if (build == page_info) leave = info_leave;
    if (build == avo_settings_wifi_page) leave = avo_settings_wifi_leave;
    if (build == avo_settings_bt_page) leave = avo_settings_bt_leave;
    if (build == avo_settings_gestures_page) leave = avo_settings_gestures_leave;
    if (build == avo_settings_transfer_page) leave = avo_settings_transfer_leave;
    avo_hal_click();
    avo_nav_push(build, leave);
}

static lv_obj_t *nav_row(lv_obj_t *page, avo_hue_t hue, const char *sym, const char *text, const char *value,
                         void (*build)(lv_obj_t *))
{
    lv_obj_t *r = avo_row(page, hue, sym, text, value);
    lv_obj_add_event_cb(r, push_cb, LV_EVENT_CLICKED, (void *)build);
    return r;
}

static void settings_build(lv_obj_t *scr)
{
    const avo_palette_t *p = avo_pal();
    avo_header_clock(scr);
    lv_obj_t *page = avo_page_create(scr, "Ajustes", AVO_HUE_GRAPHITE);

    lv_obj_t *hero = avo_glass(page);
    lv_obj_set_size(hero, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hero, 16, 0);
    lv_obj_add_flag(hero, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(hero, push_cb, LV_EVENT_CLICKED, (void *)page_info);
    lv_obj_t *img = lv_image_create(hero);
    lv_image_set_src(img, &avo_img_mark_56);
    lv_obj_t *txt = lv_obj_create(hero);
    lv_obj_remove_style_all(txt);
    lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(txt, LV_OBJ_FLAG_CLICKABLE);
    avo_label(txt, &avo_font_30, p->label, "avocatOS");
    char ver[32];
    snprintf(ver, sizeof ver, "Versión %s", avo_hal_fw_version());
    avo_label(txt, &avo_font_22, p->label2, ver);

    char wifi[AVO_WIFI_SSID_MAX], ip[16];
    avo_hal_wifi_info(wifi, sizeof wifi, ip, sizeof ip);
    const char *bt = !avo_settings()->bluetooth ? "No"
                   : avo_hal_phone_state() == AVO_PHONE_READY ? "iPhone"
                   : "Activado";
    avo_section(page, "Conexiones");
    nav_row(page, AVO_HUE_SKY, LV_SYMBOL_BLUETOOTH, "Bluetooth", bt, avo_settings_bt_page);
    nav_row(page, AVO_HUE_SKY, LV_SYMBOL_WIFI, "Wi-Fi", avo_settings()->wifi ? (wifi[0] ? wifi : "Activado") : "No",
            avo_settings_wifi_page);

    avo_section(page, "Reloj");
    nav_row(page, AVO_HUE_SKY, AVO_SYM_SUN, "Pantalla", NULL, page_display);
    nav_row(page, AVO_HUE_LIME, AVO_SYM_BRUSH, "Apariencia", avo_theme_is_avocado() ? "Avocado" : "Limpio", page_appearance);
    nav_row(page, AVO_HUE_SOLAR, AVO_SYM_CLOCK, "Hora", NULL, page_time);
    char vol[12] = "No";
    if (avo_settings()->sounds) {
        snprintf(vol, sizeof vol, "%u %%", avo_settings()->volume);
    }
    nav_row(page, AVO_HUE_EMBER, LV_SYMBOL_BELL, "Sonido", vol, avo_settings_sound_page);
    nav_row(page, AVO_HUE_ROSE, LV_SYMBOL_AUDIO, "Música", avo_settings()->artwork ? "Portadas" : "Sin portadas",
            avo_settings_music_page);
    nav_row(page, AVO_HUE_MINT, AVO_SYM_TAP, "Gestos", NULL, avo_settings_gestures_page);

    avo_section(page, "General");
    nav_row(page, AVO_HUE_MINT, AVO_SYM_MOBILE, "Enviar al reloj", NULL, avo_settings_transfer_page);
    nav_row(page, AVO_HUE_GRAPHITE, AVO_SYM_INFO, "Información", NULL, page_info);
    nav_row(page, AVO_HUE_IRIS, AVO_SYM_CHIP, "Desarrollador", NULL, page_developer);
}

static void settings_leave(void)
{
    info_leave();
}

const avo_app_t AVO_APP_SETTINGS = {
    .name = "Ajustes",
    .symbol = LV_SYMBOL_SETTINGS,
    .hue = AVO_HUE_GRAPHITE,
    .build = settings_build,
    .leave = settings_leave,
};
