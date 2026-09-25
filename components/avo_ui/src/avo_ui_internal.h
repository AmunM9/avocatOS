/* avocatOS UI internals shared by the shell, faces, panels and apps. */
#pragma once

#include "lvgl.h"
#include "avo_core.h"
#include "avo_hal.h"
#include "avo_ui.h"
#include "avo_theme.h"
#include "avo_symbols.h"

/* ---------------------------------------------------------------- settings */
avo_settings_t *avo_settings(void);
/* Persist settings and apply side effects (brightness, theme, radios...). */
void avo_settings_commit(void);

/* ---------------------------------------------------------------- apps */
typedef struct avo_app {
    const char *name;
    const char *symbol;
    avo_hue_t hue;
    void (*build)(lv_obj_t *screen); /* fill a fresh black screen          */
    void (*leave)(void);             /* optional: stop timers before delete */
} avo_app_t;

extern const avo_app_t *const AVO_APPS[];
extern const int AVO_APP_COUNT;

extern const avo_app_t AVO_APP_SETTINGS;
extern const avo_app_t AVO_APP_MUSIC;
extern const avo_app_t AVO_APP_STOPWATCH;
extern const avo_app_t AVO_APP_TIMER;
extern const avo_app_t AVO_APP_FLASHLIGHT;
extern const avo_app_t AVO_APP_LEVEL;
extern const avo_app_t AVO_APP_FACES;
extern const avo_app_t AVO_APP_ALARMS;
extern const avo_app_t AVO_APP_ACTIVITY;
extern const avo_app_t AVO_APP_WEATHER;

/* ---------------------------------------------------------------- navigation */
typedef enum {
    AVO_ROUTE_FACE = 0,
    AVO_ROUTE_GRID,
    AVO_ROUTE_APP,
    AVO_ROUTE_SUB,
    AVO_ROUTE_STACK,
    AVO_ROUTE_NOTIF,
    AVO_ROUTE_CC,
    AVO_ROUTE_AOD,
} avo_route_t;

avo_route_t avo_nav_route(void);
bool avo_nav_locked(void);                 /* a transition is running          */
/* A sheet/overlay owns the screen: swipes do not navigate. */
void avo_nav_set_modal(bool on);
bool avo_nav_modal(void);
/* The scrollable list of the current screen, so a swipe only closes the
 * screen when the list is at its edge. Cleared automatically on delete. */
void avo_nav_set_scroller(lv_obj_t *scroller);
lv_obj_t *avo_nav_scroller(void);
void avo_nav_wake(void);                   /* user-visible event: wake screen  */
void avo_nav_keep_awake(bool on);          /* e.g. while receiving an update   */
void avo_nav_face(void);              /* back to the watch face            */
void avo_nav_grid(void);              /* honeycomb app grid                */
void avo_nav_app(const avo_app_t *app);
void avo_nav_back(void);              /* one level up                      */
void avo_nav_stack(void);             /* Smart Stack (swipe up on face)    */
void avo_nav_notifications(void);     /* swipe down on face                */
void avo_nav_control_center(void);    /* PWR button                        */
void avo_nav_reload(void);            /* rebuild the current screen        */
void avo_nav_face_select(int index);  /* jump to a face without animation  */
/* Sub-page inside an app (e.g. Ajustes > Wi-Fi); swipe right goes back. */
void avo_nav_push(void (*build)(lv_obj_t *screen), void (*leave)(void));

/* ---------------------------------------------------------------- gestures */
/* Wraps the pointer read callback with the swipe recognizer. */
void avo_gesture_install(void);
/* True if the last touch (< 1 s ago) landed on a button or other control. */
bool avo_gesture_recent_control_press(void);
/* Ajustes > Gestos test area: called on every double tap / flick. */
void avo_motion_set_probe(void (*probe)(bool flick));

/* ---------------------------------------------------------------- alarms */
void avo_alarms_init(void);                /* loads them                       */
avo_alarms_t *avo_alarms(void);
void avo_alarms_commit(void);              /* persist after editing            */
void avo_alarms_tick(const avo_time_t *t); /* 1 Hz: ring, snooze               */
/* "07:30" of the next alarm (or of the snoozed one). False if none. */
bool avo_alarms_next_text(char *out, size_t len, bool *snoozed);
/* Finished countdown: sound + alert (Detener / Repetir). */
void avo_alert_timer_done(uint32_t minutes, void (*repeat)(void));

/* ---------------------------------------------------------------- glanceable data */
const char *avo_wx_symbol(int wmo_code, bool is_day);

/* ---------------------------------------------------------------- overlays (lv_layer_top) */
void avo_overlay_banner(const avo_notif_t *n);
void avo_overlay_call(const avo_notif_t *n);
void avo_overlay_charging(const avo_battery_t *b);
bool avo_overlay_active(void);
bool avo_overlay_dismiss(void);            /* true if something was closed     */
void avo_overlays_tick(void);              /* 4 Hz: timeouts, ended calls      */

/* Full-screen alert with its (looping) sound: a ringing alarm, a finished
 * timer. Strings are copied; callbacks run after the overlay closed. */
typedef struct {
    const char *title, *big, *caption;     /* "Alarma", "07:30", "Entre semana" */
    const char *symbol;
    avo_hue_t hue;
    const char *primary;                   /* bottom button, e.g. "Detener"     */
    const char *secondary;                 /* optional, e.g. "Posponer"         */
    void (*on_primary)(void);
    void (*on_secondary)(void);
    void (*on_double_tap)(void);
    void (*on_dismiss)(void);              /* button, swipe, flick or timeout   */
    avo_sound_t sound;
    uint32_t timeout_ms;                   /* 0 = never                         */
} avo_alert_t;

void avo_overlay_alert(const avo_alert_t *a);
bool avo_overlay_keeps_awake(void);        /* call or alert on screen          */
bool avo_overlay_double_tap(void);         /* main action; false if nothing    */
bool avo_overlay_flick(void);              /* dismiss; false if nothing        */
/* Short confirmation pill at the top ("Alarma pospuesta…"), ~2 s. */
void avo_toast(const char *symbol, const char *text);
/* ---------------------------------------------------------------- faces */
#define AVO_FACE_MAX 6
int avo_faces_count(void);                   /* depends on theme            */
const char *avo_face_name(int index);
lv_obj_t *avo_face_create(int index, lv_obj_t *parent);
void avo_faces_tick(const avo_time_t *t);    /* 1 Hz update of live faces   */
void avo_faces_forget(void);                 /* parent screen was deleted   */
int avo_face_index_by_name(const char *name);  /* -1 if not in this theme     */
/* shared by avo_faces.c and avo_faces_extra.c */
lv_obj_t *avo_face_root(lv_obj_t *parent);
lv_obj_t *avo_face_retrato_create(lv_obj_t *parent, const avo_time_t *t);
lv_obj_t *avo_face_orbit_create(lv_obj_t *parent, const avo_time_t *t);
void avo_faces_extra_tick(const avo_time_t *t);
void avo_faces_extra_forget(void);

/* ---------------------------------------------------------------- panels */
void avo_grid_build(lv_obj_t *screen);
void avo_cc_build(lv_obj_t *screen);
void avo_stack_build(lv_obj_t *screen);
void avo_notif_build(lv_obj_t *screen);
void avo_notif_open_detail(uint32_t uid);  /* from a banner tap                */
avo_hue_t avo_notif_hue(uint8_t ancs_category);
const char *avo_notif_symbol(uint8_t ancs_category);
void avo_aod_build(lv_obj_t *screen);
void avo_aod_tick(const avo_time_t *t);

/* ---------------------------------------------------------------- media */
void avo_media_tick(void);                 /* remembers when music last played */
bool avo_media_recent(void);               /* playing, or paused < 10 min ago  */
/* Cover, track, transport and volume, filling `parent` from y = top down. */
lv_obj_t *avo_now_playing_create(lv_obj_t *parent, int32_t top);

/* ---------------------------------------------------------------- misc */
/* Current time label helper for app headers (top-right, like watchOS). */
lv_obj_t *avo_header_clock(lv_obj_t *screen);
void avo_header_clock_refresh(lv_obj_t *label, const avo_time_t *t);
/* Stopwatch state is global so the Smart Stack can show it. */
bool avo_stopwatch_running(void);
uint32_t avo_stopwatch_elapsed(void);
