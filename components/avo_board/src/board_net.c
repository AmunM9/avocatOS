/*
 * Radios behind the HAL:
 *  - Wi-Fi station: scan, connect, auto-reconnect with saved credentials,
 *    NTP time sync (writes the RTC).
 * Bluetooth lives in board_ble.c (GAP/pairing) and board_apple.c (iPhone).
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "board_priv.h"

static const char *TAG = "board_net";

#define WIFI_MAX_RETRY 3
#define NTP_SERVER "pool.ntp.org"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* ================================================================= Wi-Fi */

static struct {
    bool stack_ready, started, want_on;
    volatile avo_link_t state;
    volatile bool scanning, scan_done;
    volatile bool scan_pending; /* requested while connecting: start when idle */
    avo_wifi_ap_t results[AVO_WIFI_MAX_RESULTS];
    int result_count;
    char ssid[AVO_WIFI_SSID_MAX];
    char ip[16];
    int retries;
    bool sntp_started;
} wf;

static void time_sync_cb(struct timeval *tv)
{
    board_rtc_store(tv->tv_sec);
    board_time_mark_synced(AVO_TIME_SRC_NTP);
    ESP_LOGI(TAG, "time synced over NTP");
}

static void start_sntp(void)
{
    if (wf.sntp_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    cfg.sync_cb = time_sync_cb;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        wf.sntp_started = true;
    }
}

static void collect_scan(void)
{
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    wifi_ap_record_t *recs = calloc(n ? n : 1, sizeof *recs);
    if (!recs) {
        esp_wifi_clear_ap_list();
        wf.scanning = false;
        wf.scan_done = true;
        return;
    }
    esp_wifi_scan_get_ap_records(&n, recs);
    int count = 0;
    avo_wifi_ap_t tmp[AVO_WIFI_MAX_RESULTS];
    for (int i = 0; i < n && count < AVO_WIFI_MAX_RESULTS; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (!ssid[0]) {
            continue; /* hidden network */
        }
        bool dup = false;
        for (int j = 0; j < count; j++) {
            dup |= strcmp(tmp[j].ssid, ssid) == 0;
        }
        if (dup) {
            continue;
        }
        strlcpy(tmp[count].ssid, ssid, sizeof tmp[count].ssid);
        tmp[count].rssi = recs[i].rssi;
        tmp[count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        count++;
    }
    free(recs);
    taskENTER_CRITICAL(&s_lock);
    memcpy(wf.results, tmp, sizeof tmp);
    wf.result_count = count;
    wf.scanning = false;
    wf.scan_done = true;
    taskEXIT_CRITICAL(&s_lock);
}

static void wifi_event_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        collect_scan();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        taskENTER_CRITICAL(&s_lock);
        wf.ip[0] = '\0';
        taskEXIT_CRITICAL(&s_lock);
        if (!wf.want_on) {
            wf.state = AVO_LINK_OFF;
        } else if (wf.retries < WIFI_MAX_RETRY && wf.ssid[0]) {
            wf.retries++;
            wf.state = AVO_LINK_BUSY;
            esp_wifi_connect();
        } else {
            wf.state = wf.ssid[0] ? AVO_LINK_ERROR : AVO_LINK_IDLE;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        taskENTER_CRITICAL(&s_lock);
        esp_ip4addr_ntoa(&ev->ip_info.ip, wf.ip, sizeof wf.ip);
        taskEXIT_CRITICAL(&s_lock);
        wf.retries = 0;
        wf.state = AVO_LINK_CONNECTED;
        start_sntp();
    }
}

/* Each step runs once: retrying after a failure must never recreate the
 * netif (ESP-IDF asserts on a duplicate default STA netif). */
static bool wifi_stack_init(void)
{
    static bool netif_ready, handlers_ready;
    if (wf.stack_ready) {
        return true;
    }
    if (!netif_ready) {
        if (esp_netif_init() != ESP_OK) {
            return false;
        }
        esp_err_t err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return false;
        }
        if (!esp_netif_create_default_wifi_sta()) {
            return false;
        }
        netif_ready = true;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi init failed (%s), internal RAM free %u", esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* credentials live in avocatOS settings */
    esp_wifi_set_mode(WIFI_MODE_STA);
    if (!handlers_ready) {
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_cb, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_cb, NULL);
        handlers_ready = true;
    }
    wf.stack_ready = true;
    return true;
}

static void wifi_apply_config(const char *ssid, const char *pass)
{
    wifi_config_t c = { 0 };
    strlcpy((char *)c.sta.ssid, ssid, sizeof c.sta.ssid);
    strlcpy((char *)c.sta.password, pass ? pass : "", sizeof c.sta.password);
    c.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA, &c);
    taskENTER_CRITICAL(&s_lock);
    strlcpy(wf.ssid, ssid, sizeof wf.ssid);
    taskEXIT_CRITICAL(&s_lock);
}

void avo_hal_wifi_enable(bool on)
{
    wf.want_on = on;
    if (on && !wf.started) {
        if (!wifi_stack_init() || esp_wifi_start() != ESP_OK) {
            wf.state = AVO_LINK_ERROR;
            return;
        }
        wf.started = true;
        wf.state = AVO_LINK_IDLE;
        const avo_settings_t *s = board_settings_cache();
        if (s && s->wifi_ssid[0]) {
            wifi_apply_config(s->wifi_ssid, s->wifi_pass);
            wf.retries = 0;
            wf.state = AVO_LINK_BUSY;
            esp_wifi_connect();
        }
    } else if (!on && wf.started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        wf.started = false;
        wf.scanning = false;
        wf.scan_pending = false;
        wf.state = AVO_LINK_OFF;
        taskENTER_CRITICAL(&s_lock);
        wf.ip[0] = '\0';
        wf.ssid[0] = '\0';
        taskEXIT_CRITICAL(&s_lock);
    }
}

avo_link_t avo_hal_wifi_state(void)
{
    return wf.started ? wf.state : AVO_LINK_OFF;
}

/* The driver refuses to scan while it is connecting (e.g. auto-reconnect
 * right after Wi-Fi is switched on), so a request made then is queued. */
static void try_pending_scan(void)
{
    if (wf.scan_pending && wf.started && wf.state != AVO_LINK_BUSY &&
        esp_wifi_scan_start(NULL, false) == ESP_OK) {
        wf.scan_pending = false;
    }
}

bool avo_hal_wifi_scan_start(void)
{
    if (!wf.started) {
        return false;
    }
    if (wf.scanning) {
        return true;
    }
    wf.scan_done = false;
    wf.scanning = true;
    wf.scan_pending = true;
    try_pending_scan();
    return true;
}

int avo_hal_wifi_scan_results(avo_wifi_ap_t *out, int max)
{
    try_pending_scan();
    if (wf.scanning || !wf.scan_done) {
        return -1;
    }
    taskENTER_CRITICAL(&s_lock);
    int n = wf.result_count < max ? wf.result_count : max;
    memcpy(out, wf.results, (size_t)n * sizeof *out);
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

bool avo_hal_wifi_connect(const char *ssid, const char *pass)
{
    if (!wf.started || !ssid || !ssid[0]) {
        return false;
    }
    esp_wifi_disconnect();
    wifi_apply_config(ssid, pass);
    wf.retries = 0;
    wf.state = AVO_LINK_BUSY;
    return esp_wifi_connect() == ESP_OK;
}

void avo_hal_wifi_info(char *ssid, size_t ssid_len, char *ip, size_t ip_len)
{
    taskENTER_CRITICAL(&s_lock);
    bool up = wf.state == AVO_LINK_CONNECTED && wf.started;
    strlcpy(ssid, up ? wf.ssid : "", ssid_len);
    strlcpy(ip, up ? wf.ip : "", ip_len);
    taskEXIT_CRITICAL(&s_lock);
}
