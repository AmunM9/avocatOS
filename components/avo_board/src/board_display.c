/*
 * Display pipeline tuned for smoothness:
 *  - QSPI at 80 MHz (the official BSP uses 40 MHz),
 *  - two DMA-capable draw buffers in internal SRAM (the BSP uses one small
 *    non-DMA buffer in PSRAM), so LVGL renders one while the other is sent,
 *  - LVGL task pinned to core 1 with two parallel SW draw units.
 */
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "board_priv.h"

static const char *TAG = "board_display";

#define LCD_HOST SPI2_HOST
#define LCD_CMD_WRITE (0x02UL << 24)
#define LCD_CMD_BRIGHTNESS 0x51
#define LVGL_TASK_PRIO 5
#define LVGL_TASK_STACK (12 * 1024)
#define LVGL_TASK_CORE 1

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;

/* CO5300 power-up sequence from the Waveshare BSP 2.0.0 */
static const sh8601_lcd_init_cmd_t LCD_INIT_CMDS[] = {
    { 0x11, (uint8_t[]){ 0x00 }, 0, 120 },
    { 0xC4, (uint8_t[]){ 0x80 }, 1, 0 },
    { 0x44, (uint8_t[]){ 0x01, 0xD1 }, 2, 0 },
    { 0x35, (uint8_t[]){ 0x00 }, 1, 0 },
    { 0x53, (uint8_t[]){ 0x20 }, 1, 10 },
    { 0x63, (uint8_t[]){ 0xFF }, 1, 10 },
    { 0x51, (uint8_t[]){ 0x00 }, 1, 10 },
    { 0x2A, (uint8_t[]){ 0x00, 0x16, 0x01, 0xAF }, 4, 0 },
    { 0x2B, (uint8_t[]){ 0x00, 0x00, 0x01, 0xF5 }, 4, 0 },
    { 0x29, (uint8_t[]){ 0x00 }, 0, 10 },
};

/* The controller needs even start / odd end coordinates. */
static void rounder_cb(lv_area_t *a)
{
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

void board_display_brightness(uint8_t percent)
{
    if (!s_io) {
        return;
    }
    uint8_t v = (uint8_t)((percent > 100 ? 100 : percent) * 255 / 100);
    esp_lcd_panel_io_tx_param(s_io, LCD_CMD_WRITE | (LCD_CMD_BRIGHTNESS << 8), &v, 1);
}

static esp_err_t panel_init(void)
{
    const spi_bus_config_t bus = SH8601_PANEL_BUS_QSPI_CONFIG(PIN_LCD_PCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2,
                                                              PIN_LCD_D3, LCD_H_RES * LCD_DRAW_LINES * 2);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = LCD_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &s_io), TAG, "io");

    sh8601_vendor_config_t vendor = {
        .init_cmds = LCD_INIT_CMDS,
        .init_cmds_size = sizeof LCD_INIT_CMDS / sizeof LCD_INIT_CMDS[0],
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");
    return ESP_OK;
}

static esp_err_t touch_init(lv_display_t *disp)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_cfg.scl_speed_hz = I2C_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(g_board_i2c, &tp_io_cfg, &tp_io), TAG, "touch io");
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
    };
    esp_lcd_touch_handle_t tp = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &tp), TAG, "touch");
    const lvgl_port_touch_cfg_t port_tp = { .disp = disp, .handle = tp };
    return lvgl_port_add_touch(&port_tp) ? ESP_OK : ESP_FAIL;
}

esp_err_t board_display_init(void)
{
    ESP_RETURN_ON_ERROR(panel_init(), TAG, "panel init");

    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_priority = LVGL_TASK_PRIO;
    port.task_stack = LVGL_TASK_STACK;
    port.task_affinity = LVGL_TASK_CORE;
    port.task_max_sleep_ms = 500;
    port.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port), TAG, "lvgl port");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io,
        .panel_handle = s_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .rounder_cb = rounder_cb,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (!disp) {
        return ESP_FAIL;
    }
    board_display_brightness(0); /* the UI raises it once the first frame is ready */
    ESP_RETURN_ON_ERROR(touch_init(disp), TAG, "touch init");
    ESP_LOGI(TAG, "display %dx%d @ %d MHz QSPI, 2x%d-line DMA buffers", LCD_H_RES, LCD_V_RES,
             LCD_PCLK_HZ / 1000000, LCD_DRAW_LINES);
    return ESP_OK;
}
