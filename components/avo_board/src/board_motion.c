/*
 * Motion at 100 Hz from the QMI8658: step counting (always), double tap
 * (the IMU's hardware tap engine, software fallback) and wrist flick (only
 * while the screen is on), and wrist raise (while it sleeps). Runs inside the
 * input task, one sample per call.
 */
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "avo_ui.h"
#include "board_priv.h"

static const char *TAG = "board_motion";

#define RAISE_EVERY (AVO_MOTION_HZ / AVO_RAISE_HZ) /* raise detector at 25 Hz */
#define TAP_POLL_EVERY 2        /* hardware tap status at 50 Hz          */
#define ROLL_EVERY AVO_MOTION_HZ /* activity bookkeeping once per second */
#define SAVE_EVERY_S 600        /* persist the day every 10 minutes      */
#define LOG_EVERY_S 60          /* orientation / steps line for tuning   */
/* "activity" held counts from the first step counter, which also counted
 * desk work; the new counter starts a clean day. */
#define NVS_KEY_ACTIVITY "activity2"

static avo_tap_t s_tap;             /* software fallback without the tap engine */
static avo_flick_t s_flick;
static avo_raise_t s_raise;
static avo_steps_t s_steps;
static avo_activity_t s_act;       /* guarded by s_lock */
static avo_accel_t s_last;         /* guarded by s_lock */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_n, s_new_steps, s_since_save;
static bool s_restored;

void board_motion_init(void)
{
    avo_steps_reset(&s_steps);
    avo_time_t now;
    avo_hal_time_now(&now);
    avo_activity_reset(&s_act, &now);
}

/* Today's totals survive a reboot. Runs once the UI has loaded the settings
 * (and so the UTC offset): before that "today" would be the UTC date. */
static void restore_today(const avo_time_t *now)
{
    avo_activity_t today, saved;
    size_t len = sizeof saved;
    avo_activity_reset(&today, now);
    if (board_nvs_load(NVS_KEY_ACTIVITY, &saved, &len) && len == sizeof saved && saved.day == today.day) {
        today = saved;
        ESP_LOGI(TAG, "restored %u steps", (unsigned)saved.steps);
    }
    portENTER_CRITICAL(&s_lock);
    s_act = today;
    portEXIT_CRITICAL(&s_lock);
    s_restored = true;
}

/* Wrist raised to look at the screen (only while it sleeps). */
static void raise_step(const float a[3])
{
    const avo_settings_t *s = board_settings_cache();
    bool fired = avo_raise_feed(&s_raise, a); /* always fed, so the history is fresh */
    if (fired && s && s->raise_to_wake && !avo_ui_is_awake()) {
        ESP_LOGI(TAG, "wrist raised: wake");
        avo_ui_post_wake();
    }
}

static void tap_step(const avo_settings_t *s, const float a[3], bool awake)
{
    bool want = awake && s->double_tap;
    if (board_imu_has_tap()) {
        if (s_n % TAP_POLL_EVERY == 0) {
            int t = board_imu_poll_tap();
            if (t) {
                ESP_LOGI(TAG, "tap: %s%s", t == 2 ? "double" : "single", want ? "" : " (ignored)");
            }
            if (t == 2 && want) {
                avo_ui_post_double_tap();
            }
        }
        return;
    }
    if (want) {
        if (avo_tap_feed(&s_tap, a)) {
            avo_ui_post_double_tap();
        }
    } else {
        memset(&s_tap, 0, sizeof s_tap); /* re-arm from a still wrist */
    }
}

static void gestures_step(const avo_settings_t *s, const float a[3], const float g[3])
{
    bool awake = avo_ui_is_awake();
    tap_step(s, a, awake);
    if (awake && s->wrist_flick) {
        if (avo_flick_feed(&s_flick, g)) {
            ESP_LOGI(TAG, "wrist flick");
            avo_ui_post_flick();
        }
    } else {
        memset(&s_flick, 0, sizeof s_flick);
    }
}

static void activity_roll(const avo_settings_t *s)
{
    if (!s || s->utc_offset_min != board_time_utc_offset()) {
        return; /* UI (and its time zone) not ready yet: keep the steps in s_new_steps */
    }
    avo_time_t now;
    avo_hal_time_now(&now);
    if (!s_restored) {
        restore_today(&now);
    }
    portENTER_CRITICAL(&s_lock);
    uint32_t day_before = s_act.day;
    avo_activity_add(&s_act, &now, s_new_steps);
    avo_activity_t copy = s_act;
    portEXIT_CRITICAL(&s_lock);
    s_new_steps = 0;
    board_imu_gyro(s->wrist_flick);
    if (++s_since_save >= SAVE_EVERY_S || copy.day != day_before) {
        s_since_save = 0;
        board_nvs_save(NVS_KEY_ACTIVITY, &copy, sizeof copy);
    }
    if (s_n % (LOG_EVERY_S * AVO_MOTION_HZ) == 0) {
        ESP_LOGI(TAG, "gravity (%.2f, %.2f, %.2f), walking %d, steps %u, stand %d h",
                 s_raise.g[0], s_raise.g[1], s_raise.g[2], s_steps.walking, (unsigned)copy.steps,
                 avo_activity_stand_hours(&copy));
    }
}

void board_motion_step(void)
{
    float a[3], g[3];
    if (!board_imu_read6(a, g)) {
        return;
    }
    s_n++;
    portENTER_CRITICAL(&s_lock);
    s_last = (avo_accel_t){ .valid = true, .ax = a[0], .ay = a[1], .az = a[2] };
    portEXIT_CRITICAL(&s_lock);
    s_new_steps += avo_steps_feed(&s_steps, a);
    const avo_settings_t *s = board_settings_cache();
    if (s) {
        gestures_step(s, a, g);
    }
    if (s_n % RAISE_EVERY == 0) {
        raise_step(a);
    }
    if (s_n % ROLL_EVERY == 0) {
        activity_roll(s);
    }
}

void avo_hal_accel(avo_accel_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_last;
    portEXIT_CRITICAL(&s_lock);
}

void avo_hal_activity(avo_activity_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_act;
    portEXIT_CRITICAL(&s_lock);
}
