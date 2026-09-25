/* Host unit tests: swipe recognizer, calendar, ANCS / AMS / CTS parsers. */
#include <stdio.h>
#include <string.h>
#include "avo_core.h"

static int g_failed, g_passed;
#define CHECK(c) do { if (c) g_passed++; else { g_failed++; fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) == 0) g_passed++; else { g_failed++; \
    fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)
#define RUN(fn) do { fprintf(stderr, "- %s\n", #fn); fn(); } while (0)

/* ---------------- swipe ---------------- */

/* Drag from (x0,y0) to (x1,y1) in `steps` samples over `ms`, then lift. */
static avo_swipe_dir_t drag(int x0, int y0, int x1, int y1, int steps, uint32_t ms)
{
    avo_swipe_tracker_t t;
    avo_swipe_t s;
    avo_swipe_reset(&t);
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
        if (avo_swipe_feed(&t, true, (int16_t)x, (int16_t)y, ms * (uint32_t)i / (uint32_t)steps, &s)) {
            return AVO_SWIPE_NONE; /* must never fire while pressed */
        }
    }
    return avo_swipe_feed(&t, false, (int16_t)x1, (int16_t)y1, ms + 10, &s) ? s.dir : AVO_SWIPE_NONE;
}

static void test_swipe_clean_directions(void)
{
    CHECK(drag(200, 400, 200, 250, 8, 200) == AVO_SWIPE_UP);
    CHECK(drag(200, 100, 200, 260, 8, 200) == AVO_SWIPE_DOWN);
    CHECK(drag(300, 250, 140, 250, 8, 200) == AVO_SWIPE_LEFT);
    CHECK(drag(100, 250, 260, 250, 8, 200) == AVO_SWIPE_RIGHT);
}

static void test_swipe_tolerates_diagonal_drift(void)
{
    /* the case LVGL missed: 90 px down with 35 px sideways drift */
    CHECK(drag(200, 120, 235, 210, 10, 220) == AVO_SWIPE_DOWN);
    CHECK(drag(200, 420, 170, 330, 10, 220) == AVO_SWIPE_UP);
}

static void test_swipe_short_flick(void)
{
    CHECK(drag(200, 300, 200, 255, 4, 90) == AVO_SWIPE_UP);
}

static void test_swipe_slow_long_drag(void)
{
    CHECK(drag(80, 250, 250, 255, 30, 1000) == AVO_SWIPE_RIGHT);
}

static void test_swipe_rejects_taps_and_ambiguous(void)
{
    CHECK(drag(200, 250, 204, 247, 3, 80) == AVO_SWIPE_NONE);     /* tap */
    CHECK(drag(200, 250, 260, 310, 8, 200) == AVO_SWIPE_NONE);    /* 45 degrees */
    CHECK(drag(200, 250, 200, 280, 20, 900) == AVO_SWIPE_NONE);   /* slow and short */
    CHECK(drag(200, 400, 200, 250, 40, 2500) == AVO_SWIPE_NONE);  /* held too long */
}

static void test_swipe_reports_start_point(void)
{
    avo_swipe_tracker_t t;
    avo_swipe_t s = { 0 };
    avo_swipe_reset(&t);
    avo_swipe_feed(&t, true, 205, 480, 0, &s);
    avo_swipe_feed(&t, true, 205, 380, 100, &s);
    CHECK(avo_swipe_feed(&t, false, 205, 380, 110, &s));
    CHECK(s.start_x == 205 && s.start_y == 480 && s.dy == -100);
}

static void test_swipe_release_without_press_is_ignored(void)
{
    avo_swipe_tracker_t t;
    avo_swipe_t s;
    avo_swipe_reset(&t);
    CHECK(!avo_swipe_feed(&t, false, 10, 10, 0, &s));
}

/* ---------------- text ---------------- */

static void test_text_clean_keeps_plain_text(void)
{
    char s[] = "¿Nos vemos? Café · 10:09";
    avo_text_clean(s);
    CHECK_STR(s, "¿Nos vemos? Café · 10:09");
}

static void test_text_clean_strips_modifiers(void)
{
    /* 👋🏽 (wave + skin tone), ❤️ (heart + VS16), 👨‍💻 (man ZWJ laptop) */
    char s[] = "Hola \xF0\x9F\x91\x8B\xF0\x9F\x8F\xBD \xE2\x9D\xA4\xEF\xB8\x8F "
               "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x92\xBB";
    avo_text_clean(s);
    CHECK_STR(s, "Hola \xF0\x9F\x91\x8B \xE2\x9D\xA4 \xF0\x9F\x91\xA8\xF0\x9F\x92\xBB");
}

static void test_text_clean_drops_broken_utf8(void)
{
    char s[] = "ok\xC3 fin\xF0\x9F";
    avo_text_clean(s);
    CHECK_STR(s, "ok fin");
}

static void test_url_encode(void)
{
    char b[64];
    CHECK(avo_url_encode("Los Hass Aguacate", b, sizeof b));
    CHECK_STR(b, "Los+Hass+Aguacate");
    CHECK(avo_url_encode("Café & Co/1", b, sizeof b));
    CHECK_STR(b, "Caf%C3%A9+%26+Co%2F1");
    CHECK(!avo_url_encode("abcdefgh", b, 5));
}

static void test_itunes_artwork_url(void)
{
    const char *json = "{\"resultCount\":1,\"results\":[{\"trackName\":\"X\",\"artworkUrl60\":\"https://a/60x60bb.jpg\","
                       "\"artworkUrl100\":\"https:\\/\\/is1-ssl.mzstatic.com\\/image\\/thumb\\/a\\/100x100bb.jpg\"}]}";
    char u[128];
    CHECK(avo_itunes_artwork_url(json, 132, u, sizeof u));
    CHECK_STR(u, "https://is1-ssl.mzstatic.com/image/thumb/a/132x132bb.jpg");
    CHECK(!avo_itunes_artwork_url("{\"resultCount\":0,\"results\":[]}", 132, u, sizeof u));
}

/* ---------------- calendar ---------------- */

static void test_epoch_known_dates(void)
{
    avo_time_t t = { 1970, 1, 1, 4, 0, 0, 0 };
    CHECK(avo_time_to_epoch(&t, 0) == 0);
    avo_time_t u = { 2026, 9, 24, 4, 10, 9, 30 };
    CHECK(avo_time_to_epoch(&u, 0) == 1790244570LL);
    CHECK(avo_time_to_epoch(&u, -300) == 1790244570LL + 5 * 3600);
    avo_time_t leap = { 2028, 2, 29, 2, 12, 0, 0 };
    CHECK(avo_time_to_epoch(&leap, 0) == 1835438400LL);
}

/* ---------------- ANCS ---------------- */

static void test_ancs_source(void)
{
    const uint8_t pkt[8] = { AVO_ANCS_ADDED, 0x02, AVO_ANCS_CAT_SOCIAL, 3, 0x78, 0x56, 0x34, 0x12 };
    avo_ancs_source_t s;
    CHECK(avo_ancs_parse_source(pkt, 8, &s));
    CHECK(s.event_id == AVO_ANCS_ADDED && s.category == AVO_ANCS_CAT_SOCIAL && s.category_count == 3);
    CHECK(s.uid == 0x12345678u);
    CHECK(!avo_ancs_parse_source(pkt, 7, &s));
    const uint8_t bad[8] = { 9, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(!avo_ancs_parse_source(bad, 8, &s));
}

static void test_ancs_get_attrs_request(void)
{
    uint8_t b[32];
    size_t n = avo_ancs_build_get_attrs(0x01020304u, b, sizeof b);
    const uint8_t want[] = { 0x00, 0x04, 0x03, 0x02, 0x01,
                             0x00,                         /* app identifier */
                             0x01, AVO_ANCS_TITLE_MAX - 1, 0x00,
                             0x03, AVO_ANCS_MESSAGE_MAX - 1, 0x00,
                             0x05 };                       /* date */
    CHECK(n == sizeof want);
    CHECK(memcmp(b, want, sizeof want) == 0);
    CHECK(avo_ancs_build_get_attrs(1, b, 4) == 0); /* too small */
}

static size_t put_attr(uint8_t *p, uint8_t id, const char *s)
{
    size_t l = strlen(s);
    p[0] = id;
    p[1] = (uint8_t)(l & 0xFF);
    p[2] = (uint8_t)(l >> 8);
    memcpy(p + 3, s, l);
    return 3 + l;
}

static size_t sample_response(uint8_t *b)
{
    size_t n = 0;
    b[n++] = 0x00;
    b[n++] = 0x2A; b[n++] = 0; b[n++] = 0; b[n++] = 0;
    n += put_attr(b + n, 0, "net.whatsapp.WhatsApp");
    n += put_attr(b + n, 1, "Laura");
    n += put_attr(b + n, 3, "¿Nos vemos a las 7?");
    n += put_attr(b + n, 5, "20260924T100930");
    return n;
}

static void test_ancs_attrs_complete(void)
{
    uint8_t b[256];
    size_t n = sample_response(b);
    avo_ancs_attrs_t a;
    CHECK(avo_ancs_parse_attrs(b, n, &a) == (int)n);
    CHECK(a.uid == 0x2A);
    CHECK_STR(a.app_id, "net.whatsapp.WhatsApp");
    CHECK_STR(a.title, "Laura");
    CHECK_STR(a.message, "¿Nos vemos a las 7?");
    CHECK_STR(a.date, "20260924T100930");
}

static void test_ancs_attrs_fragmented(void)
{
    uint8_t b[256];
    size_t n = sample_response(b);
    avo_ancs_attrs_t a;
    for (size_t cut = 1; cut < n; cut++) {
        CHECK(avo_ancs_parse_attrs(b, cut, &a) == 0);
    }
}

static void test_ancs_attrs_truncates_long_values(void)
{
    uint8_t b[512];
    size_t n = 0;
    b[n++] = 0; b[n++] = 1; b[n++] = 0; b[n++] = 0; b[n++] = 0;
    char longmsg[300];
    memset(longmsg, 'a', sizeof longmsg - 1);
    longmsg[sizeof longmsg - 1] = '\0';
    n += put_attr(b + n, 0, "x");
    n += put_attr(b + n, 1, "t");
    n += put_attr(b + n, 3, longmsg);
    n += put_attr(b + n, 5, "d");
    avo_ancs_attrs_t a;
    CHECK(avo_ancs_parse_attrs(b, n, &a) == (int)n);
    CHECK(strlen(a.message) == AVO_ANCS_MESSAGE_MAX - 1);
}

static void test_ancs_attrs_malformed(void)
{
    const uint8_t wrong_cmd[] = { 0x07, 0, 0, 0, 0 };
    avo_ancs_attrs_t a;
    CHECK(avo_ancs_parse_attrs(wrong_cmd, sizeof wrong_cmd, &a) == -1);
}

static void test_ancs_action(void)
{
    uint8_t b[8];
    CHECK(avo_ancs_build_action(0x0A0B0C0Du, false, b, sizeof b) == 6);
    const uint8_t want[] = { 0x02, 0x0D, 0x0C, 0x0B, 0x0A, 0x01 };
    CHECK(memcmp(b, want, 6) == 0);
    avo_ancs_build_action(1, true, b, sizeof b);
    CHECK(b[5] == 0x00);
}

static void test_app_names(void)
{
    char n[32];
    avo_app_display_name("net.whatsapp.WhatsApp", n, sizeof n);
    CHECK_STR(n, "WhatsApp");
    avo_app_display_name("com.apple.MobileSMS", n, sizeof n);
    CHECK_STR(n, "Mensajes");
    avo_app_display_name("com.example.coolapp", n, sizeof n);
    CHECK_STR(n, "Coolapp");
    avo_app_display_name("", n, sizeof n);
    CHECK_STR(n, "iPhone");
}

/* ---------------- AMS ---------------- */

static void test_ams_update(void)
{
    const uint8_t pkt[] = { AVO_AMS_ENTITY_TRACK, AVO_AMS_TRACK_TITLE, 0, 'H', 'a', 's', 's' };
    avo_ams_update_t u;
    CHECK(avo_ams_parse_update(pkt, sizeof pkt, &u));
    CHECK(u.entity == AVO_AMS_ENTITY_TRACK && u.attr == AVO_AMS_TRACK_TITLE);
    CHECK_STR(u.value, "Hass");
    CHECK(!avo_ams_parse_update(pkt, 2, &u));
}

static void test_ams_playback(void)
{
    CHECK(avo_ams_playback_state("1,1.0,12.5") == 1);
    CHECK(avo_ams_playback_state("0,0.0,99.1") == 0);
    CHECK(avo_ams_playback_state("") == -1);
    CHECK(avo_ams_playback_state("x") == -1);
}

/* ---------------- CTS ---------------- */

static void test_cts_time(void)
{
    const uint8_t pkt[10] = { 0xEA, 0x07, 9, 24, 10, 9, 30, 4, 0, 0 }; /* 2026, Thursday */
    avo_time_t t;
    CHECK(avo_cts_parse_time(pkt, sizeof pkt, &t));
    CHECK(t.year == 2026 && t.month == 9 && t.day == 24 && t.hour == 10 && t.min == 9 && t.sec == 30);
    CHECK(t.wday == 4);
    const uint8_t bad[10] = { 0xEA, 0x07, 13, 24, 10, 9, 30, 4, 0, 0 };
    CHECK(!avo_cts_parse_time(bad, sizeof bad, &t));
}

static void test_cts_local_info(void)
{
    int16_t off;
    const uint8_t bogota[2] = { (uint8_t)(int8_t)-20, 0 };  /* UTC-5, no DST */
    CHECK(avo_cts_parse_local_info(bogota, 2, &off) && off == -300);
    const uint8_t madrid_summer[2] = { 4, 4 };              /* UTC+1 + 1 h DST */
    CHECK(avo_cts_parse_local_info(madrid_summer, 2, &off) && off == 120);
    const uint8_t unknown[2] = { (uint8_t)(int8_t)-128, 0 };
    CHECK(!avo_cts_parse_local_info(unknown, 2, &off));
}

int main(void)
{
    RUN(test_swipe_clean_directions);
    RUN(test_swipe_tolerates_diagonal_drift);
    RUN(test_swipe_short_flick);
    RUN(test_swipe_slow_long_drag);
    RUN(test_swipe_rejects_taps_and_ambiguous);
    RUN(test_swipe_reports_start_point);
    RUN(test_swipe_release_without_press_is_ignored);
    RUN(test_text_clean_keeps_plain_text);
    RUN(test_text_clean_strips_modifiers);
    RUN(test_text_clean_drops_broken_utf8);
    RUN(test_url_encode);
    RUN(test_itunes_artwork_url);
    RUN(test_epoch_known_dates);
    RUN(test_ancs_source);
    RUN(test_ancs_get_attrs_request);
    RUN(test_ancs_attrs_complete);
    RUN(test_ancs_attrs_fragmented);
    RUN(test_ancs_attrs_truncates_long_values);
    RUN(test_ancs_attrs_malformed);
    RUN(test_ancs_action);
    RUN(test_app_names);
    RUN(test_ams_update);
    RUN(test_ams_playback);
    RUN(test_cts_time);
    RUN(test_cts_local_info);
    printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
