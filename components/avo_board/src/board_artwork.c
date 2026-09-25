/*
 * Album art for Now Playing. AMS does not carry artwork, so when the track
 * changes and Wi-Fi is up, the track is looked up in Apple's public iTunes
 * Search API (artist + title), the cover JPEG is downloaded at the size the
 * UI draws, decoded to RGB565 in PSRAM and published with double buffering
 * so the UI never reads pixels that are being rewritten.
 */
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"
#include "board_priv.h"

static const char *TAG = "board_artwork";

#define ART_PX 132
#define POLL_MS 1500
#define SETTLE_MS 900           /* title and artist arrive separately        */
#define JSON_MAX (24 * 1024)
#define JPEG_MAX (48 * 1024)
#define HTTP_TIMEOUT_MS 6000
#define TASK_STACK (6 * 1024)

static uint16_t *s_buf[2];       /* PSRAM, ART_PX * ART_PX each               */
static volatile int s_front = -1; /* buffer the UI may read, -1 = none        */
static volatile uint32_t s_version;
static uint16_t s_w, s_h;

/* Download `url` into `dst` (at most cap-1 bytes, NUL terminated). */
static int http_get(const char *url, char *dst, int cap)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        return -1;
    }
    int total = -1;
    if (esp_http_client_open(c, 0) == ESP_OK && esp_http_client_fetch_headers(c) >= 0 &&
        esp_http_client_get_status_code(c) == 200) {
        total = 0;
        int n;
        while (total < cap - 1 && (n = esp_http_client_read(c, dst + total, cap - 1 - total)) > 0) {
            total += n;
        }
        dst[total] = '\0';
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return total;
}

static bool fetch_cover(const char *artist, const char *title)
{
    char term[160], q[400], url[256];
    snprintf(term, sizeof term, "%s %s", artist, title);
    if (!avo_url_encode(term, q, sizeof q)) {
        return false;
    }
    char *json = heap_caps_malloc(JSON_MAX, MALLOC_CAP_SPIRAM);
    uint8_t *jpeg = heap_caps_malloc(JPEG_MAX, MALLOC_CAP_SPIRAM);
    bool ok = false;
    if (json && jpeg) {
        char search[480];
        snprintf(search, sizeof search, "https://itunes.apple.com/search?media=music&entity=song&limit=1&term=%s", q);
        if (http_get(search, json, JSON_MAX) > 0 && avo_itunes_artwork_url(json, ART_PX, url, sizeof url)) {
            int n = http_get(url, (char *)jpeg, JPEG_MAX);
            int back = s_front == 0 ? 1 : 0;
            esp_jpeg_image_cfg_t jc = {
                .indata = jpeg,
                .indata_size = (uint32_t)(n > 0 ? n : 0),
                .outbuf = (uint8_t *)s_buf[back],
                .outbuf_size = ART_PX * ART_PX * 2,
                .out_format = JPEG_IMAGE_FORMAT_RGB565,
                .out_scale = JPEG_IMAGE_SCALE_0,
            };
            esp_jpeg_image_output_t out;
            if (n > 0 && esp_jpeg_decode(&jc, &out) == ESP_OK && out.width <= ART_PX && out.height <= ART_PX) {
                s_w = out.width;
                s_h = out.height;
                s_front = back;
                s_version++;
                ok = true;
            }
        }
    }
    heap_caps_free(json);
    heap_caps_free(jpeg);
    return ok;
}

static void artwork_task(void *arg)
{
    (void)arg;
    char done_key[130] = "";
    char seen_key[130] = "";
    uint32_t seen_at = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        avo_media_t m;
        avo_hal_media(&m);
        char key[130];
        snprintf(key, sizeof key, "%.63s|%.63s", m.artist, m.title);
        if (!m.available || !m.title[0] || strcmp(key, done_key) == 0) {
            continue;
        }
        if (strcmp(key, seen_key) != 0) {
            strlcpy(seen_key, key, sizeof seen_key); /* wait until artist + title settle */
            seen_at = avo_hal_millis();
            s_front = -1;                             /* stale cover: show the generated one */
            s_version++;
            continue;
        }
        if (avo_hal_millis() - seen_at < SETTLE_MS || avo_hal_wifi_state() != AVO_LINK_CONNECTED) {
            continue;
        }
        strlcpy(done_key, key, sizeof done_key);
        bool ok = fetch_cover(m.artist, m.title);
        ESP_LOGI(TAG, "cover for \"%s\": %s", m.title, ok ? "ok" : "not found");
    }
}

void board_artwork_start(void)
{
    s_buf[0] = heap_caps_malloc(ART_PX * ART_PX * 2, MALLOC_CAP_SPIRAM);
    s_buf[1] = heap_caps_malloc(ART_PX * ART_PX * 2, MALLOC_CAP_SPIRAM);
    if (!s_buf[0] || !s_buf[1]) {
        return;
    }
    /* stack in PSRAM: the task only does networking and decoding */
    xTaskCreatePinnedToCoreWithCaps(artwork_task, "avo_art", TASK_STACK, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM);
}

bool avo_hal_artwork(avo_artwork_t *out)
{
    int f = s_front;
    if (f < 0) {
        return false;
    }
    *out = (avo_artwork_t){ .pixels = s_buf[f], .w = s_w, .h = s_h, .version = s_version };
    return true;
}
