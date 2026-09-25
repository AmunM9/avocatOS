/* avo_hal for the desktop simulator: realistic fake data, no hardware. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "avo_hal.h"
#include "sim.h"

#define SIM_SETTINGS_FILE "avocatos_sim_settings.bin"
#define SCAN_DELAY_MS 1200
#define CONNECT_DELAY_MS 900

static int16_t s_utc_offset = -300;
static bool s_bt_on, s_wifi_on;
static uint32_t s_bt_since, s_scan_at, s_connect_at;
static bool s_scanning, s_connecting, s_connected;
static char s_ssid[AVO_WIFI_SSID_MAX];
static uint8_t s_brightness = 80;
static bool s_phone_ready, s_charger;
static avo_notif_t s_notifs[AVO_NOTIF_MAX];
static int s_notif_count;
static uint32_t s_notif_version;
static avo_notif_t s_alert;
static bool s_alert_pending;
static avo_media_t s_media;

uint32_t avo_hal_millis(void) { return sim_millis(); }

void avo_hal_time_now(avo_time_t *out)
{
    time_t t = sim_fixed_time() ? sim_fixed_time() : time(NULL);
    t += (time_t)s_utc_offset * 60 + (time_t)(sim_fixed_time() ? sim_millis() / 1000 : 0);
    struct tm tm;
    gmtime_r(&t, &tm);
    *out = (avo_time_t){ tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_wday, tm.tm_hour, tm.tm_min, tm.tm_sec };
}

void avo_hal_time_set_utc_offset(int16_t m) { s_utc_offset = m; }
avo_time_src_t avo_hal_time_source(void) { return s_phone_ready ? AVO_TIME_SRC_PHONE : s_connected ? AVO_TIME_SRC_NTP : AVO_TIME_SRC_INTERNAL; }
uint32_t avo_hal_time_since_sync_s(void) { return (s_phone_ready || s_connected) ? 120 : UINT32_MAX; }
bool avo_hal_time_phone_offset(int16_t *m) { if (s_phone_ready) *m = -300; return s_phone_ready; }

void avo_hal_display_brightness(uint8_t p) { s_brightness = p; sim_set_brightness(p); }

void avo_hal_battery(avo_battery_t *o)
{
    *o = (avo_battery_t){ .present = true, .charging = s_charger, .usb = s_charger, .percent = 76, .millivolts = 3968 };
}

void avo_hal_restart(void) { exit(0); }

void avo_hal_accel(avo_accel_t *o)
{
    float t = sim_millis() / 1000.0f;
    *o = (avo_accel_t){ .valid = true, .ax = 0.25f * sinf(t * 0.9f), .ay = 0.18f * cosf(t * 0.7f), .az = 0.95f };
}

/* ---------------------------------------------------------------- Bluetooth */
void avo_hal_bt_enable(bool on) { s_bt_on = on; s_bt_since = sim_millis(); }
avo_link_t avo_hal_bt_state(void) { return s_bt_on ? AVO_LINK_BUSY : AVO_LINK_OFF; }
const char *avo_hal_bt_name(void) { return "avocatOS"; }

/* ---------------------------------------------------------------- Wi-Fi */
static const avo_wifi_ap_t FAKE_APS[] = {
    { "Casa", -48, true }, { "Aguacate 5G", -61, true }, { "Café Hass", -72, false }, { "Vecino", -83, true },
};

void avo_hal_wifi_enable(bool on)
{
    s_wifi_on = on;
    if (!on) {
        s_connected = s_connecting = s_scanning = false;
    }
}

avo_link_t avo_hal_wifi_state(void)
{
    if (!s_wifi_on) return AVO_LINK_OFF;
    if (s_connecting && sim_millis() - s_connect_at >= CONNECT_DELAY_MS) {
        s_connecting = false;
        s_connected = true;
    }
    if (s_connecting) return AVO_LINK_BUSY;
    return s_connected ? AVO_LINK_CONNECTED : AVO_LINK_IDLE;
}

bool avo_hal_wifi_scan_start(void)
{
    if (!s_wifi_on) return false;
    s_scanning = true;
    s_scan_at = sim_millis();
    return true;
}

int avo_hal_wifi_scan_results(avo_wifi_ap_t *out, int max)
{
    if (s_scanning && sim_millis() - s_scan_at < SCAN_DELAY_MS) return -1;
    s_scanning = false;
    int n = (int)(sizeof FAKE_APS / sizeof FAKE_APS[0]);
    n = n < max ? n : max;
    memcpy(out, FAKE_APS, (size_t)n * sizeof *out);
    return n;
}

bool avo_hal_wifi_connect(const char *ssid, const char *pass)
{
    (void)pass;
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    s_connected = false;
    s_connecting = true;
    s_connect_at = sim_millis();
    return true;
}

void avo_hal_wifi_info(char *ssid, size_t sl, char *ip, size_t il)
{
    bool up = avo_hal_wifi_state() == AVO_LINK_CONNECTED;
    snprintf(ssid, sl, "%s", up ? s_ssid : "");
    snprintf(ip, il, "%s", up ? "192.168.1.42" : "");
}

/* ---------------------------------------------------------------- system */
void avo_hal_sysinfo(avo_sysinfo_t *o)
{
    memset(o, 0, sizeof *o);
    snprintf(o->chip, sizeof o->chip, "ESP32-S3 rev 0.2");
    snprintf(o->mac, sizeof o->mac, "02:00:00:a0:ca:70");
    o->flash_mb = 32;
    o->psram_mb = 8;
    o->heap_internal_free = 142 * 1024;
    o->heap_psram_free = 7 * 1024 * 1024;
    o->uptime_s = sim_millis() / 1000;
    o->cpu_temp_c = 38.5f;
    snprintf(o->idf_version, sizeof o->idf_version, "v6.0.3 (sim)");
}

bool avo_hal_settings_load(avo_settings_t *out)
{
    if (sim_fresh_settings()) return false;
    FILE *f = fopen(SIM_SETTINGS_FILE, "rb");
    if (!f) return false;
    bool ok = fread(out, sizeof *out, 1, f) == 1 && out->version == AVO_SETTINGS_VERSION;
    fclose(f);
    return ok;
}

bool avo_hal_settings_save(const avo_settings_t *in)
{
    FILE *f = fopen(SIM_SETTINGS_FILE, "wb");
    if (!f) return false;
    bool ok = fwrite(in, sizeof *in, 1, f) == 1;
    fclose(f);
    return ok;
}

void avo_hal_click(void) {}

/* ---------------------------------------------------------------- fake iPhone */
static void add_notif(uint32_t uid, uint8_t cat, const char *app, const char *title, const char *msg, bool alert)
{
    avo_notif_t n = { .uid = uid, .category = cat };
    snprintf(n.app, sizeof n.app, "%s", app);
    snprintf(n.title, sizeof n.title, "%s", title);
    snprintf(n.message, sizeof n.message, "%s", msg);
    avo_text_clean(n.title);
    avo_text_clean(n.message);
    avo_hal_time_now(&n.when);
    memmove(&s_notifs[1], &s_notifs[0], sizeof(avo_notif_t) * (AVO_NOTIF_MAX - 1));
    s_notifs[0] = n;
    if (s_notif_count < AVO_NOTIF_MAX) s_notif_count++;
    s_notif_version++;
    if (alert) { s_alert = n; s_alert_pending = true; }
}

void sim_phone_connect(void)
{
    s_phone_ready = true;
    s_media = (avo_media_t){ .available = true, .playing = true, .volume = 60, .version = 1 };
    snprintf(s_media.app, sizeof s_media.app, "Spotify");
    snprintf(s_media.title, sizeof s_media.title, "Aguacate Tropical");
    snprintf(s_media.artist, sizeof s_media.artist, "Los Hass");
    snprintf(s_media.album, sizeof s_media.album, "Guacamole");
    add_notif(101, AVO_ANCS_CAT_EMAIL, "Mail", "Factura de septiembre", "Tu factura ya está disponible. Vence el 30 de septiembre.", false);
    add_notif(102, AVO_ANCS_CAT_SCHEDULE, "Calendario", "Revisión de diseño", "Empieza en 10 minutos · Sala Hass", false);
    add_notif(103, AVO_ANCS_CAT_SOCIAL, "WhatsApp", "Laura", "¿Nos vemos a las 7 en el parque? Llevo el aguacate para la tostada.", false);
}

void sim_phone_push(bool call)
{
    if (call) add_notif(200, AVO_ANCS_CAT_INCOMING_CALL, "Teléfono", "Mamá", "Llamada entrante", true);
    else add_notif(201, AVO_ANCS_CAT_SOCIAL, "Mensajes", "Carlos \xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBB", "Ya salí \xF0\x9F\x9A\x97\xF0\x9F\x92\xA8 llego en 15 minutos \xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD", true);
}

void sim_set_charger(bool on) { s_charger = on; }

avo_phone_state_t avo_hal_phone_state(void) { return !s_bt_on ? AVO_PHONE_OFF : s_phone_ready ? AVO_PHONE_READY : AVO_PHONE_WAITING; }
bool avo_hal_phone_bonded(void) { return s_phone_ready; }
void avo_hal_phone_forget(void) { s_phone_ready = false; s_media.available = false; s_media.version++; }
uint32_t avo_hal_notif_version(void) { return s_notif_version; }

int avo_hal_notif_list(avo_notif_t *out, int max)
{
    int n = s_notif_count < max ? s_notif_count : max;
    memcpy(out, s_notifs, sizeof(avo_notif_t) * (size_t)n);
    return n;
}

void avo_hal_notif_dismiss_local(uint32_t uid)
{
    for (int i = 0; i < s_notif_count; i++) {
        if (s_notifs[i].uid == uid) {
            memmove(&s_notifs[i], &s_notifs[i + 1], sizeof(avo_notif_t) * (size_t)(s_notif_count - i - 1));
            s_notif_count--;
            s_notif_version++;
            return;
        }
    }
}

void avo_hal_notif_action(uint32_t uid, bool positive) { (void)positive; avo_hal_notif_dismiss_local(uid); }

bool avo_hal_notif_take_alert(avo_notif_t *out)
{
    if (!s_alert_pending) return false;
    *out = s_alert;
    s_alert_pending = false;
    return true;
}

void avo_hal_media(avo_media_t *out) { *out = s_media; }

bool avo_hal_media_command(uint8_t cmd)
{
    if (!s_media.available) return false;
    if (cmd == AVO_AMS_CMD_TOGGLE) s_media.playing = !s_media.playing;
    if (cmd == AVO_AMS_CMD_VOL_UP && s_media.volume <= 90) s_media.volume += 10;
    if (cmd == AVO_AMS_CMD_VOL_DOWN && s_media.volume >= 10) s_media.volume -= 10;
    if (cmd == AVO_AMS_CMD_NEXT) snprintf(s_media.title, sizeof s_media.title, "Guacamole Nights");
    s_media.version++;
    return true;
}

bool avo_hal_artwork(avo_artwork_t *out) { (void)out; return false; }
