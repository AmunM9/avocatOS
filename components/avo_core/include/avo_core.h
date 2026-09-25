/*
 * avocatOS core — platform independent logic (no LVGL, no ESP-IDF).
 * Everything here is unit-tested on the host (see tests/).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Honeycomb app grid                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    int16_t x;
    int16_t y;
} avo_pt_t;

/* Fixed point scale: 256 == 1.0 */
#define AVO_SCALE_ONE 256

typedef struct {
    int16_t full_radius;   /* distance (px) where bubbles keep full size   */
    int16_t fade_radius;   /* distance (px) where bubbles reach min_scale   */
    uint16_t min_scale;    /* scale at fade_radius and beyond (x/256)       */
} avo_hc_cfg_t;

/* Center of the slot `index` in a hexagonal spiral around (0,0).
 * Slot 0 is the center, slots 1..6 form ring 1, 7..18 ring 2, ...
 * `spacing` is the distance between neighbouring slot centers. */
avo_pt_t avo_hc_slot(int index, int spacing);

/* Fisheye scale for a bubble whose center is (dx,dy) away from the
 * viewport center. Smoothstep between full_radius and fade_radius. */
uint16_t avo_hc_scale(int dx, int dy, const avo_hc_cfg_t *cfg);

/* ------------------------------------------------------------------ */
/* Time formatting (Spanish)                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int year;   /* e.g. 2026 */
    int month;  /* 1..12     */
    int day;    /* 1..31     */
    int wday;   /* 0=Sunday  */
    int hour;   /* 0..23     */
    int min;    /* 0..59     */
    int sec;    /* 0..59     */
} avo_time_t;

/* "10:09" (24h) or "10:09" with hour in 1..12 (12h). Returns chars written. */
size_t avo_fmt_hm(char *out, size_t len, const avo_time_t *t, bool h24);
/* Hour only / minute only, zero padded: "09", "07" */
size_t avo_fmt_hour(char *out, size_t len, const avo_time_t *t, bool h24);
size_t avo_fmt_min(char *out, size_t len, const avo_time_t *t);
/* "MIÉ 24" */
size_t avo_fmt_wday_day(char *out, size_t len, const avo_time_t *t);
/* "miércoles, 24 de septiembre" */
size_t avo_fmt_long_date(char *out, size_t len, const avo_time_t *t);
/* "a. m." / "p. m." for 12h mode */
const char *avo_fmt_ampm(const avo_time_t *t);
/* "01:02:03" style duration for stopwatch (centiseconds precision) */
size_t avo_fmt_stopwatch(char *out, size_t len, uint32_t elapsed_ms);

/* ------------------------------------------------------------------ */
/* Display power state machine                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    AVO_PWR_ACTIVE = 0,
    AVO_PWR_DIM,
    AVO_PWR_AOD,
    AVO_PWR_OFF,
} avo_pwr_state_t;

typedef struct {
    uint32_t dim_after_ms;   /* inactivity before dimming                 */
    uint32_t sleep_after_ms; /* inactivity before AOD/OFF                 */
    bool aod_enabled;        /* AOD instead of OFF when sleeping          */
} avo_pwr_cfg_t;

typedef struct {
    avo_pwr_cfg_t cfg;
    avo_pwr_state_t state;
    uint32_t last_activity_ms;
} avo_pwr_t;

void avo_pwr_init(avo_pwr_t *p, const avo_pwr_cfg_t *cfg, uint32_t now_ms);
/* User interaction (touch, button, wrist raise). Returns new state. */
avo_pwr_state_t avo_pwr_activity(avo_pwr_t *p, uint32_t now_ms);
/* Periodic tick. Returns new state. */
avo_pwr_state_t avo_pwr_tick(avo_pwr_t *p, uint32_t now_ms);
/* Force sleep (e.g. PWR button long press / wrist down) */
avo_pwr_state_t avo_pwr_sleep(avo_pwr_t *p);

/* ------------------------------------------------------------------ */
/* Battery                                                             */
/* ------------------------------------------------------------------ */

/* Li-ion open-circuit estimate, clamped to 0..100. */
int avo_batt_percent_from_mv(int millivolts);

/* ------------------------------------------------------------------ */
/* Settings                                                            */
/* ------------------------------------------------------------------ */

#define AVO_SETTINGS_VERSION 1
#define AVO_WIFI_SSID_MAX 33
#define AVO_WIFI_PASS_MAX 65

typedef enum {
    AVO_THEME_CLEAN = 0,
    AVO_THEME_AVOCADO = 1,
} avo_theme_mode_t;

typedef struct {
    uint16_t version;
    uint8_t brightness;        /* 5..100 %                               */
    uint8_t theme;             /* avo_theme_mode_t                       */
    uint8_t face;              /* index of the active watch face         */
    bool h24;
    bool aod;
    bool raise_to_wake;
    bool sounds;
    bool bluetooth;
    bool wifi;
    bool show_fps;
    uint16_t screen_timeout_s; /* 5..120 s until sleep                   */
    int16_t utc_offset_min;    /* -720..840                              */
    char wifi_ssid[AVO_WIFI_SSID_MAX];
    char wifi_pass[AVO_WIFI_PASS_MAX];
} avo_settings_t;

void avo_settings_defaults(avo_settings_t *s);
/* Clamp every field into its valid range. Returns true if anything changed. */
bool avo_settings_sanitize(avo_settings_t *s, uint8_t face_count);

/* ------------------------------------------------------------------ */
/* Swipe recognizer (fed with raw touch samples)                       */
/* ------------------------------------------------------------------ */

typedef enum {
    AVO_SWIPE_NONE = 0,
    AVO_SWIPE_UP,
    AVO_SWIPE_DOWN,
    AVO_SWIPE_LEFT,
    AVO_SWIPE_RIGHT,
} avo_swipe_dir_t;

typedef struct {
    avo_swipe_dir_t dir;
    int16_t start_x, start_y;
    int16_t dx, dy;
    uint32_t duration_ms;
} avo_swipe_t;

typedef struct {
    bool down;
    int16_t x0, y0, x, y;
    uint32_t t0;
} avo_swipe_tracker_t;

void avo_swipe_reset(avo_swipe_tracker_t *t);
/* Feed one touch sample. Returns true (and fills `out`) when a finger lift
 * completes a swipe. Tolerates diagonal drift and slow drags. */
bool avo_swipe_feed(avo_swipe_tracker_t *t, bool pressed, int16_t x, int16_t y, uint32_t now_ms, avo_swipe_t *out);

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

/* Remove invisible emoji modifiers in place (variation selectors, zero
 * width joiners, skin tones, tag characters, keycap mark) so a single-
 * colour emoji font can draw the base glyph. Also drops broken UTF-8. */
void avo_text_clean(char *s);

/* Percent-encode for a URL query value (spaces become '+'). Returns false
 * if it did not fit. */
bool avo_url_encode(const char *src, char *dst, size_t len);
/* From an iTunes Search API JSON reply, the first "artworkUrl100" rewritten
 * to `px` x `px` pixels. Returns false if the reply has no artwork. */
bool avo_itunes_artwork_url(const char *json, int px, char *out, size_t len);

/* ------------------------------------------------------------------ */
/* Calendar                                                            */
/* ------------------------------------------------------------------ */

/* Seconds since 1970-01-01 for a wall-clock time `offset_min` ahead of UTC. */
int64_t avo_time_to_epoch(const avo_time_t *local, int16_t offset_min);
/* 0 = Sunday ... 6 = Saturday */
int avo_weekday(int year, int month, int day);

/* ------------------------------------------------------------------ */
/* Apple Notification Center Service (ANCS)                            */
/* ------------------------------------------------------------------ */

enum { AVO_ANCS_ADDED = 0, AVO_ANCS_MODIFIED = 1, AVO_ANCS_REMOVED = 2 };
enum {
    AVO_ANCS_CAT_OTHER = 0, AVO_ANCS_CAT_INCOMING_CALL = 1, AVO_ANCS_CAT_MISSED_CALL = 2,
    AVO_ANCS_CAT_VOICEMAIL = 3, AVO_ANCS_CAT_SOCIAL = 4, AVO_ANCS_CAT_SCHEDULE = 5,
    AVO_ANCS_CAT_EMAIL = 6, AVO_ANCS_CAT_NEWS = 7, AVO_ANCS_CAT_HEALTH = 8,
    AVO_ANCS_CAT_BUSINESS = 9, AVO_ANCS_CAT_LOCATION = 10, AVO_ANCS_CAT_ENTERTAINMENT = 11,
};
#define AVO_ANCS_FLAG_SILENT 0x01
#define AVO_ANCS_FLAG_PRE_EXISTING 0x04

#define AVO_ANCS_APP_ID_MAX 48
#define AVO_ANCS_TITLE_MAX 64
#define AVO_ANCS_MESSAGE_MAX 160
#define AVO_ANCS_DATE_MAX 16

typedef struct {
    uint8_t event_id;
    uint8_t flags;
    uint8_t category;
    uint8_t category_count;
    uint32_t uid;
} avo_ancs_source_t;

typedef struct {
    uint32_t uid;
    char app_id[AVO_ANCS_APP_ID_MAX];
    char title[AVO_ANCS_TITLE_MAX];
    char message[AVO_ANCS_MESSAGE_MAX];
    char date[AVO_ANCS_DATE_MAX]; /* "yyyyMMdd'T'HHmmSS" */
} avo_ancs_attrs_t;

bool avo_ancs_parse_source(const uint8_t *d, size_t n, avo_ancs_source_t *out);
/* Control Point request for app id, title, message and date. Returns length. */
size_t avo_ancs_build_get_attrs(uint32_t uid, uint8_t *out, size_t cap);
/* Parse a (possibly fragmented) Data Source response accumulated in `d`.
 * Returns bytes consumed when complete, 0 when more data is needed,
 * -1 when the data is malformed. */
int avo_ancs_parse_attrs(const uint8_t *d, size_t n, avo_ancs_attrs_t *out);
/* Perform Notification Action: positive (accept/open) or negative (decline/clear). */
size_t avo_ancs_build_action(uint32_t uid, bool positive, uint8_t *out, size_t cap);
/* Friendly name for common bundle ids, else the last part of the id. */
void avo_app_display_name(const char *bundle_id, char *out, size_t len);

/* ------------------------------------------------------------------ */
/* Apple Media Service (AMS)                                           */
/* ------------------------------------------------------------------ */

enum { AVO_AMS_ENTITY_PLAYER = 0, AVO_AMS_ENTITY_QUEUE = 1, AVO_AMS_ENTITY_TRACK = 2 };
enum { AVO_AMS_PLAYER_NAME = 0, AVO_AMS_PLAYER_PLAYBACK = 1, AVO_AMS_PLAYER_VOLUME = 2 };
enum { AVO_AMS_TRACK_ARTIST = 0, AVO_AMS_TRACK_ALBUM = 1, AVO_AMS_TRACK_TITLE = 2, AVO_AMS_TRACK_DURATION = 3 };
enum {
    AVO_AMS_CMD_PLAY = 0, AVO_AMS_CMD_PAUSE = 1, AVO_AMS_CMD_TOGGLE = 2, AVO_AMS_CMD_NEXT = 3,
    AVO_AMS_CMD_PREV = 4, AVO_AMS_CMD_VOL_UP = 5, AVO_AMS_CMD_VOL_DOWN = 6,
};

typedef struct {
    uint8_t entity;
    uint8_t attr;
    uint8_t flags;
    char value[64];
} avo_ams_update_t;

bool avo_ams_parse_update(const uint8_t *d, size_t n, avo_ams_update_t *out);
/* PlaybackInfo "state,rate,elapsed": 0 paused, 1 playing, 2 rewind, 3 fast-forward, -1 invalid. */
int avo_ams_playback_state(const char *value);

/* ------------------------------------------------------------------ */
/* Current Time Service (CTS)                                          */
/* ------------------------------------------------------------------ */

bool avo_cts_parse_time(const uint8_t *d, size_t n, avo_time_t *out);
/* Local Time Information: time zone + DST, as minutes ahead of UTC. */
bool avo_cts_parse_local_info(const uint8_t *d, size_t n, int16_t *offset_min);

#ifdef __cplusplus
}
#endif
