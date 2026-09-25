/* Pages shared between the two Ajustes source files. */
#pragma once

#include "lvgl.h"
#include "avo_theme.h"


lv_obj_t *avo_subpage_create(lv_obj_t *screen, const char *title, avo_hue_t hue);
/* Non-interactive key/value row; returns the value label. */
lv_obj_t *avo_info_row(lv_obj_t *parent, const char *key, const char *value);
lv_obj_t *avo_note(lv_obj_t *parent, const char *text);

void avo_settings_bt_page(lv_obj_t *screen);
void avo_settings_bt_leave(void);
void avo_settings_wifi_page(lv_obj_t *screen);
void avo_settings_wifi_leave(void);
void avo_settings_sound_page(lv_obj_t *screen);
void avo_settings_music_page(lv_obj_t *screen);
void avo_settings_gestures_page(lv_obj_t *screen);
void avo_settings_gestures_leave(void);
void avo_settings_transfer_page(lv_obj_t *screen);
void avo_settings_transfer_leave(void);
