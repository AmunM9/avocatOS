/*
 * Transfer portal: a one-page web server on the local Wi-Fi, opened from
 * the iPhone's browser, to send the Retrato photo or install an update.
 *
 * Safety rules:
 *  - it only runs while its screen is open on the watch;
 *  - every upload carries the 4-digit PIN shown on the watch; five wrong
 *    PINs lock the portal until it is opened again;
 *  - sizes are checked before anything is written;
 *  - an update must be an avocatOS image (project name), is verified by
 *    esp_ota_end() (checksum + SHA-256) and is written to the *other* app
 *    slot; the running firmware stays intact, and a new image that does not
 *    confirm itself is rolled back by the bootloader.
 * The task stack is in internal RAM: flash is written from it.
 */
#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "board_priv.h"

static const char *TAG = "board_portal";

#define CHUNK 4096
#define MAX_BAD_PINS 5
#define RESTART_DELAY_US (2500 * 1000)
#define CONFIRM_AFTER_US (60 * 1000 * 1000)
#define SERVER_STACK 6144
#define MAX_TIMEOUTS 3          /* x recv_wait_timeout (15 s) */

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_srv;
static avo_portal_t s_st;          /* guarded by s_lock */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_bad_pins;

static void set_state(avo_portal_state_t st, uint8_t percent, const char *error)
{
    portENTER_CRITICAL(&s_lock);
    s_st.state = st;
    s_st.percent = percent;
    if (error) {
        strlcpy(s_st.error, error, sizeof s_st.error);
    }
    s_st.version++;
    portEXIT_CRITICAL(&s_lock);
}

void avo_hal_portal(avo_portal_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_st;
    portEXIT_CRITICAL(&s_lock);
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, text);
}

/* PIN from the X-PIN header; counts failures. */
static bool pin_ok(httpd_req_t *req)
{
    char pin[8] = "";
    if (s_bad_pins >= MAX_BAD_PINS) {
        return false;
    }
    if (httpd_req_get_hdr_value_str(req, "X-PIN", pin, sizeof pin) == ESP_OK && strcmp(pin, s_st.pin) == 0) {
        return true;
    }
    if (++s_bad_pins >= MAX_BAD_PINS) {
        set_state(AVO_PORTAL_ERROR, 0, "Demasiados PIN incorrectos");
    }
    return false;
}

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

/* Receive exactly `len` bytes, calling `sink` per chunk. */
static bool receive(httpd_req_t *req, size_t len, bool (*sink)(const uint8_t *, size_t, void *), void *ctx)
{
    uint8_t *buf = heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    size_t done = 0;
    int timeouts = 0;
    bool ok = buf != NULL;
    while (ok && done < len) {
        int n = httpd_req_recv(req, (char *)buf, len - done < CHUNK ? len - done : CHUNK);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < MAX_TIMEOUTS) {
            continue; /* the phone paused; give it a few chances */
        }
        ok = n > 0 && sink(buf, (size_t)n, ctx);
        done += n > 0 ? (size_t)n : 0;
        set_state(AVO_PORTAL_RECEIVING, (uint8_t)(done * 100 / len), NULL);
    }
    heap_caps_free(buf);
    return ok;
}

/* ---------------------------------------------------------------- photo */

typedef struct {
    uint8_t *data;
    size_t len;
} photo_rx_t;

static bool photo_sink(const uint8_t *d, size_t n, void *ctx)
{
    photo_rx_t *rx = ctx;
    memcpy(rx->data + rx->len, d, n);
    rx->len += n;
    return true;
}

static esp_err_t photo_post(httpd_req_t *req)
{
    if (!pin_ok(req)) {
        return reply(req, "403 Forbidden", "PIN incorrecto.");
    }
    if (req->content_len == 0 || req->content_len > PHOTO_JPEG_MAX) {
        return reply(req, "413 Payload Too Large", "La foto es demasiado grande.");
    }
    photo_rx_t rx = { heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM), 0 };
    if (!rx.data) {
        return reply(req, "500 Internal Server Error", "Sin memoria para la foto.");
    }
    bool ok = receive(req, req->content_len, photo_sink, &rx);
    esp_err_t err = ok ? board_photo_save(rx.data, rx.len) : ESP_FAIL;
    heap_caps_free(rx.data);
    if (err != ESP_OK) {
        set_state(AVO_PORTAL_ERROR, 0, ok ? "La foto no es válida" : "Se cortó el envío");
        return reply(req, "400 Bad Request", ok ? "La foto no es válida (410x502 JPEG)." : "Se cortó el envío.");
    }
    set_state(AVO_PORTAL_PHOTO_OK, 100, NULL);
    return reply(req, "200 OK", "ok");
}

/* ---------------------------------------------------------------- update */

typedef struct {
    esp_ota_handle_t h;
    const esp_partition_t *part;
    size_t seen;
    bool checked;
} ota_rx_t;

/* The app description sits right after the image and first segment headers. */
#define APP_DESC_OFFSET (sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t))

static bool ota_sink(const uint8_t *d, size_t n, void *ctx)
{
    ota_rx_t *rx = ctx;
    if (!rx->checked) {
        if (rx->seen + n < APP_DESC_OFFSET + sizeof(esp_app_desc_t) || d[0] != ESP_IMAGE_HEADER_MAGIC) {
            return false; /* first chunk is always big enough (4 KB) */
        }
        const esp_app_desc_t *desc = (const esp_app_desc_t *)(d + APP_DESC_OFFSET);
        if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
            strncmp(desc->project_name, esp_app_get_description()->project_name, sizeof desc->project_name) != 0) {
            return false;
        }
        rx->checked = true;
    }
    rx->seen += n;
    return esp_ota_write(rx->h, d, n) == ESP_OK;
}

static void restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static esp_err_t update_post(httpd_req_t *req)
{
    if (!pin_ok(req)) {
        return reply(req, "403 Forbidden", "PIN incorrecto.");
    }
    ota_rx_t rx = { .part = esp_ota_get_next_update_partition(NULL) };
    if (!rx.part || req->content_len < CHUNK || req->content_len > rx.part->size) {
        return reply(req, "413 Payload Too Large", "El archivo no parece una actualización de avocatOS.");
    }
    if (esp_ota_begin(rx.part, OTA_WITH_SEQUENTIAL_WRITES, &rx.h) != ESP_OK) {
        return reply(req, "500 Internal Server Error", "No se pudo preparar la actualización.");
    }
    bool ok = receive(req, req->content_len, ota_sink, &rx);
    if (!ok) {
        esp_ota_abort(rx.h);
        set_state(AVO_PORTAL_ERROR, 0, rx.checked ? "Se cortó el envío" : "No es una versión de avocatOS");
        return reply(req, "400 Bad Request", rx.checked ? "Se cortó el envío." : "Ese archivo no es de avocatOS.");
    }
    if (esp_ota_end(rx.h) != ESP_OK || esp_ota_set_boot_partition(rx.part) != ESP_OK) {
        set_state(AVO_PORTAL_ERROR, 0, "La imagen está dañada");
        return reply(req, "400 Bad Request", "La imagen está dañada; no se instaló nada.");
    }
    ESP_LOGI(TAG, "update written to %s, restarting", rx.part->label);
    set_state(AVO_PORTAL_UPDATE_OK, 100, NULL);
    reply(req, "200 OK", "ok");
    static esp_timer_handle_t t;
    const esp_timer_create_args_t args = { .callback = restart_cb, .name = "avo_restart" };
    if (!t && esp_timer_create(&args, &t) != ESP_OK) {
        return ESP_OK;
    }
    esp_timer_start_once(t, RESTART_DELAY_US);
    return ESP_OK;
}

/* ---------------------------------------------------------------- lifecycle */

bool avo_hal_portal_start(void)
{
    char ssid[AVO_WIFI_SSID_MAX], ip[16];
    avo_hal_wifi_info(ssid, sizeof ssid, ip, sizeof ip);
    if (!ip[0]) {
        set_state(AVO_PORTAL_NO_WIFI, 0, NULL);
        return false;
    }
    if (!s_srv) {
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.stack_size = SERVER_STACK;
        cfg.max_open_sockets = 3;
        cfg.lru_purge_enable = true;
        cfg.recv_wait_timeout = 15;
        if (httpd_start(&s_srv, &cfg) != ESP_OK) {
            set_state(AVO_PORTAL_ERROR, 0, "No se pudo abrir el portal");
            return false;
        }
        const httpd_uri_t uris[] = {
            { .uri = "/", .method = HTTP_GET, .handler = page_get },
            { .uri = "/photo", .method = HTTP_POST, .handler = photo_post },
            { .uri = "/update", .method = HTTP_POST, .handler = update_post },
        };
        for (size_t i = 0; i < sizeof uris / sizeof uris[0]; i++) {
            httpd_register_uri_handler(s_srv, &uris[i]);
        }
    }
    s_bad_pins = 0;
    portENTER_CRITICAL(&s_lock);
    snprintf(s_st.url, sizeof s_st.url, "http://%s", ip);
    snprintf(s_st.pin, sizeof s_st.pin, "%04u", (unsigned)(esp_random() % 10000));
    portEXIT_CRITICAL(&s_lock);
    set_state(AVO_PORTAL_WAITING, 0, "");
    ESP_LOGI(TAG, "portal open at http://%s", ip);
    return true;
}

void avo_hal_portal_stop(void)
{
    if (s_srv) {
        httpd_stop(s_srv);
        s_srv = NULL;
    }
    set_state(AVO_PORTAL_OFF, 0, "");
}

/* ---------------------------------------------------------------- update confirmation */

static void confirm_cb(void *arg)
{
    (void)arg;
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "update confirmed");
    }
}

void board_ota_confirm_later(void)
{
    static esp_timer_handle_t t;
    const esp_timer_create_args_t args = { .callback = confirm_cb, .name = "avo_confirm" };
    if (!t && esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, CONFIRM_AFTER_US);
    }
}

const char *avo_hal_fw_version(void)
{
    return esp_app_get_description()->version;
}
