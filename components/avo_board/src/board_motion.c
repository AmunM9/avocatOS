/*
 * Motion at 100 Hz from the QMI8658: step counting (always), double tap and
 * wrist flick (only while the screen is on), and wrist raise (while it
 * sleeps). Runs inside the input task, one sample per call.
 */
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "avo_ui.h"
#include "board_priv.h"

static const char *TAG = "board_motion";

#define RAISE_EVERY 10          /* wrist raise check at 10 Hz            */
#define RAISE_FACE_UP_G 0.75f
#define RAISE_TILTED_G 0.35f
#define RAISE_HOLD_TICKS 2
#define ROLL_EVERY AVO_MOTION_HZ /* activity bookkeeping once per second */
#define SAVE_EVERY_S 600        /* persist the day every 10 minutes      */
#define NVS_KEY_ACTIVITY "activity"

static avo_tap_t s_tap;
static avo_flick_t s_flick;
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

/* Screen-up after being tilted away for a moment = wrist raised. */
static void raise_step(const float a[3])
{
    static bool was_tilted;
    static int hold;
    const avo_settings_t *s = board_settings_cache();
    if (!s || !s->raise_to_wake || avo_ui_is_awake()) {
        was_tilted = false;
        hold = 0;
        return;
    }
    if (a[2] < RAISE_TILTED_G) {
        was_tilted = true;
        hold = 0;
    } else if (was_tilted && a[2] > RAISE_FACE_UP_G && fabsf(a[0]) < 0.5f && fabsf(a[1]) < 0.6f) {
        if (++hold >= RAISE_HOLD_TICKS) {
            avo_ui_post_wake();
            was_tilted = false;
            hold = 0;
        }
    }
}

static void gestures_step(const avo_settings_t *s, const float a[3], const float g[3])
{
    bool awake = avo_ui_is_awake();
    if (awake && s->double_tap) {
        if (avo_tap_feed(&s_tap, a)) {
            avo_ui_post_double_tap();
        }
    } else {
        memset(&s_tap, 0, sizeof s_tap); /* re-arm from a still wrist */
    }
    if (awake && s->wrist_flick) {
        if (avo_flick_feed(&s_flick, g)) {
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
