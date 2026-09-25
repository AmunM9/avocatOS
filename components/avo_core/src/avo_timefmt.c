#include <stdio.h>
#include <string.h>
#include "avo_core.h"

static const char *const WDAY_SHORT[7] = { "DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB" };
static const char *const WDAY_LONG[7] = {
    "domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado",
};
static const char *const MONTH_LONG[12] = {
    "enero", "febrero", "marzo", "abril", "mayo", "junio",
    "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre",
};

/* snprintf may cut a multi-byte UTF-8 sequence in half; drop the fragment. */
static size_t utf8_finish(char *out, size_t len, int written)
{
    if (len == 0) {
        return 0;
    }
    size_t n = (written < 0) ? 0 : (size_t)written;
    if (n < len) {
        return n;
    }
    n = len - 1;
    size_t cont = 0;
    while (n > 0 && ((unsigned char)out[n - 1] & 0xC0) == 0x80) {
        n--;
        cont++;
    }
    if (n > 0 && ((unsigned char)out[n - 1] & 0x80)) {
        unsigned char lead = (unsigned char)out[n - 1];
        size_t need = (lead >= 0xF0) ? 3 : (lead >= 0xE0) ? 2 : 1;
        if (cont < need) {
            n--; /* incomplete sequence: drop its lead byte too */
        } else {
            n += cont;
        }
    }
    out[n] = '\0';
    return n;
}

static int hour12(int h)
{
    int x = h % 12;
    return x == 0 ? 12 : x;
}

static int clamp_idx(int v, int max)
{
    return (v < 0 || v >= max) ? 0 : v;
}

size_t avo_fmt_hm(char *out, size_t len, const avo_time_t *t, bool h24)
{
    int h = h24 ? t->hour : hour12(t->hour);
    return utf8_finish(out, len, snprintf(out, len, "%02d:%02d", h, t->min));
}

size_t avo_fmt_hour(char *out, size_t len, const avo_time_t *t, bool h24)
{
    int h = h24 ? t->hour : hour12(t->hour);
    return utf8_finish(out, len, snprintf(out, len, "%02d", h));
}

size_t avo_fmt_min(char *out, size_t len, const avo_time_t *t)
{
    return utf8_finish(out, len, snprintf(out, len, "%02d", t->min));
}

size_t avo_fmt_wday_day(char *out, size_t len, const avo_time_t *t)
{
    return utf8_finish(out, len, snprintf(out, len, "%s %d", WDAY_SHORT[clamp_idx(t->wday, 7)], t->day));
}

size_t avo_fmt_long_date(char *out, size_t len, const avo_time_t *t)
{
    return utf8_finish(out, len, snprintf(out, len, "%s, %d de %s",
                                          WDAY_LONG[clamp_idx(t->wday, 7)], t->day,
                                          MONTH_LONG[clamp_idx(t->month - 1, 12)]));
}

const char *avo_fmt_ampm(const avo_time_t *t)
{
    return t->hour < 12 ? "a. m." : "p. m.";
}

size_t avo_fmt_stopwatch(char *out, size_t len, uint32_t elapsed_ms)
{
    uint32_t cs = (elapsed_ms / 10) % 100;
    uint32_t s = (elapsed_ms / 1000) % 60;
    uint32_t m = (elapsed_ms / 60000) % 60;
    uint32_t h = elapsed_ms / 3600000;
    int w = (h > 0)
        ? snprintf(out, len, "%u:%02u:%02u,%02u", (unsigned)h, (unsigned)m, (unsigned)s, (unsigned)cs)
        : snprintf(out, len, "%02u:%02u,%02u", (unsigned)m, (unsigned)s, (unsigned)cs);
    return utf8_finish(out, len, w);
}
