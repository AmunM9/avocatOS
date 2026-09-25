/*
 * Minimal JSON field extraction for the two small, fixed replies avocatOS
 * reads (Open-Meteo forecast, ipwho.is location). A full parser would cost
 * RAM for a tree; these replies are flat enough to scan.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avo_core.h"

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }
    return p;
}

const char *avo_json_find(const char *json, const char *key)
{
    if (!json || !key) {
        return NULL;
    }
    size_t klen = strlen(key);
    for (const char *p = strchr(json, '"'); p; p = strchr(p + 1, '"')) {
        if (strncmp(p + 1, key, klen) == 0 && p[klen + 1] == '"') {
            const char *v = skip_ws(p + klen + 2);
            if (*v == ':') {
                return skip_ws(v + 1);
            }
        }
    }
    return NULL;
}

static bool parse_number(const char *v, double *out)
{
    if (!v || !(*v == '-' || (*v >= '0' && *v <= '9'))) {
        return false;
    }
    char *end;
    double d = strtod(v, &end);
    if (end == v) {
        return false;
    }
    *out = d;
    return true;
}

bool avo_json_number(const char *json, const char *key, double *out)
{
    return parse_number(avo_json_find(json, key), out);
}

static size_t put_utf8(char *out, size_t w, size_t len, unsigned cp)
{
    char b[3];
    size_t n;
    if (cp < 0x80) {
        b[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    }
    if (w + n >= len) {
        return 0; /* never split a character */
    }
    memcpy(out + w, b, n);
    return n;
}

bool avo_json_string(const char *json, const char *key, char *out, size_t len)
{
    const char *v = avo_json_find(json, key);
    if (!v || *v != '"' || len == 0) {
        return false;
    }
    size_t w = 0;
    for (const char *p = v + 1; *p && *p != '"'; p++) {
        unsigned cp = (unsigned char)*p;
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': cp = '\n'; break;
            case 't': cp = '\t'; break;
            case 'u': {
                char hex[5] = { 0 };
                for (int i = 0; i < 4 && p[1 + i]; i++) hex[i] = p[1 + i];
                cp = (unsigned)strtoul(hex, NULL, 16);
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?';
                p += strlen(hex);
                break;
            }
            default: cp = (unsigned char)*p; break; /* \" \\ \/ */
            }
            size_t n = put_utf8(out, w, len, cp);
            if (!n) break;
            w += n;
            continue;
        }
        /* raw UTF-8: copy the whole sequence or stop, never half of it */
        size_t n = cp < 0x80 ? 1 : (cp & 0xE0) == 0xC0 ? 2 : (cp & 0xF0) == 0xE0 ? 3 : 4;
        if (w + n >= len || strnlen(p, n) < n) {
            break;
        }
        memcpy(out + w, p, n);
        w += n;
        p += n - 1;
    }
    out[w] = '\0';
    return true;
}

/* Numbers of a JSON array value "[a,b,...]". Returns how many were read. */
static int number_array(const char *v, double *out, int max)
{
    if (!v || *v != '[') {
        return 0;
    }
    int n = 0;
    const char *p = skip_ws(v + 1);
    while (n < max && *p && *p != ']') {
        char *end;
        double d = strtod(p, &end);
        if (end == p) {
            break;
        }
        out[n++] = d;
        p = skip_ws(end);
        if (*p == ',') {
            p = skip_ws(p + 1);
        }
    }
    return n;
}

bool avo_weather_parse(const char *json, avo_weather_t *out)
{
    memset(out, 0, sizeof *out);
    const char *cur = avo_json_find(json, "current");
    double temp, code, day = 1;
    if (!cur || !avo_json_number(cur, "temperature_2m", &temp) || !avo_json_number(cur, "weather_code", &code)) {
        return false;
    }
    avo_json_number(cur, "is_day", &day);
    out->temp = (float)temp;
    out->code = (int)code;
    out->is_day = day != 0;
    const char *daily = avo_json_find(json, "daily");
    if (daily) {
        double c[AVO_WX_DAYS], hi[AVO_WX_DAYS], lo[AVO_WX_DAYS];
        int n = number_array(avo_json_find(daily, "weather_code"), c, AVO_WX_DAYS);
        int nh = number_array(avo_json_find(daily, "temperature_2m_max"), hi, AVO_WX_DAYS);
        int nl = number_array(avo_json_find(daily, "temperature_2m_min"), lo, AVO_WX_DAYS);
        if (nh < n) n = nh;
        if (nl < n) n = nl;
        for (int i = 0; i < n; i++) {
            out->day_code[i] = (int)c[i];
            out->day_max[i] = (float)hi[i];
            out->day_min[i] = (float)lo[i];
        }
        out->days = n;
    }
    out->valid = true;
    return true;
}

bool avo_geo_parse(const char *json, double *lat, double *lon, char *city, size_t len)
{
    const char *ok = avo_json_find(json, "success");
    if (ok && strncmp(ok, "false", 5) == 0) {
        return false;
    }
    if (!avo_json_number(json, "latitude", lat) || !avo_json_number(json, "longitude", lon)) {
        return false;
    }
    if (!avo_json_string(json, "city", city, len)) {
        city[0] = '\0';
    }
    return true;
}

avo_wx_kind_t avo_wmo_kind(int code)
{
    if (code == 0) return AVO_WX_CLEAR;
    if (code == 1 || code == 2) return AVO_WX_PARTLY;
    if (code == 45 || code == 48) return AVO_WX_FOG;
    if (code >= 51 && code <= 57) return AVO_WX_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return AVO_WX_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return AVO_WX_SNOW;
    if (code >= 95 && code <= 99) return AVO_WX_STORM;
    return AVO_WX_CLOUDY;
}

const char *avo_wmo_text(int code)
{
    switch (code) {
    case 0: return "Despejado";
    case 1: return "Mayormente despejado";
    case 2: return "Parcialmente nublado";
    case 3: return "Nublado";
    case 45: case 48: return "Niebla";
    case 51: case 53: case 55: return "Llovizna";
    case 56: case 57: return "Llovizna helada";
    case 61: case 63: return "Lluvia";
    case 65: return "Lluvia fuerte";
    case 66: case 67: return "Lluvia helada";
    case 71: case 73: return "Nieve";
    case 75: return "Nevada fuerte";
    case 77: return "Granizo menudo";
    case 80: case 81: return "Chubascos";
    case 82: return "Chubascos fuertes";
    case 85: case 86: return "Chubascos de nieve";
    case 95: return "Tormenta";
    case 96: case 99: return "Tormenta con granizo";
    default: return "Variable";
    }
}
