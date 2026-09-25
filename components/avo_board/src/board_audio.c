/*
 * Speaker: ES8311 codec (I2C control, I2S data) + amplifier on GPIO46, pins
 * from the Waveshare BSP 2.0.0. Sounds are synthesized by avo_core and
 * streamed from one task whose stack lives in PSRAM (it never touches flash).
 * The codec and amplifier are closed after a short idle time, which removes
 * the idle hiss and saves power.
 */
#include <string.h>
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "board_priv.h"

static const char *TAG = "board_audio";

#define PIN_I2S_MCLK 16
#define PIN_I2S_BCLK 41
#define PIN_I2S_WS 45
#define PIN_I2S_DOUT 40
#define PIN_PA 46
#define FRAMES 256
#define CODEC_VOLUME 80       /* hardware level; the user volume is applied in software */
#define IDLE_CLOSE_MS 1500
#define TASK_STACK (4 * 1024)
#define CMD_STOP (-1)

static QueueHandle_t s_cmds;
static esp_codec_dev_handle_t s_spk;

static esp_err_t codec_init(void)
{
    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 4;
    chan.dma_frame_num = FRAMES;
    chan.auto_clear_after_cb = true; /* silence, not a repeated buffer, if we fall behind */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &tx, NULL), TAG, "i2s channel");
    const i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AVO_SYNTH_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT, .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "i2s enable");

    audio_codec_i2s_cfg_t i2s_cfg = { .port = I2S_NUM_0, .tx_handle = tx };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = { .port = I2C_NUM_0, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = g_board_i2c };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    if (!data_if || !ctrl_if || !gpio_if) {
        return ESP_ERR_NO_MEM;
    }
    es8311_codec_cfg_t es = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = PIN_PA,
        .use_mclk = true,
        .hw_gain = { .pa_voltage = 5.0f, .codec_dac_voltage = 3.3f },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es);
    if (!codec_if) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_codec_dev_cfg_t dev = { .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec_if, .data_if = data_if };
    s_spk = esp_codec_dev_new(&dev);
    return s_spk ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool codec_open(void)
{
    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .sample_rate = AVO_SYNTH_RATE };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "codec open failed");
        return false;
    }
    esp_codec_dev_set_out_vol(s_spk, CODEC_VOLUME);
    return true;
}

static uint8_t user_volume(void)
{
    const avo_settings_t *s = board_settings_cache();
    return s ? s->volume : 70;
}

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[FRAMES];
    avo_synth_t syn = { 0 };
    bool active = false, open = false;
    uint32_t idle_since = 0;
    for (;;) {
        int8_t cmd;
        TickType_t wait = active ? 0 : open ? pdMS_TO_TICKS(100) : portMAX_DELAY;
        while (xQueueReceive(s_cmds, &cmd, wait) == pdTRUE) {
            wait = 0;
            if (cmd == CMD_STOP) {
                active = false;
            } else if (!(active && syn.loop && !avo_sound_loops((avo_sound_t)cmd))) {
                /* a ringing alarm is never cut short by a click */
                avo_synth_start(&syn, (avo_sound_t)cmd, user_volume());
                active = true;
            }
        }
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (active) {
            if (!open) {
                open = codec_open();
                if (!open) {
                    active = false;
                    continue;
                }
            }
            avo_synth_render(&syn, buf, FRAMES);
            esp_codec_dev_write(s_spk, buf, sizeof buf);
            if (avo_synth_done(&syn)) {
                active = false;
            }
            idle_since = now;
        } else if (open && now - idle_since > IDLE_CLOSE_MS) {
            esp_codec_dev_close(s_spk); /* amplifier off */
            open = false;
        }
    }
}

esp_err_t board_audio_init(void)
{
    esp_err_t err = codec_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker unavailable: %s", esp_err_to_name(err));
        return err;
    }
    s_cmds = xQueueCreate(8, sizeof(int8_t));
    if (!s_cmds || xTaskCreatePinnedToCoreWithCaps(audio_task, "avo_audio", TASK_STACK, NULL, 4, NULL, 0,
                                                   MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void post(int8_t cmd)
{
    if (s_cmds) {
        xQueueSend(s_cmds, &cmd, 0);
    }
}

void avo_hal_sound_play(avo_sound_t id)
{
    if (id < AVO_SOUND_COUNT) {
        post((int8_t)id);
    }
}

void avo_hal_sound_stop(void) { post(CMD_STOP); }

void avo_hal_click(void)
{
    const avo_settings_t *s = board_settings_cache();
    if (s && s->sounds) {
        post(AVO_SOUND_CLICK);
    }
}
