/* Parsers and request builders for Apple's BLE services (ANCS, AMS, CTS).
 * Pure functions: the radio code lives in components/avo_board. */
#include <ctype.h>
#include <string.h>
#include "avo_core.h"

#define ANCS_CMD_GET_NOTIF_ATTRS 0
#define ANCS_CMD_PERFORM_ACTION 2
#define ANCS_ATTR_APP_ID 0
#define ANCS_ATTR_TITLE 1
#define ANCS_ATTR_MESSAGE 3
#define ANCS_ATTR_DATE 5
#define ANCS_ATTR_COUNT 4
#define ANCS_HEADER 5 /* command + uid */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Copy up to cap-1 bytes and never leave half a UTF-8 character. */
static void copy_utf8(char *dst, size_t cap, const uint8_t *src, size_t len)
{
    size_t n = len < cap - 1 ? len : cap - 1;
    if (n < len) {
        while (n > 0 && (src[n] & 0xC0) == 0x80) {
            n--; /* src[n] continues a character that would be cut */
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ================================================================= ANCS */

bool avo_ancs_parse_source(const uint8_t *d, size_t n, avo_ancs_source_t *out)
{
    if (n < 8 || d[0] > AVO_ANCS_REMOVED) {
        return false;
    }
    *out = (avo_ancs_source_t){
        .event_id = d[0], .flags = d[1], .category = d[2], .category_count = d[3], .uid = rd32(d + 4),
    };
    return true;
}

size_t avo_ancs_build_get_attrs(uint32_t uid, uint8_t *out, size_t cap)
{
    const size_t need = 13;
    if (cap < need) {
        return 0;
    }
    size_t i = 0;
    out[i++] = ANCS_CMD_GET_NOTIF_ATTRS;
    wr32(out + i, uid);
    i += 4;
    out[i++] = ANCS_ATTR_APP_ID;
    out[i++] = ANCS_ATTR_TITLE;
    out[i++] = AVO_ANCS_TITLE_MAX - 1;
    out[i++] = 0;
    out[i++] = ANCS_ATTR_MESSAGE;
    out[i++] = AVO_ANCS_MESSAGE_MAX - 1;
    out[i++] = 0;
    out[i++] = ANCS_ATTR_DATE;
    return i;
}

int avo_ancs_parse_attrs(const uint8_t *d, size_t n, avo_ancs_attrs_t *out)
{
    if (n < 1) {
        return 0;
    }
    if (d[0] != ANCS_CMD_GET_NOTIF_ATTRS) {
        return -1;
    }
    if (n < ANCS_HEADER) {
        return 0;
    }
    memset(out, 0, sizeof *out);
    out->uid = rd32(d + 1);
    size_t pos = ANCS_HEADER;
    for (int k = 0; k < ANCS_ATTR_COUNT; k++) {
        if (pos + 3 > n) {
            return 0;
        }
        uint8_t id = d[pos];
        size_t len = (size_t)d[pos + 1] | (size_t)d[pos + 2] << 8;
        pos += 3;
        if (pos + len > n) {
            return 0;
        }
        const uint8_t *v = d + pos;
        switch (id) {
        case ANCS_ATTR_APP_ID: copy_utf8(out->app_id, sizeof out->app_id, v, len); break;
        case ANCS_ATTR_TITLE: copy_utf8(out->title, sizeof out->title, v, len); break;
        case ANCS_ATTR_MESSAGE: copy_utf8(out->message, sizeof out->message, v, len); break;
        case ANCS_ATTR_DATE: copy_utf8(out->date, sizeof out->date, v, len); break;
        default: return -1;
        }
        pos += len;
    }
    return (int)pos;
}

size_t avo_ancs_build_action(uint32_t uid, bool positive, uint8_t *out, size_t cap)
{
    if (cap < 6) {
        return 0;
    }
    out[0] = ANCS_CMD_PERFORM_ACTION;
    wr32(out + 1, uid);
    out[5] = positive ? 0 : 1;
    return 6;
}

static const struct {
    const char *id;
    const char *name;
} APP_NAMES[] = {
    { "com.apple.MobileSMS", "Mensajes" },    { "com.apple.mobilephone", "Teléfono" },
    { "com.apple.mobilemail", "Mail" },        { "com.apple.mobilecal", "Calendario" },
    { "com.apple.reminders", "Recordatorios" }, { "com.apple.facetime", "FaceTime" },
    { "com.apple.Music", "Música" },           { "com.apple.Health", "Salud" },
    { "com.apple.findmy", "Encontrar" },       { "com.apple.mobileslideshow", "Fotos" },
    { "net.whatsapp.WhatsApp", "WhatsApp" },   { "net.whatsapp.WhatsAppSMB", "WhatsApp Business" },
    { "ph.telegra.Telegraph", "Telegram" },    { "com.burbn.instagram", "Instagram" },
    { "com.facebook.Messenger", "Messenger" }, { "com.facebook.Facebook", "Facebook" },
    { "com.google.Gmail", "Gmail" },           { "com.spotify.client", "Spotify" },
    { "com.atebits.Tweetie2", "X" },           { "com.tinyspeck.chatlyio", "Slack" },
    { "com.zhiliaoapp.musically", "TikTok" },  { "com.google.ios.youtube", "YouTube" },
    { "com.linkedin.LinkedIn", "LinkedIn" },   { "com.microsoft.skype.teams", "Teams" },
    { "com.netflix.Netflix", "Netflix" },      { "com.ubercab.UberClient", "Uber" },
};

void avo_app_display_name(const char *bundle_id, char *out, size_t len)
{
    if (!len) {
        return;
    }
    if (!bundle_id || !bundle_id[0]) {
        copy_utf8(out, len, (const uint8_t *)"iPhone", 6);
        return;
    }
    for (size_t i = 0; i < sizeof APP_NAMES / sizeof APP_NAMES[0]; i++) {
        if (strcmp(bundle_id, APP_NAMES[i].id) == 0) {
            copy_utf8(out, len, (const uint8_t *)APP_NAMES[i].name, strlen(APP_NAMES[i].name));
            return;
        }
    }
    const char *last = strrchr(bundle_id, '.');
    last = last ? last + 1 : bundle_id;
    copy_utf8(out, len, (const uint8_t *)last, strlen(last));
    out[0] = (char)toupper((unsigned char)out[0]);
}

/* ================================================================= AMS */

bool avo_ams_parse_update(const uint8_t *d, size_t n, avo_ams_update_t *out)
{
    if (n < 3) {
        return false;
    }
    out->entity = d[0];
    out->attr = d[1];
    out->flags = d[2];
    copy_utf8(out->value, sizeof out->value, d + 3, n - 3);
    return true;
}

int avo_ams_playback_state(const char *value)
{
    if (!value || value[0] < '0' || value[0] > '3' || (value[1] != ',' && value[1] != '\0')) {
        return -1;
    }
    return value[0] - '0';
}

/* ================================================================= CTS */

bool avo_cts_parse_time(const uint8_t *d, size_t n, avo_time_t *out)
{
    if (n < 7) {
        return false;
    }
    avo_time_t t = {
        .year = d[0] | d[1] << 8, .month = d[2], .day = d[3], .hour = d[4], .min = d[5], .sec = d[6],
    };
    if (t.year < 2000 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 || t.hour > 23 ||
        t.min > 59 || t.sec > 59) {
        return false;
    }
    t.wday = avo_weekday(t.year, t.month, t.day);
    *out = t;
    return true;
}

bool avo_cts_parse_local_info(const uint8_t *d, size_t n, int16_t *offset_min)
{
    if (n < 2 || (int8_t)d[0] == -128) {
        return false;
    }
    int dst = 0;
    switch (d[1]) {
    case 2: dst = 30; break;
    case 4: dst = 60; break;
    case 8: dst = 120; break;
    default: break; /* 0 = standard time, 255 = unknown */
    }
    *offset_min = (int16_t)((int8_t)d[0] * 15 + dst);
    return true;
}
