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
/* Spanish digit grouping: 6482 -> "6.482", 12500000 -> "12.500.000" */
size_t avo_fmt_thousands(char *out, size_t len, uint32_t n);

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

#define AVO_SETTINGS_VERSION 2
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
    /* ---- version 2 (appended, so a version 1 blob is a prefix) */
    uint8_t volume;            /* 0..100 %                               */
    bool double_tap;
    bool wrist_flick;
    bool weather;              /* fetch the forecast over Wi-Fi          */
    uint16_t step_goal;        /* 1000..50000                            */
} avo_settings_t;

/* Size of a stored version 1 blob (every field before `volume`). */
#define AVO_SETTINGS_V1_SIZE ((offsetof(avo_settings_t, volume) + 1u) & ~1u)

void avo_settings_defaults(avo_settings_t *s);
/* Clamp every field into its valid range. Returns true if anything changed. */
bool avo_settings_sanitize(avo_settings_t *s, uint8_t face_count);
/* Accept a blob of `loaded_len` bytes read into `s`: the current version,
 * or a version 1 blob whose new fields get their defaults. */
bool avo_settings_upgrade(avo_settings_t *s, size_t loaded_len);

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

/* ------------------------------------------------------------------ */
/* Sounds (synthesized, no audio files)                                */
/* ------------------------------------------------------------------ */

#define AVO_SYNTH_RATE 16000

typedef enum {
    AVO_SOUND_CLICK = 0,  /* UI tick                         */
    AVO_SOUND_NOTIFY,     /* notification banner             */
    AVO_SOUND_CHARGE,     /* USB plugged in                  */
    AVO_SOUND_ALARM,      /* loops until stopped             */
    AVO_SOUND_TIMER,      /* loops until stopped             */
    AVO_SOUND_RING,       /* incoming call, loops            */
    AVO_SOUND_COUNT,
} avo_sound_t;

typedef struct {
    uint16_t hz;          /* 0 = silence */
    uint16_t ms;
} avo_note_t;

typedef struct {
    const avo_note_t *notes;
    uint8_t count;
    bool loop;
    uint8_t idx;          /* current note                        */
    uint32_t pos;         /* sample inside the current note      */
    uint32_t phase;       /* 16.16 fixed point index in the sine */
    int32_t gain;         /* peak amplitude, 0..32767            */
} avo_synth_t;

bool avo_sound_loops(avo_sound_t id);
/* Length of one pass of the sound. */
uint32_t avo_sound_duration_ms(avo_sound_t id);
/* volume 0..100 (perceptual curve). */
void avo_synth_start(avo_synth_t *s, avo_sound_t id, uint8_t volume);
/* Mono 16-bit samples at AVO_SYNTH_RATE. Returns the frames written; fewer
 * than `frames` (the rest zeroed) once a non-looping sound ends. */
size_t avo_synth_render(avo_synth_t *s, int16_t *out, size_t frames);
bool avo_synth_done(const avo_synth_t *s);

/* ------------------------------------------------------------------ */
/* Motion: double tap, wrist flick and steps from the IMU at 100 Hz    */
/* ------------------------------------------------------------------ */

#define AVO_MOTION_HZ 100

typedef struct {
    float prev[3];
    bool primed;
    uint32_t n;            /* samples seen                         */
    uint16_t calm;         /* consecutive quiet samples            */
    uint8_t state;         /* 0 idle, 1 first tap, 2 second tap    */
    uint32_t first_at, last_peak_at, cooldown_until;
    bool high;             /* previous sample was above threshold  */
} avo_tap_t;

/* Accelerometer sample in g. True when a double tap completed. */
bool avo_tap_feed(avo_tap_t *t, const float a[3]);

typedef struct {
    uint32_t n;
    uint16_t calm;         /* consecutive samples with little rotation */
    uint8_t state;         /* 0 idle, 1 out, 2 back                    */
    uint8_t axis;
    int8_t sign;
    uint32_t start_at, back_at, cooldown_until;
    uint32_t armed_at;     /* a still wrist started moving here         */
} avo_flick_t;

/* Gyroscope sample in degrees/s. True when a wrist flick (quick turn away
 * and back from a still wrist) completed. */
bool avo_flick_feed(avo_flick_t *f, const float g[3]);

typedef struct {
    uint32_t n;
    float lp;                 /* smoothed acceleration magnitude         */
    float win_max, win_min;   /* extremes of the current window          */
    float thresh, span;       /* from the previous window                */
    bool above;
    uint32_t last_step_at;
    uint8_t pending;          /* regular steps waiting to be confirmed   */
    bool counting;
} avo_steps_t;

void avo_steps_reset(avo_steps_t *s);
/* Accelerometer sample in g. Returns steps to add now (0 most of the time;
 * the steps held back while checking the rhythm are released together). */
uint32_t avo_steps_feed(avo_steps_t *s, const float a[3]);

/* ------------------------------------------------------------------ */
/* Daily activity: steps, exercise minutes, stand hours                */
/* ------------------------------------------------------------------ */

#define AVO_EXERCISE_GOAL_MIN 30
#define AVO_STAND_GOAL_H 12

typedef struct {
    uint32_t day;           /* yyyymmdd                              */
    uint32_t steps;
    uint16_t exercise_min;
    uint32_t stand_mask;    /* bit h = stood during hour h           */
    int16_t minute;         /* hour * 60 + min being accumulated     */
    uint16_t minute_steps;
} avo_activity_t;

void avo_activity_reset(avo_activity_t *a, const avo_time_t *t);
/* Add `steps` taken now. Rolls over at midnight and closes each minute. */
void avo_activity_add(avo_activity_t *a, const avo_time_t *t, uint32_t steps);
int avo_activity_stand_hours(const avo_activity_t *a);

/* ------------------------------------------------------------------ */
/* Alarms                                                              */
/* ------------------------------------------------------------------ */

#define AVO_ALARM_MAX 8
#define AVO_ALARMS_VERSION 1
#define AVO_DAYS_ALL 0x7F        /* bit 0 = Sunday ... bit 6 = Saturday */
#define AVO_DAYS_WEEKDAYS 0x3E
#define AVO_DAYS_WEEKEND 0x41

typedef struct {
    uint8_t hour, min;
    uint8_t days;            /* 0 = once                             */
    bool enabled;
} avo_alarm_t;

typedef struct {
    uint16_t version;
    uint8_t count;
    avo_alarm_t list[AVO_ALARM_MAX];
} avo_alarms_t;

void avo_alarms_defaults(avo_alarms_t *s);
bool avo_alarms_sanitize(avo_alarms_t *s);
/* True when the alarm rings at this hh:mm. */
bool avo_alarm_due(const avo_alarm_t *a, const avo_time_t *t);
/* Enabled alarm that rings next (strictly after now); -1 if none. */
int avo_alarms_next(const avo_alarms_t *s, const avo_time_t *now, int *minutes_until);
/* "Una vez", "Todos los días", "Entre semana", "Fines de semana", "L X V" */
size_t avo_fmt_alarm_days(char *out, size_t len, uint8_t days);

/* ------------------------------------------------------------------ */
/* Weather (Open-Meteo) and location (ipwho.is)                        */
/* ------------------------------------------------------------------ */

#define AVO_WX_DAYS 4

typedef enum {
    AVO_WX_CLEAR = 0, AVO_WX_PARTLY, AVO_WX_CLOUDY, AVO_WX_FOG,
    AVO_WX_DRIZZLE, AVO_WX_RAIN, AVO_WX_SNOW, AVO_WX_STORM,
} avo_wx_kind_t;

typedef struct {
    bool valid;
    float temp;
    int code;               /* WMO weather code        */
    bool is_day;
    int days;
    int day_code[AVO_WX_DAYS];
    float day_max[AVO_WX_DAYS], day_min[AVO_WX_DAYS];
    char city[32];
    uint32_t updated_ms;    /* avo_hal_millis() of the fetch */
} avo_weather_t;

/* First value of "key" at or after `json`; skips whitespace. */
const char *avo_json_find(const char *json, const char *key);
bool avo_json_number(const char *json, const char *key, double *out);
/* Decodes \" \\ \/ and \uXXXX (BMP) into UTF-8. */
bool avo_json_string(const char *json, const char *key, char *out, size_t len);
/* Open-Meteo reply with current=temperature_2m,weather_code,is_day and
 * daily=weather_code,temperature_2m_max,temperature_2m_min. */
bool avo_weather_parse(const char *json, avo_weather_t *out);
/* ipwho.is reply: coordinates and city. */
bool avo_geo_parse(const char *json, double *lat, double *lon, char *city, size_t len);
avo_wx_kind_t avo_wmo_kind(int code);
const char *avo_wmo_text(int code);  /* "Despejado", "Llovizna", ... */

#ifdef __cplusplus
}
#endif
