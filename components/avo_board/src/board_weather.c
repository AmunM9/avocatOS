/*
 * Forecast for the Tiempo app, faces and Smart Stack. There is no GPS: the
 * approximate location comes once per boot from the public IP (ipwho.is,
 * no account), then Open-Meteo (no API key) gives the forecast every 30 min.
 * Coordinates are rounded to 0.01 deg (~1 km) before they leave the watch.
 */
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "board_priv.h"

static const char *TAG = "board_weather";

#define REFRESH_MS (30u * 60u * 1000u)
#define RETRY_MS (5u * 60u * 1000u)
#define JSON_CAP 4096
#define GEO_URL "https://ipwho.is/?fields=success,city,latitude,longitude"
#define FORECAST_URL "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f" \
    "&current=temperature_2m,weather_code,is_day"                                      \
    "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=%d"

static avo_weather_t s_wx;        /* guarded by s_lock */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_located;
static double s_lat, s_lon;
static char s_city[32];
static uint32_t s_next_ms;

static bool locate(char *json)
{
    if (s_located) {
        return true;
    }
    if (board_http_get(GEO_URL, json, JSON_CAP) <= 0 || !avo_geo_parse(json, &s_lat, &s_lon, s_city, sizeof s_city)) {
        ESP_LOGW(TAG, "location lookup failed");
        return false;
    }
    s_located = true;
    ESP_LOGI(TAG, "location: %s", s_city[0] ? s_city : "(no city)");
    return true;
}

void board_weather_step(void)
{
    const avo_settings_t *s = board_settings_cache();
    uint32_t now = avo_hal_millis();
    if (!s || !s->weather || avo_hal_wifi_state() != AVO_LINK_CONNECTED || (int32_t)(now - s_next_ms) < 0) {
        return;
    }
    s_next_ms = now + RETRY_MS;
    char *json = heap_caps_malloc(JSON_CAP, MALLOC_CAP_SPIRAM);
    if (!json) {
        return;
    }
    if (locate(json)) {
        static char url[320]; /* only this task uses it */
        snprintf(url, sizeof url, FORECAST_URL, s_lat, s_lon, AVO_WX_DAYS);
        avo_weather_t w;
        if (board_http_get(url, json, JSON_CAP) > 0 && avo_weather_parse(json, &w)) {
            strlcpy(w.city, s_city, sizeof w.city);
            w.updated_ms = now;
            portENTER_CRITICAL(&s_lock);
            s_wx = w;
            portEXIT_CRITICAL(&s_lock);
            s_next_ms = now + REFRESH_MS;
            ESP_LOGI(TAG, "%.1f C, code %d", w.temp, w.code);
        } else {
            ESP_LOGW(TAG, "forecast failed");
        }
    }
    heap_caps_free(json);
}

bool avo_hal_weather(avo_weather_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_wx;
    portEXIT_CRITICAL(&s_lock);
    return out->valid;
}

bool avo_hal_location(double *lat, double *lon)
{
    if (!s_located) {
        return false;
    }
    *lat = s_lat;
    *lon = s_lon;
    return true;
}
