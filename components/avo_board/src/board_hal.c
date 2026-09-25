/* avo_hal implementation for the real board + avo_board_init(). */
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include "driver/temperature_sensor.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "avo_board.h"
#include "avo_ui.h"
#include "board_priv.h"

static const char *TAG = "avocatOS";

#define NVS_NS "avocatos"
#define NVS_KEY "settings"

static avo_settings_t s_cache;
static bool s_cache_valid;
static volatile int16_t s_utc_offset_min;
static volatile avo_time_src_t s_time_src = AVO_TIME_SRC_INTERNAL;
static volatile int64_t s_synced_at_us = -1;
static volatile int16_t s_phone_offset;
static volatile bool s_phone_offset_valid;
static temperature_sensor_handle_t s_tsens;

/* ================================================================= time */

uint32_t avo_hal_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void avo_hal_time_now(avo_time_t *out)
{
    time_t t = time(NULL) + (time_t)s_utc_offset_min * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    *out = (avo_time_t){
        .year = tm.tm_year + 1900, .month = tm.tm_mon + 1, .day = tm.tm_mday, .wday = tm.tm_wday,
        .hour = tm.tm_hour, .min = tm.tm_min, .sec = tm.tm_sec,
    };
}

void avo_hal_time_set_utc_offset(int16_t minutes) { s_utc_offset_min = minutes; }
int16_t board_time_utc_offset(void) { return s_utc_offset_min; }
avo_time_src_t avo_hal_time_source(void) { return s_time_src; }

uint32_t avo_hal_time_since_sync_s(void)
{
    return s_synced_at_us < 0 ? UINT32_MAX : (uint32_t)((esp_timer_get_time() - s_synced_at_us) / 1000000);
}

void board_time_mark_synced(avo_time_src_t src)
{
    s_time_src = src;
    s_synced_at_us = esp_timer_get_time();
}

void board_time_set_phone_offset(int16_t minutes)
{
    s_phone_offset = minutes;
    s_phone_offset_valid = true;
}

bool avo_hal_time_phone_offset(int16_t *minutes)
{
    if (s_phone_offset_valid) {
        *minutes = s_phone_offset;
    }
    return s_phone_offset_valid;
}

/* ================================================================= display / power */

void avo_hal_display_brightness(uint8_t percent) { board_display_brightness(percent); }
void avo_hal_battery(avo_battery_t *out) { board_pmu_read(out); }
void avo_hal_restart(void) { esp_restart(); }



/* ================================================================= system */

void avo_hal_sysinfo(avo_sysinfo_t *out)
{
    memset(out, 0, sizeof *out);
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    snprintf(out->chip, sizeof out->chip, "ESP32-S3 rev %d.%d", chip.revision / 100, chip.revision % 100);
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out->mac, sizeof out->mac, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    uint32_t flash = 0;
    esp_flash_get_size(NULL, &flash);
    out->flash_mb = flash / (1024 * 1024);
    out->psram_mb = (uint32_t)(esp_psram_get_size() / (1024 * 1024));
    out->heap_internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->heap_psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    out->cpu_temp_c = NAN;
    float c;
    if (s_tsens && temperature_sensor_get_celsius(s_tsens, &c) == ESP_OK) {
        out->cpu_temp_c = c;
    }
    snprintf(out->idf_version, sizeof out->idf_version, "%s", esp_get_idf_version());
}

/* ================================================================= settings (NVS) */

const avo_settings_t *board_settings_cache(void)
{
    return s_cache_valid ? &s_cache : NULL;
}

bool avo_hal_settings_load(avo_settings_t *out)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = sizeof *out;
    esp_err_t err = nvs_get_blob(h, NVS_KEY, out, &len);
    nvs_close(h);
    /* a version 1 blob is shorter: it upgrades in place, keeping Wi-Fi etc. */
    if (err != ESP_OK || !avo_settings_upgrade(out, len)) {
        return false;
    }
    s_cache = *out;
    s_cache_valid = true;
    return true;
}

bool avo_hal_settings_save(const avo_settings_t *in)
{
    if (s_cache_valid && memcmp(&s_cache, in, sizeof *in) == 0) {
        return true; /* nothing changed: spare the flash */
    }
    s_cache = *in;
    s_cache_valid = true;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, NVS_KEY, in, sizeof *in);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

/* ================================================================= small blobs */

bool board_nvs_load(const char *key, void *buf, size_t *len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_get_blob(h, key, buf, len);
    nvs_close(h);
    return err == ESP_OK;
}

bool board_nvs_save(const char *key, const void *buf, size_t len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(h, key, buf, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

bool avo_hal_alarms_load(avo_alarms_t *out)
{
    size_t len = sizeof *out;
    if (!board_nvs_load("alarms", out, &len) || len != sizeof *out || out->version != AVO_ALARMS_VERSION) {
        return false;
    }
    avo_alarms_sanitize(out);
    return true;
}

bool avo_hal_alarms_save(const avo_alarms_t *in)
{
    return board_nvs_save("alarms", in, sizeof *in);
}

/* ================================================================= init */

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* leftovers from the previous firmware: only our own NVS partition is erased */
        ESP_LOGW(TAG, "NVS format changed, erasing the nvs partition");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

esp_err_t avo_board_init(void)
{
    nvs_init();
    ESP_ERROR_CHECK(board_i2c_init());
    board_pmu_init();  /* optional: the UI shows "—" without it */
    board_rtc_init();
    board_imu_init();
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    if (temperature_sensor_install(&tcfg, &s_tsens) == ESP_OK) {
        temperature_sensor_enable(s_tsens);
    }
    ESP_ERROR_CHECK(board_display_init());
    board_audio_init(); /* optional: the watch works silently without it */
    ESP_ERROR_CHECK(board_input_start());
    ESP_LOGI(TAG, "internal RAM free %u KB (largest %u KB), PSRAM free %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    return ESP_OK;
}

esp_err_t avo_board_start_ui(void)
{
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    avo_ui_start();
    lvgl_port_unlock();
    board_artwork_start();
    ESP_LOGI(TAG, "radios up: internal RAM free %u KB (DMA-capable %u KB)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024));
    return ESP_OK;
}
