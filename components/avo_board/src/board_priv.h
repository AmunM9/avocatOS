/* Board internals: pins (from Waveshare pin_config.h / BSP 2.0.0) and the
 * small drivers shared between board_*.c files. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "avo_hal.h"

/* ---- display: CO5300 over QSPI (driven with the SH8601 esp_lcd driver) */
#define PIN_LCD_CS 12
#define PIN_LCD_PCLK 11
#define PIN_LCD_D0 4
#define PIN_LCD_D1 5
#define PIN_LCD_D2 6
#define PIN_LCD_D3 7
#define PIN_LCD_RST 8
#define LCD_H_RES 410
#define LCD_V_RES 502
#define LCD_X_GAP 0x16
/* 80 MHz doubles the official BSP's 40 MHz (proven on this board by
 * waveshare-watch-rs). Lower to 40 if the panel ever shows artifacts. */
#define LCD_PCLK_HZ (80 * 1000 * 1000)
#define LCD_DRAW_LINES 40 /* per buffer: 410*40*2 = 32 KB, x2, internal DMA RAM (shared with Wi-Fi/BLE) */

/* ---- I2C: touch, PMU, IMU, RTC, codec */
#define PIN_I2C_SDA 15
#define PIN_I2C_SCL 14
#define I2C_HZ 400000
#define PIN_TP_INT 38
#define PIN_TP_RST 9
#define I2C_ADDR_AXP2101 0x34
#define I2C_ADDR_QMI8658 0x6B
#define I2C_ADDR_PCF85063 0x51

/* ---- buttons */
#define PIN_BOOT 0 /* PWR is read through the AXP2101 key IRQ flags */

extern i2c_master_bus_handle_t g_board_i2c;

esp_err_t board_i2c_init(void);
i2c_master_dev_handle_t board_i2c_add(uint8_t addr);
esp_err_t board_reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len);
esp_err_t board_reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);

/* AXP2101 power monitor. Only measurement/IRQ-flag registers are written,
 * never voltage or rail settings. */
esp_err_t board_pmu_init(void);
void board_pmu_read(avo_battery_t *out);
/* Returns PWR key events since the last call. */
void board_pmu_poll_key(bool *short_press, bool *long_press);

/* PCF85063 RTC: loads the system clock (UTC) at boot. */
esp_err_t board_rtc_init(void);
void board_rtc_store(time_t utc);

/* QMI8658 accelerometer (gyro stays off to save power). */
esp_err_t board_imu_init(void);
bool board_imu_read(float *ax, float *ay, float *az);

esp_err_t board_display_init(void);
void board_display_brightness(uint8_t percent);

esp_err_t board_input_start(void);

/* Settings last loaded/saved, used by board code (Wi-Fi credentials,
 * raise-to-wake) without calling into the UI. */
const avo_settings_t *board_settings_cache(void);
void board_time_mark_synced(avo_time_src_t src);
void board_time_set_phone_offset(int16_t minutes);
int16_t board_time_utc_offset(void);

/* Bluetooth: board_ble.c owns GAP/pairing, board_apple.c the iPhone services. */
struct os_mbuf;
void board_ble_set_phone_state(avo_phone_state_t st);
uint16_t board_ble_conn(void);
void board_apple_init(void);
void board_apple_start(uint16_t conn);   /* link encrypted: discover + subscribe */
void board_apple_stop(void);             /* link lost                          */
void board_apple_on_notify(uint16_t attr_handle, const struct os_mbuf *om);
void board_artwork_start(void);          /* album art over Wi-Fi             */
