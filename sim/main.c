/*
 * avocatOS desktop simulator.
 *
 *   ./avocatos_sim             interactive SDL window (mouse = finger)
 *       b BOOT   d BOOT double   p PWR   l PWR long
 *       i connect fake iPhone    n notification   k incoming call   c charger
 *       t double tap on the case     f wrist flick
 *   ./avocatos_sim --shots D   headless: scripted finger walks every screen,
 *                              checks the gestures and writes D/NN_name.ppm
 * Frames come from a real partial-refresh framebuffer, like the watch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "lvgl.h"
#include "avo_ui.h"
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"
#include "sim.h"

#define W AVO_W
#define H AVO_H
#define STEP_MS 5

static uint32_t s_ms;
static bool s_headless;
static time_t s_fixed;
static bool s_fresh = true;
static uint16_t s_fb[W * H];
static int s_fail;

/* ---------------------------------------------------------------- clock */
static uint32_t real_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

uint32_t sim_millis(void) { return s_headless ? s_ms : real_ms(); }
time_t sim_fixed_time(void) { return s_fixed; }
bool sim_fresh_settings(void) { return s_fresh; }
void sim_set_brightness(uint8_t p) { (void)p; }
static uint32_t tick_cb(void) { return sim_millis(); }

/* ---------------------------------------------------------------- headless display */
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *src = (const uint16_t *)px;
    int32_t w = lv_area_get_width(a);
    for (int32_t y = a->y1; y <= a->y2; y++) {
        memcpy(&s_fb[y * W + a->x1], src + (y - a->y1) * w, (size_t)w * 2);
    }
    lv_display_flush_ready(d);
}

static void run_ms(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += STEP_MS) {
        s_ms += STEP_MS;
        lv_timer_handler();
    }
}

static void write_frame(const char *dir, const char *name)
{
    static int n;
    char path[512];
    snprintf(path, sizeof path, "%s/%02d_%s.ppm", dir, ++n, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        uint16_t c = s_fb[i];
        const uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void shot(const char *dir, const char *name)
{
    run_ms(700);
    write_frame(dir, name);
}

/* ---------------------------------------------------------------- scripted finger */
static struct {
    bool pressed;
    int32_t x, y;
} finger;

static void finger_read(lv_indev_t *i, lv_indev_data_t *d)
{
    (void)i;
    d->point.x = finger.x;
    d->point.y = finger.y;
    d->state = finger.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* drag with a sideways drift, like a real thumb */
static void swipe(int x0, int y0, int x1, int y1, uint32_t ms)
{
    const int steps = 12;
    finger = (typeof(finger)){ true, x0, y0 };
    run_ms(30);
    for (int i = 1; i <= steps; i++) {
        finger.x = x0 + (x1 - x0) * i / steps;
        finger.y = y0 + (y1 - y0) * i / steps;
        run_ms(ms / steps);
    }
    finger.pressed = false;
    run_ms(40);
    run_ms(500); /* transition */
}

static void tap(int x, int y)
{
    finger = (typeof(finger)){ true, x, y };
    run_ms(60);
    finger.pressed = false;
    run_ms(500);
}

static void expect(const char *what, avo_route_t want)
{
    bool ok = avo_nav_route() == want;
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    s_fail += !ok;
}

static void check(const char *what, bool ok)
{
    printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    s_fail += !ok;
}

/* double tap / wrist flick / alarms, driven like the board does */
static void motion_checks(void)
{
    avo_nav_face(); run_ms(500);
    sim_phone_push(true); run_ms(400);
    check("llamada: suena el timbre", sim_last_sound() == AVO_SOUND_RING);
    avo_ui_post_flick(); run_ms(300);
    check("giro de muñeca silencia la llamada sin colgar", avo_overlay_active() && sim_last_sound() < 0);
    avo_ui_post_double_tap(); run_ms(300);
    check("doble toque contesta la llamada", !avo_overlay_active());
    sim_phone_push(false); run_ms(400);
    avo_ui_post_flick(); run_ms(300);
    check("giro de muñeca descarta el aviso", !avo_overlay_active());
    avo_nav_app(&AVO_APP_SETTINGS); run_ms(600);
    avo_ui_post_flick(); run_ms(700);
    expect("giro de muñeca vuelve a la esfera", AVO_ROUTE_FACE);

    /* double tap made of touches on a button = two presses, not the gesture */
    avo_nav_app(&AVO_APP_MUSIC); run_ms(700);
    avo_media_t m0, m1;
    avo_hal_media(&m0);
    tap(205, 292);                            /* play/pause button */
    avo_ui_post_double_tap(); run_ms(300);
    avo_hal_media(&m1);
    check("doble toque sobre un botón no se suma", m1.playing == !m0.playing);
    avo_nav_face(); run_ms(1500);
    tap(205, 250);                            /* the face: no control there */
    avo_ui_post_double_tap(); run_ms(300);
    avo_hal_media(&m0);
    check("doble toque en la esfera pausa/reanuda la música", m0.playing == !m1.playing);

    /* an alarm one minute ahead rings, double tap snoozes it */
    avo_time_t now;
    avo_hal_time_now(&now);
    avo_alarms_t *al = avo_alarms();
    int saved = al->count;
    al->list[al->count++] = (avo_alarm_t){ (uint8_t)now.hour, (uint8_t)((now.min + 1) % 60), 0, true };
    bool rang = false;
    for (int i = 0; i < 70 && !rang; i++) {
        run_ms(1000);
        rang = avo_overlay_active() && sim_last_sound() == AVO_SOUND_ALARM;
    }
    check("la alarma suena a su hora", rang);
    avo_ui_post_double_tap(); run_ms(300);
    char hm[12];
    bool snoozed = false;
    avo_alarms_next_text(hm, sizeof hm, &snoozed);
    check("doble toque pospone la alarma", !avo_overlay_active() && snoozed && sim_last_sound() < 0);
    check("posponer muestra el aviso breve", lv_obj_get_child_count(lv_layer_top()) > 0);
    run_ms(3000);
    check("el aviso breve desaparece solo", lv_obj_get_child_count(lv_layer_top()) == 0);
    check("una alarma de una vez se desactiva sola", !al->list[saved].enabled);
    al->count = saved;
    avo_alarms_commit();

    avo_nav_face(); run_ms(1500);
    finger = (typeof(finger)){ true, 205, 250 };
    run_ms(700);                              /* touch and hold the face */
    finger.pressed = false;
    run_ms(600);
    expect("mantener pulsado en la esfera abre las apps", AVO_ROUTE_GRID);
    avo_nav_face(); run_ms(500);
}

/* ---------------------------------------------------------------- tour */
static void gesture_checks(void)
{
    avo_nav_face(); run_ms(500);
    swipe(205, 150, 240, 330, 240);           /* down with 35 px drift */
    expect("deslizar abajo (con desvío) abre Notificaciones", AVO_ROUTE_NOTIF);
    swipe(205, 380, 200, 180, 120);           /* fast flick up from the lower half */
    expect("flick rápido desplaza Notificaciones sin cerrarlas", AVO_ROUTE_NOTIF);
    swipe(205, 495, 190, 300, 240);           /* up from the bottom edge */
    expect("deslizar arriba desde el borde cierra Notificaciones", AVO_ROUTE_FACE);
    swipe(200, 420, 225, 240, 260);           /* up with drift */
    expect("deslizar arriba en la esfera abre Smart Stack", AVO_ROUTE_STACK);
    swipe(205, 100, 215, 300, 240);
    expect("deslizar abajo cierra Smart Stack", AVO_ROUTE_FACE);
    avo_ui_post_button(AVO_BTN_BOOT, AVO_PRESS_LONG); run_ms(800);
    expect("BOOT largo ya no abre nada", AVO_ROUTE_FACE);
    avo_ui_post_button(AVO_BTN_PWR, AVO_PRESS_SHORT); run_ms(800);
    expect("PWR abre Centro de control", AVO_ROUTE_CC);
    swipe(205, 400, 210, 180, 240);           /* up again: Now Playing page */
    expect("deslizar arriba en Centro de control muestra Reproduciendo", AVO_ROUTE_CC);
    swipe(205, 150, 210, 380, 240);           /* down: back to the toggles */
    expect("deslizar abajo vuelve a los controles", AVO_ROUTE_CC);
    swipe(205, 120, 190, 330, 240);
    expect("deslizar abajo cierra Centro de control", AVO_ROUTE_FACE);
    avo_nav_control_center(); run_ms(500);
    tap(330, 280);                            /* Linterna toggle (3rd col, 2nd row) */
    expect("Linterna desde Centro de control", AVO_ROUTE_APP);
    swipe(60, 250, 300, 265, 260);
    expect("deslizar a la derecha sale de Linterna", AVO_ROUTE_FACE);
    avo_nav_app(&AVO_APP_SETTINGS); run_ms(500);
    swipe(60, 300, 290, 330, 260);
    expect("deslizar a la derecha sale de una app", AVO_ROUTE_FACE);
}

static void tour(const char *dir, const char *suffix)
{
    char nm[64];
#define SHOT(x) do { snprintf(nm, sizeof nm, "%s_%s", x, suffix); shot(dir, nm); } while (0)
    avo_nav_face();
    for (int i = 0; i < avo_faces_count(); i++) {
        avo_nav_face_select(i);
        snprintf(nm, sizeof nm, "face_%s", avo_face_name(i));
        SHOT(nm);
    }
    avo_nav_face_select(0);
    avo_nav_grid(); run_ms(500);
    /* drag the honeycomb and grab a frame mid-motion (overlap artifacts) */
    finger = (typeof(finger)){ true, 205, 250 };
    run_ms(30);
    for (int i = 1; i <= 8; i++) { finger.x = 205 - 11 * i; finger.y = 250 - 9 * i; run_ms(16); }
    write_frame(dir, "grid_dragging");
    for (int i = 1; i <= 14; i++) { finger.x -= 12; finger.y -= 14; run_ms(16); }
    write_frame(dir, "grid_dragging_edge");
    finger.pressed = false; run_ms(600);
    SHOT("grid");
    avo_nav_face(); run_ms(400);
    avo_nav_control_center();       SHOT("control_center");
    swipe(205, 420, 210, 180, 240); write_frame(dir, suffix[0] == 'c' ? "now_playing_clean" : "now_playing_avocado");
    avo_nav_control_center(); run_ms(400);
    avo_nav_stack();                SHOT("smart_stack");
    avo_nav_face(); run_ms(400);
    avo_nav_notifications();        SHOT("notifications");
    avo_notif_open_detail(103);     SHOT("notification_detail");
    avo_nav_face(); run_ms(400);
    avo_nav_app(&AVO_APP_MUSIC);    SHOT("music");
    sim_set_artwork(true);          run_ms(400); SHOT("music_cover");
    sim_set_long_track();           run_ms(300); SHOT("music_long_cover");
    run_ms(2500);                   SHOT("music_long_scrolled");
    sim_set_artwork(false);         run_ms(400); SHOT("music_long_text");
    avo_settings()->artwork = false; sim_set_artwork(true); run_ms(400); SHOT("music_covers_off");
    avo_settings()->artwork = true; sim_set_artwork(false);
    avo_nav_app(&AVO_APP_SETTINGS); SHOT("settings");
    avo_nav_face(); run_ms(400);
    sim_phone_push(false);          run_ms(400); write_frame(dir, "banner");
    avo_overlay_dismiss(); run_ms(300);
    sim_set_charger(true);          run_ms(1500); write_frame(dir, "charging");
    sim_set_charger(false);         run_ms(3000);
    avo_nav_app(&AVO_APP_ACTIVITY); SHOT("activity");
    avo_nav_app(&AVO_APP_WEATHER);  SHOT("weather");
    avo_nav_app(&AVO_APP_ALARMS);   SHOT("alarms");
    tap(205, 150);                  SHOT("alarm_editor");
    avo_nav_app(&AVO_APP_SETTINGS); run_ms(400);
    avo_nav_push(avo_settings_sound_page, NULL); SHOT("settings_sound");
    avo_nav_push(avo_settings_gestures_page, avo_settings_gestures_leave); SHOT("settings_gestures");
    avo_nav_face(); run_ms(400);
    avo_nav_face_select(1);         SHOT("face_modular_live");
    avo_nav_face_select(0);
    avo_alert_timer_done(5, NULL);  run_ms(500); write_frame(dir, "timer_done");
    avo_overlay_dismiss(); run_ms(300);
#undef SHOT
    (void)suffix;
}

static int headless(const char *dir)
{
    mkdir(dir, 0755);
    s_headless = true;
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    static uint8_t buf[W * 60 * 2] __attribute__((aligned(64)));
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, finger_read);

    avo_ui_start();
    run_ms(1500);
    avo_hal_bt_enable(true);
    sim_phone_connect();
    run_ms(300);
    gesture_checks();
    motion_checks();
    tour(dir, "clean");
    avo_settings()->theme = AVO_THEME_AVOCADO;
    avo_settings_commit();
    run_ms(400);
    tour(dir, "avocado");
    sim_phone_push(true);
    run_ms(500);
    write_frame(dir, "incoming_call");
    printf("%s\n", s_fail ? "GESTURES: FAIL" : "GESTURES: OK");
    return s_fail ? 1 : 0;
}

/* ---------------------------------------------------------------- interactive */
static void keyboard_cb(lv_event_t *e)
{
    static bool charger;
    (void)e;
    switch (lv_indev_get_key(lv_indev_active())) {
    case 'b': avo_ui_post_button(AVO_BTN_BOOT, AVO_PRESS_SHORT); break;
    case 'd': avo_ui_post_button(AVO_BTN_BOOT, AVO_PRESS_DOUBLE); break;
    case 'p': avo_ui_post_button(AVO_BTN_PWR, AVO_PRESS_SHORT); break;
    case 'l': avo_ui_post_button(AVO_BTN_PWR, AVO_PRESS_LONG); break;
    case 'i': sim_phone_connect(); break;
    case 'n': sim_phone_push(false); break;
    case 'k': sim_phone_push(true); break;
    case 'c': charger = !charger; sim_set_charger(charger); break;
    case 't': avo_ui_post_double_tap(); break;
    case 'f': avo_ui_post_flick(); break;
    default: break;
    }
}

static int interactive(void)
{
    s_fresh = false;
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_sdl_window_create(W, H);
    lv_sdl_window_set_title(d, "avocatOS");
    lv_sdl_mouse_create();
    lv_indev_t *kb = lv_sdl_keyboard_create();
    lv_group_t *g = lv_group_create();
    lv_obj_t *catcher = lv_obj_create(lv_layer_sys());
    lv_obj_set_size(catcher, 0, 0);
    lv_group_add_obj(g, catcher);
    lv_obj_add_event_cb(catcher, keyboard_cb, LV_EVENT_KEY, NULL);
    lv_indev_set_group(kb, g);
    avo_ui_start();
    for (;;) {
        uint32_t wait = lv_timer_handler();
        usleep((wait > 20 ? 20 : wait) * 1000);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--shots") == 0) {
        struct tm tm = { .tm_year = 2026 - 1900, .tm_mon = 8, .tm_mday = 24, .tm_hour = 15, .tm_min = 9, .tm_sec = 30 };
        s_fixed = timegm(&tm); /* Thu 24 Sep 2026, 10:09:30 in UTC-5 */
        return headless(argv[2]);
    }
    return interactive();
}
