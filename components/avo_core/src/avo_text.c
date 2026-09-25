/* Text helpers for strings that arrive from the phone. */
#include <stdio.h>
#include <string.h>
#include "avo_core.h"

/* Decode one UTF-8 sequence. Returns its length, or 0 if broken. */
static size_t utf8_decode(const unsigned char *p, uint32_t *cp)
{
    if (p[0] < 0x80) {
        *cp = p[0];
        return 1;
    }
    size_t n = (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3 : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    if (n == 0) {
        return 0;
    }
    uint32_t v = p[0] & (0x7F >> n);
    for (size_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0; /* also catches the terminating NUL */
        }
        v = (v << 6) | (p[i] & 0x3F);
    }
    *cp = v;
    return n;
}

static bool invisible_modifier(uint32_t cp)
{
    return cp == 0xFE0E || cp == 0xFE0F                 /* variation selectors */
        || cp == 0x200D                                 /* zero width joiner   */
        || cp == 0x20E3                                 /* combining keycap    */
        || (cp >= 0x1F3FB && cp <= 0x1F3FF)             /* skin tones          */
        || (cp >= 0xE0020 && cp <= 0xE007F);            /* tag characters      */
}

void avo_text_clean(char *s)
{
    unsigned char *r = (unsigned char *)s;
    unsigned char *w = r;
    while (*r) {
        uint32_t cp;
        size_t n = utf8_decode(r, &cp);
        if (n == 0) {
            r++; /* skip a broken byte */
            continue;
        }
        if (!invisible_modifier(cp)) {
            memmove(w, r, n);
            w += n;
        }
        r += n;
    }
    *w = '\0';
}

bool avo_url_encode(const char *src, char *dst, size_t len)
{
    static const char HEX[] = "0123456789ABCDEF";
    size_t w = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                     *p == '-' || *p == '_' || *p == '.' || *p == '~';
        size_t need = plain || *p == ' ' ? 1 : 3;
        if (w + need >= len) {
            return false;
        }
        if (plain) {
            dst[w++] = (char)*p;
        } else if (*p == ' ') {
            dst[w++] = '+';
        } else {
            dst[w++] = '%';
            dst[w++] = HEX[*p >> 4];
            dst[w++] = HEX[*p & 0x0F];
        }
    }
    dst[w] = '\0';
    return true;
}

bool avo_itunes_artwork_url(const char *json, int px, char *out, size_t len)
{
    static const char KEY[] = "\"artworkUrl100\":\"";
    const char *p = strstr(json, KEY);
    if (!p || len < 16) {
        return false;
    }
    p += sizeof KEY - 1;
    size_t w = 0;
    for (; *p && *p != '"'; p++) {
        if (*p == '\\' && p[1] == '/') {
            p++; /* JSON escapes "/" as "\/" */
        }
        if (w + 1 >= len) {
            return false;
        }
        out[w++] = *p;
    }
    out[w] = '\0';
    char *size = strstr(out, "100x100bb");
    if (!size) {
        return w > 0;
    }
    char tail[64];
    snprintf(tail, sizeof tail, "%s", size + 9);
    int n = snprintf(size, len - (size_t)(size - out), "%dx%dbb%s", px, px, tail);
    return n > 0 && (size_t)n < len - (size_t)(size - out);
}
