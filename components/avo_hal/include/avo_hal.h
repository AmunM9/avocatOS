/*
 * avocatOS hardware abstraction.
 *
 * The UI only talks to hardware through these functions. There are two
 * implementations: the real board (components/avo_board) and the desktop
 * simulator (sim/hal_sim.c). Every function is called from the LVGL task
 * unless stated otherwise, and must never block for more than a few ms.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "avo_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- time */
uint32_t avo_hal_millis(void);
/* Local wall-clock time (already shifted by the configured UTC offset). */
void avo_hal_time_now(avo_time_t *out);
void avo_hal_time_set_utc_offset(int16_t minutes);

typedef enum {
    AVO_TIME_SRC_INTERNAL = 0, /* RTC / build time, not verified        */
    AVO_TIME_SRC_NTP,          /* synced over Wi-Fi                     */
    AVO_TIME_SRC_PHONE,        /* Current Time Service of the iPhone    */
} avo_time_src_t;

avo_time_src_t avo_hal_time_source(void);
/* Seconds since the last successful sync, UINT32_MAX when never synced. */
uint32_t avo_hal_time_since_sync_s(void);
/* Time zone reported by the phone (minutes ahead of UTC). False if none. */
bool avo_hal_time_phone_offset(int16_t *minutes);

/* ---------------------------------------------------------------- display */
void avo_hal_display_brightness(uint8_t percent); /* 0 turns the panel off */

/* ---------------------------------------------------------------- power */
typedef struct {
    bool present;     /* a battery is connected                */
    bool charging;
    bool usb;         /* USB power present                     */
    int percent;      /* 0..100, -1 when unknown               */
    int millivolts;   /* battery voltage, 0 when unknown       */
} avo_battery_t;

void avo_hal_battery(avo_battery_t *out);
void avo_hal_restart(void);

/* ---------------------------------------------------------------- motion */
typedef struct {
    bool valid;
    float ax, ay, az; /* g */
} avo_accel_t;

void avo_hal_accel(avo_accel_t *out);

/* ---------------------------------------------------------------- radios */
typedef enum {
    AVO_LINK_OFF = 0,
    AVO_LINK_IDLE,        /* on, not connected                  */
    AVO_LINK_BUSY,        /* scanning / connecting / advertising */
    AVO_LINK_CONNECTED,
    AVO_LINK_ERROR,
} avo_link_t;

void avo_hal_bt_enable(bool on);
avo_link_t avo_hal_bt_state(void);
const char *avo_hal_bt_name(void);

#define AVO_WIFI_MAX_RESULTS 12

typedef struct {
    char ssid[AVO_WIFI_SSID_MAX];
    int8_t rssi;
    bool secure;
} avo_wifi_ap_t;

void avo_hal_wifi_enable(bool on);
avo_link_t avo_hal_wifi_state(void);
bool avo_hal_wifi_scan_start(void);
/* Copies up to `max` results; returns count, or -1 while the scan runs. */
int avo_hal_wifi_scan_results(avo_wifi_ap_t *out, int max);
bool avo_hal_wifi_connect(const char *ssid, const char *pass);
/* Connected network name and IPv4 as text; empty strings when offline. */
void avo_hal_wifi_info(char *ssid, size_t ssid_len, char *ip, size_t ip_len);

/* ---------------------------------------------------------------- iPhone (ANCS / AMS / CTS) */
typedef enum {
    AVO_PHONE_OFF = 0,      /* Bluetooth off                                  */
    AVO_PHONE_WAITING,      /* advertising, visible in iPhone Settings > BT   */
    AVO_PHONE_PAIRING,      /* connected, securing the link                   */
    AVO_PHONE_READY,        /* paired and services subscribed                 */
} avo_phone_state_t;

avo_phone_state_t avo_hal_phone_state(void);
bool avo_hal_phone_bonded(void);
void avo_hal_phone_forget(void);

#define AVO_NOTIF_MAX 20

typedef struct {
    uint32_t uid;
    uint8_t category;       /* AVO_ANCS_CAT_*                                 */
    char app[24];
    char title[AVO_ANCS_TITLE_MAX];
    char message[AVO_ANCS_MESSAGE_MAX];
    avo_time_t when;        /* local time it was received                     */
} avo_notif_t;

/* Increments on every change (added, updated, removed). */
uint32_t avo_hal_notif_version(void);
/* Newest first. Returns count. */
int avo_hal_notif_list(avo_notif_t *out, int max);
/* positive: accept call / open; negative: decline / clear on the iPhone too. */
void avo_hal_notif_action(uint32_t uid, bool positive);
void avo_hal_notif_dismiss_local(uint32_t uid);
/* Ask the iPhone for the whole message of `uid` (the list holds its first
 * AVO_ANCS_MESSAGE_MAX bytes); avo_hal_notif_full() is true once it arrived. */
void avo_hal_notif_request_full(uint32_t uid);
bool avo_hal_notif_full(uint32_t uid, char *out, size_t cap);
/* Returns a newly arrived, alert-worthy notification once (for the banner). */
bool avo_hal_notif_take_alert(avo_notif_t *out);

typedef struct {
    bool available;         /* AMS connected                                  */
    bool playing;
    char app[24];
    char title[64];
    char artist[64];
    char album[64];
    uint8_t volume;         /* 0..100                                         */
    uint32_t version;
} avo_media_t;

void avo_hal_media(avo_media_t *out);
bool avo_hal_media_command(uint8_t ams_command); /* AVO_AMS_CMD_* */

/* Album art of the current track (RGB565, little endian), fetched over Wi-Fi.
 * `pixels` stays valid until `version` changes. False when not available. */
typedef struct {
    const uint16_t *pixels;
    uint16_t w, h;
    uint32_t version;
} avo_artwork_t;

bool avo_hal_artwork(avo_artwork_t *out);

/* ---------------------------------------------------------------- system */
typedef struct {
    char chip[32];        /* "ESP32-S3 rev 0.2"      */
    char mac[18];         /* "aa:bb:cc:dd:ee:ff"     */
    uint32_t flash_mb;
    uint32_t psram_mb;
    uint32_t heap_internal_free;
    uint32_t heap_psram_free;
    uint32_t uptime_s;
    float cpu_temp_c;     /* NAN when unknown        */
    char idf_version[24];
} avo_sysinfo_t;

void avo_hal_sysinfo(avo_sysinfo_t *out);

/* ---------------------------------------------------------------- storage */
bool avo_hal_settings_load(avo_settings_t *out);
bool avo_hal_settings_save(const avo_settings_t *in);

/* ---------------------------------------------------------------- feedback */
/* Short UI click (only when Sonidos is on); there is no vibration motor. */
void avo_hal_click(void);
/* Plays at the configured volume, replacing any sound in progress. Looping
 * sounds (alarm, timer, ring) repeat until avo_hal_sound_stop(). */
void avo_hal_sound_play(avo_sound_t id);
void avo_hal_sound_stop(void);

/* ---------------------------------------------------------------- activity */
/* Today's totals from the step counter. */
void avo_hal_activity(avo_activity_t *out);

/* ---------------------------------------------------------------- alarms */
bool avo_hal_alarms_load(avo_alarms_t *out);
bool avo_hal_alarms_save(const avo_alarms_t *in);

/* ---------------------------------------------------------------- weather */
/* Last forecast fetched over Wi-Fi (Open-Meteo). False until one arrives. */
bool avo_hal_weather(avo_weather_t *out);

#ifdef __cplusplus
}
#endif
