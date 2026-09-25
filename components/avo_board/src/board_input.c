/*
 * Physical inputs polled from one low-priority task:
 *  - BOOT (GPIO0): short / double / long press   -> Digital Crown role
 *  - PWR (AXP2101 key flags): short / long press -> side button role
 *  - wrist raise from the accelerometer (when the screen sleeps)
 */
#include <math.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "avo_ui.h"
#include "board_priv.h"

#define POLL_MS 20
#define PMU_EVERY 3          /* poll the PMU key every 60 ms          */
#define IMU_EVERY 5          /* accelerometer at 10 Hz while asleep   */
#define LONG_MS 700
#define DOUBLE_GAP_MS 280
#define DEBOUNCE_MS 30
#define RAISE_FACE_UP_G 0.75f
#define RAISE_TILTED_G 0.35f
#define RAISE_HOLD_TICKS 2
#define INPUT_TASK_STACK 3072
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

/* Screen-up after being tilted away for a moment = wrist raised. */
static void raise_step(void)
{
    static bool was_tilted;
    static int hold;
    const avo_settings_t *s = board_settings_cache();
    if (!s || !s->raise_to_wake || avo_ui_is_awake()) {
        was_tilted = false;
        hold = 0;
        return;
    }
    float ax, ay, az;
    if (!board_imu_read(&ax, &ay, &az)) {
        return;
    }
    if (az < RAISE_TILTED_G) {
        was_tilted = true;
        hold = 0;
    } else if (was_tilted && az > RAISE_FACE_UP_G && fabsf(ax) < 0.5f && fabsf(ay) < 0.6f) {
        if (++hold >= RAISE_HOLD_TICKS) {
            avo_ui_post_wake();
            was_tilted = false;
            hold = 0;
        }
    }
}

static void input_task(void *arg)
{
    (void)arg;
    uint32_t tick = 0;
    for (;;) {
        boot_button_step();
        if (tick % PMU_EVERY == 0) {
            pmu_key_step();
        }
        if (tick % IMU_EVERY == 0) {
            raise_step();
        }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
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
