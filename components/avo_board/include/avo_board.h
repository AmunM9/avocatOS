/* avocatOS board support for the Waveshare ESP32-S3-Touch-AMOLED-2.06. */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Buses, power monitor, RTC, IMU, display (80 MHz QSPI), touch, LVGL, buttons. */
esp_err_t avo_board_init(void);
/* Builds the avocatOS UI inside the LVGL task lock. */
esp_err_t avo_board_start_ui(void);

#ifdef __cplusplus
}
#endif
