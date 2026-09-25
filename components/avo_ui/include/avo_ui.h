/* avocatOS user interface — public entry points. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AVO_BTN_BOOT = 0, /* acts like the Digital Crown: home / app grid */
    AVO_BTN_PWR,      /* acts like the side button: Control Center   */
} avo_btn_t;

typedef enum {
    AVO_PRESS_SHORT = 0,
    AVO_PRESS_DOUBLE,
    AVO_PRESS_LONG,
} avo_press_t;

/* Build the UI. Call once from the LVGL task after the display and input
 * devices exist. Loads settings through avo_hal_settings_load(). */
void avo_ui_start(void);

/* Thread safe: may be called from any task or from the board's button
 * polling. Events are consumed by the LVGL task within ~20 ms. */
void avo_ui_post_button(avo_btn_t btn, avo_press_t press);
void avo_ui_post_wake(void);  /* wrist raise                */
void avo_ui_post_sleep(void); /* wrist lowered / cover      */
void avo_ui_post_double_tap(void); /* two knocks on the case          */
void avo_ui_post_flick(void);      /* quick wrist turn away and back   */

/* True while the UI wants full refresh speed (not in AOD/OFF). Board code
 * may use it to lower the CPU clock or skip the touch poll. */
bool avo_ui_is_awake(void);

#ifdef __cplusplus
}
#endif
