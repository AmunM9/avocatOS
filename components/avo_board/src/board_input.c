/*
 * Physical inputs polled from one low-priority task:
 *  - BOOT (GPIO0): short / double / long press   -> Digital Crown role
 *  - PWR (AXP2101 key flags): short / long press -> side button role
 *  - motion at 100 Hz: steps, double tap, wrist flick, wrist raise
 * The motion code writes NVS, so this task keeps its stack in internal RAM.
 */
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "avo_ui.h"
#include "board_priv.h"

#define POLL_MS 10           /* = 1000 / AVO_MOTION_HZ                */
#define BOOT_EVERY 2         /* BOOT button every 20 ms               */
#define PMU_EVERY 6          /* PMU key every 60 ms                   */
#define LONG_MS 700
#define DOUBLE_GAP_MS 280
#define DEBOUNCE_MS 30
#define INPUT_TASK_STACK 4096
#define INPUT_TASK_PRIO 3

typedef enum { B_IDLE, B_DOWN, B_WAIT_SECOND, B_LONG_SENT } boot_state_t;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void boot_button_step(void)
{
    static boot_state_t st = B_IDLE;
    static uint32_t t0, released_at;
    static bool second;
    bool down = gpio_get_level(PIN_BOOT) == 0;
    uint32_t t = now_ms();

    switch (st) {
    case B_IDLE:
        if (down) {
            st = B_DOWN;
            t0 = t;
            second = false;
        }
        break;
    case B_DOWN:
        if (down && t - t0 >= LONG_MS) {
            avo_ui_post_button(AVO_BTN_BOOT, AVO_PRESS_LONG);
            st = B_LONG_SENT;
        } else if (!down && t - t0 >= DEBOUNCE_MS) {
            if (second) {
                avo_ui_post_button(AVO_BTN_BOOT, AVO_PRESS_DOUBLE);
                st = B_IDLE;
            } else {
                released_at = t;
                st = B_WAIT_SECOND;
            }
        } else if (!down) {
            st = B_IDLE; /* bounce */
        }
        break;
    case B_WAIT_SECOND:
        if (down) {
            st = B_DOWN;
            t0 = t;
            second = true;
        } else if (t - released_at >= DOUBLE_GAP_MS) {
            avo_ui_post_button(AVO_BTN_BOOT, AVO_PRESS_SHORT);
            st = B_IDLE;
        }
        break;
    case B_LONG_SENT:
        if (!down) {
            st = B_IDLE;
        }
        break;
    }
}

static void pmu_key_step(void)
{
    bool s, l;
    board_pmu_poll_key(&s, &l);
    if (l) {
        avo_ui_post_button(AVO_BTN_PWR, AVO_PRESS_LONG);
    } else if (s) {
        avo_ui_post_button(AVO_BTN_PWR, AVO_PRESS_SHORT);
    }
}

static void input_task(void *arg)
{
    (void)arg;
    uint32_t tick = 0;
    TickType_t wake = xTaskGetTickCount();
    board_motion_init();
    for (;;) {
        if (tick % BOOT_EVERY == 0) {
            boot_button_step();
        }
        if (tick % PMU_EVERY == 0) {
            pmu_key_step();
        }
        board_motion_step();
        tick++;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(POLL_MS));
    }
}

esp_err_t board_input_start(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    BaseType_t ok = xTaskCreatePinnedToCore(input_task, "avo_input", INPUT_TASK_STACK, NULL, INPUT_TASK_PRIO, NULL, 0);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
