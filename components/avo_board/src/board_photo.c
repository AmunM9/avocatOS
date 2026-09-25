/*
 * Photo of the Retrato face. The iPhone's browser crops it to the screen
 * and sends it as a JPEG (board_portal.c); it is kept in the "photo" flash
 * partition and decoded to RGB565 in PSRAM when the face needs it.
 *
 * Layout: the JPEG starts at the second sector; the header in the first
 * sector is written last, so a power cut mid-write leaves no valid photo.
 */
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "jpeg_decoder.h"
#include "board_priv.h"

static const char *TAG = "board_photo";

#define PHOTO_MAGIC 0x504F5641u   /* "AVOP" */
#define SECTOR 4096
#define DATA_OFFSET SECTOR

typedef struct {
    uint32_t magic;
    uint32_t len;
    uint32_t crc;
} photo_hdr_t;

static SemaphoreHandle_t s_mtx;
static uint16_t *s_px;           /* PHOTO_W x PHOTO_H, PSRAM          */
static bool s_decoded, s_absent;
static uint32_t s_version = 1;

static const esp_partition_t *photo_part(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, PHOTO_PART_SUBTYPE, "photo");
}

static void lock(void)
{
    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
}

static void unlock(void) { xSemaphoreGive(s_mtx); }

bool board_photo_check(const uint8_t *jpeg, size_t len)
{
    esp_jpeg_image_cfg_t cfg = { .indata = (uint8_t *)jpeg, .indata_size = (uint32_t)len };
    esp_jpeg_image_output_t info;
    return esp_jpeg_get_image_info(&cfg, &info) == ESP_OK && info.width == PHOTO_W && info.height == PHOTO_H;
}

esp_err_t board_photo_save(const uint8_t *jpeg, size_t len)
{
    const esp_partition_t *p = photo_part();
    if (!p || len == 0 || len > p->size - DATA_OFFSET) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!board_photo_check(jpeg, len)) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t span = (DATA_OFFSET + len + SECTOR - 1) / SECTOR * SECTOR;
    esp_err_t err = esp_partition_erase_range(p, 0, span);
    if (err == ESP_OK) {
        err = esp_partition_write(p, DATA_OFFSET, jpeg, len);
    }
    if (err == ESP_OK) {
        photo_hdr_t h = { PHOTO_MAGIC, (uint32_t)len, esp_rom_crc32_le(0, jpeg, (uint32_t)len) };
        err = esp_partition_write(p, 0, &h, sizeof h);
    }
    lock();
    s_decoded = s_absent = false; /* decode again on the next request */
    s_version++;
    unlock();
    ESP_LOGI(TAG, "photo saved (%u bytes): %s", (unsigned)len, esp_err_to_name(err));
    return err;
}

void avo_hal_photo_delete(void)
{
    const esp_partition_t *p = photo_part();
    if (p) {
        esp_partition_erase_range(p, 0, SECTOR);
    }
    lock();
    s_decoded = false;
    s_absent = true;
    s_version++;
    unlock();
}

/* Read + verify + decode. Called with the lock held. */
static bool decode_locked(void)
{
    const esp_partition_t *p = photo_part();
    photo_hdr_t h;
    if (!p || esp_partition_read(p, 0, &h, sizeof h) != ESP_OK || h.magic != PHOTO_MAGIC ||
        h.len == 0 || h.len > p->size - DATA_OFFSET) {
        return false;
    }
    uint8_t *jpeg = heap_caps_malloc(h.len, MALLOC_CAP_SPIRAM);
    if (!s_px) {
        s_px = heap_caps_malloc(PHOTO_W * PHOTO_H * 2, MALLOC_CAP_SPIRAM);
    }
    bool ok = jpeg && s_px && esp_partition_read(p, DATA_OFFSET, jpeg, h.len) == ESP_OK &&
              esp_rom_crc32_le(0, jpeg, h.len) == h.crc;
    if (ok) {
        esp_jpeg_image_cfg_t cfg = {
            .indata = jpeg, .indata_size = h.len,
            .outbuf = (uint8_t *)s_px, .outbuf_size = PHOTO_W * PHOTO_H * 2,
            .out_format = JPEG_IMAGE_FORMAT_RGB565, .out_scale = JPEG_IMAGE_SCALE_0,
        };
        esp_jpeg_image_output_t out;
        ok = esp_jpeg_decode(&cfg, &out) == ESP_OK && out.width == PHOTO_W && out.height == PHOTO_H;
    }
    heap_caps_free(jpeg);
    if (!ok) {
        ESP_LOGW(TAG, "stored photo unreadable");
    }
    return ok;
}

bool avo_hal_photo(avo_photo_t *out)
{
    lock();
    if (!s_decoded && !s_absent) {
        s_decoded = decode_locked();
        s_absent = !s_decoded;
    }
    bool ok = s_decoded;
    *out = (avo_photo_t){ .pixels = ok ? s_px : NULL, .w = PHOTO_W, .h = PHOTO_H, .version = s_version };
    unlock();
    return ok;
}
