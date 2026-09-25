/*
 * avocatOS shell: navigation, buttons, power states, settings, and the
 * periodic checks that turn phone/battery events into overlays.
 *
 * Navigation model (watchOS inspired):
 *   face  --swipe up-->     Smart Stack      --swipe down--> face
 *   face  --swipe down-->   Notifications    --swipe up-->   face
 *   face  --swipe L/R-->    next/prev face (tileview)
 *   face  --BOOT-->         app grid --tap--> app --swipe right--> back
 *   PWR                     Control Center (overlay, returns to caller)
 *                           --swipe up--> Now Playing page
 * Swipes are recognized in avo_gesture.c; this file owns the transitions.
 */
#include <stdatomic.h>
#include <string.h>
#include "avo_ui_internal.h"

#define SPLASH_MS 900
#define TICK_MS 250
#define WAKE_ACTIVITY_MS 300
#define DIM_PERCENT 40
#define AOD_BRIGHTNESS 8
#define MAX_HEADER_CLOCKS 4
#define NAV_GUARD_MS (AVO_ANIM_MS + 120)
#define BATTERY_POLL_TICKS 4   /* 1 s */
#define OFFSET_POLL_TICKS 8    /* 2 s */

/* ---------------------------------------------------------------- state */
static avo_settings_t s_settings;
static avo_theme_mode_t s_applied_theme;
static avo_pwr_t s_pwr;

static avo_route_t s_route = AVO_ROUTE_FACE;
static const avo_app_t *s_app;       /* app of ROUTE_APP / ROUTE_SUB           */
static const avo_app_t *s_last_app;  /* for BOOT double press                  */
static bool s_app_from_grid;         /* back goes to the grid, else to the face */
static void (*s_sub_build)(lv_obj_t *);
static void (*s_leave)(void);        /* cleanup of the current screen          */

static lv_obj_t *s_face_scr;
static lv_obj_t *s_face_tv;

/* Control Center keeps the caller alive underneath */
static lv_obj_t *s_cc_prev_scr;
static avo_route_t s_cc_prev_route;
static void (*s_cc_prev_leave)(void);

static uint32_t s_nav_busy_until;
static bool s_modal;
static lv_obj_t *s_scroller;
static lv_obj_t *s_header_clocks[MAX_HEADER_CLOCKS];

static atomic_int s_btn_events;      /* bitmask, see avo_ui_post_button()      */
static atomic_bool s_wake_req;
static atomic_bool s_sleep_req;
static atomic_bool s_awake = true;
static atomic_bool s_double_tap;
static atomic_bool s_flick;
static void (*s_motion_probe)(bool flick);

static bool s_prev_usb;
static uint32_t s_tick_count;

/* ---------------------------------------------------------------- settings */
avo_settings_t *avo_settings(void) { return &s_settings; }

static void apply_brightness_for_state(void)
{
    uint8_t b = s_settings.brightness;
    switch (s_pwr.state) {
    case AVO_PWR_ACTIVE: avo_hal_display_brightness(b); break;
    case AVO_PWR_DIM: avo_hal_display_brightness(LV_MAX(5, b * DIM_PERCENT / 100)); break;
    case AVO_PWR_AOD: avo_hal_display_brightness(AOD_BRIGHTNESS); break;
    case AVO_PWR_OFF: avo_hal_display_brightness(0); break;
    }
}

static void pwr_config_from_settings(uint32_t now)
{
    uint32_t sleep_ms = (uint32_t)s_settings.screen_timeout_s * 1000u;
    avo_pwr_cfg_t cfg = {
        .dim_after_ms = sleep_ms > 6000 ? sleep_ms - 5000 : sleep_ms / 2,
        .sleep_after_ms = sleep_ms,
        .aod_enabled = s_settings.aod,
    };
    avo_pwr_state_t keep = s_pwr.state;
    avo_pwr_init(&s_pwr, &cfg, now);
    s_pwr.state = keep;
}

static void rebuild_face_screen(void);

void avo_settings_commit(void)
{
    avo_settings_sanitize(&s_settings, (uint8_t)AVO_FACE_MAX);
    avo_hal_settings_save(&s_settings);
    avo_hal_time_set_utc_offset(s_settings.utc_offset_min);
    avo_hal_bt_enable(s_settings.bluetooth);
    avo_hal_wifi_enable(s_settings.wifi);
    pwr_config_from_settings(avo_hal_millis());
    apply_brightness_for_state();
    if (s_settings.show_fps) {
        lv_sysmon_show_performance(NULL);
    } else {
        lv_sysmon_hide_performance(NULL);
    }
    if (s_settings.theme != s_applied_theme) {
        s_applied_theme = (avo_theme_mode_t)s_settings.theme;
        avo_theme_apply(s_applied_theme);
        if (s_settings.face >= avo_faces_count()) {
            s_settings.face = 0;
            avo_hal_settings_save(&s_settings);
        }
        rebuild_face_screen();
        avo_nav_reload();
    }
}

/* ---------------------------------------------------------------- header clocks */
static void header_clock_deleted(lv_event_t *e)
{
    lv_obj_t *l = lv_event_get_target(e);
    for (int i = 0; i < MAX_HEADER_CLOCKS; i++) {
        if (s_header_clocks[i] == l) {
            s_header_clocks[i] = NULL;
        }
    }
}

void avo_header_clock_refresh(lv_obj_t *label, const avo_time_t *t)
{
    char buf[12];
    avo_fmt_hm(buf, sizeof buf, t, s_settings.h24);
    lv_label_set_text(label, buf);
}

lv_obj_t *avo_header_clock(lv_obj_t *screen)
{
    lv_obj_t *l = avo_label(screen, &avo_font_26, avo_pal()->label, "");
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -(AVO_PAD + 16), AVO_TOP + 8);
    avo_time_t t;
    avo_hal_time_now(&t);
    avo_header_clock_refresh(l, &t);
    for (int i = 0; i < MAX_HEADER_CLOCKS; i++) {
        if (!s_header_clocks[i]) {
            s_header_clocks[i] = l;
            lv_obj_add_event_cb(l, header_clock_deleted, LV_EVENT_DELETE, NULL);
            break;
        }
    }
    return l;
}

/* ---------------------------------------------------------------- navigation state */
avo_route_t avo_nav_route(void) { return s_route; }
bool avo_nav_locked(void) { return (int32_t)(s_nav_busy_until - avo_hal_millis()) > 0; }
void avo_nav_set_modal(bool on) { s_modal = on; }
bool avo_nav_modal(void) { return s_modal; }
lv_obj_t *avo_nav_scroller(void) { return s_scroller; }

static void scroller_deleted(lv_event_t *e)
{
    if (lv_event_get_target(e) == s_scroller) {
        s_scroller = NULL;
    }
}

void avo_nav_set_scroller(lv_obj_t *scroller)
{
    s_scroller = scroller;
    if (scroller) {
        lv_obj_add_event_cb(scroller, scroller_deleted, LV_EVENT_DELETE, NULL);
    }
}

static void run_leave(void)
{
    s_modal = false;
    if (s_leave) {
        void (*fn)(void) = s_leave;
        s_leave = NULL;
        fn();
    }
}

/* Delete a screen only once no transition references it: LVGL 9.5 walks
 * the display's previous screen on every invalidation, so freeing it while
 * a load animation still points at it is a use-after-free. */
static void safe_delete_retry(lv_timer_t *t)
{
    lv_obj_t *scr = lv_timer_get_user_data(t);
    lv_display_t *d = lv_display_get_default();
    if (scr == lv_display_get_screen_active(d) || scr == lv_display_get_screen_prev(d)) {
        return; /* still in use: try again on the next tick */
    }
    lv_timer_delete(t);
    lv_obj_delete(scr);
}

static void safe_delete_screen(lv_obj_t *scr)
{
    if (scr) {
        lv_timer_create(safe_delete_retry, 50, scr);
    }
}

/* Drop the screen that was kept alive under the Control Center. */
static void cc_discard_prev(void)
{
    if (!s_cc_prev_scr) {
        return;
    }
    if (s_cc_prev_leave) {
        s_cc_prev_leave();
    }
    if (s_cc_prev_scr != s_face_scr) {
        safe_delete_screen(s_cc_prev_scr);
    }
    s_cc_prev_scr = NULL;
    s_cc_prev_leave = NULL;
}

static void load(lv_obj_t *scr, lv_screen_load_anim_t anim, avo_route_t route)
{
    lv_obj_t *old = lv_screen_active();
    bool keep_old = (old == s_face_scr) || (route == AVO_ROUTE_CC);
    if (route != AVO_ROUTE_CC && s_route == AVO_ROUTE_CC) {
        cc_discard_prev();
    }
    s_route = route;
    s_nav_busy_until = avo_hal_millis() + (anim == LV_SCREEN_LOAD_ANIM_NONE ? 0 : NAV_GUARD_MS);
    lv_screen_load_anim(scr, anim, anim == LV_SCREEN_LOAD_ANIM_NONE ? 0 : AVO_ANIM_MS, 0, !keep_old);
}

static lv_obj_t *new_screen(void)
{
    s_scroller = NULL; /* the builder may register the new screen's list */
    return avo_screen_create();
}

void avo_nav_face(void)
{
    if (s_route == AVO_ROUTE_FACE) {
        return;
    }
    avo_route_t from = s_route;
    run_leave();
    lv_screen_load_anim_t anim = LV_SCREEN_LOAD_ANIM_FADE_IN;
    if (from == AVO_ROUTE_STACK) anim = LV_SCREEN_LOAD_ANIM_OUT_BOTTOM;
    if (from == AVO_ROUTE_NOTIF) anim = LV_SCREEN_LOAD_ANIM_OUT_TOP;
    if (from == AVO_ROUTE_CC) anim = LV_SCREEN_LOAD_ANIM_OUT_BOTTOM;
    if (from == AVO_ROUTE_APP || from == AVO_ROUTE_SUB) anim = LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
    if (from == AVO_ROUTE_AOD) anim = LV_SCREEN_LOAD_ANIM_NONE;
    load(s_face_scr, anim, AVO_ROUTE_FACE);
}

void avo_nav_grid(void)
{
    avo_route_t from = s_route;
    run_leave();
    lv_obj_t *scr = new_screen();
    avo_grid_build(scr);
    load(scr, from == AVO_ROUTE_FACE ? LV_SCREEN_LOAD_ANIM_FADE_IN : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, AVO_ROUTE_GRID);
}

static void open_app(const avo_app_t *app, lv_screen_load_anim_t anim)
{
    run_leave();
    s_app = app;
    s_last_app = app;
    s_sub_build = NULL;
    lv_obj_t *scr = new_screen();
    app->build(scr);
    s_leave = app->leave;
    load(scr, anim, AVO_ROUTE_APP);
}

void avo_nav_app(const avo_app_t *app)
{
    if (!app) {
        return;
    }
    s_app_from_grid = (s_route == AVO_ROUTE_GRID) || (s_route == AVO_ROUTE_APP && s_app_from_grid);
    open_app(app, LV_SCREEN_LOAD_ANIM_OVER_LEFT);
}

void avo_nav_push(void (*build)(lv_obj_t *), void (*leave)(void))
{
    run_leave();
    s_sub_build = build;
    lv_obj_t *scr = new_screen();
    build(scr);
    s_leave = leave;
    load(scr, LV_SCREEN_LOAD_ANIM_OVER_LEFT, AVO_ROUTE_SUB);
}

void avo_nav_back(void)
{
    switch (s_route) {
    case AVO_ROUTE_SUB: open_app(s_app, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT); break;
    case AVO_ROUTE_APP:
        if (s_app_from_grid) {
            avo_nav_grid();
        } else {
            avo_nav_face();
        }
        break;
    case AVO_ROUTE_CC: avo_nav_control_center(); break; /* toggles closed */
    default: avo_nav_face(); break;
    }
}

void avo_nav_stack(void)
{
    run_leave();
    lv_obj_t *scr = new_screen();
    avo_stack_build(scr);
    load(scr, LV_SCREEN_LOAD_ANIM_OVER_TOP, AVO_ROUTE_STACK);
}

void avo_nav_notifications(void)
{
    run_leave();
    lv_obj_t *scr = new_screen();
    avo_notif_build(scr);
    load(scr, LV_SCREEN_LOAD_ANIM_OVER_BOTTOM, AVO_ROUTE_NOTIF);
}

void avo_nav_control_center(void)
{
    if (s_route == AVO_ROUTE_CC) {
        /* close: return to the screen underneath */
        lv_obj_t *prev = s_cc_prev_scr;
        s_leave = s_cc_prev_leave;
        s_route = s_cc_prev_route;
        s_cc_prev_scr = NULL;
        s_cc_prev_leave = NULL;
        s_modal = false;
        s_nav_busy_until = avo_hal_millis() + NAV_GUARD_MS;
        lv_screen_load_anim(prev, LV_SCREEN_LOAD_ANIM_OUT_BOTTOM, AVO_ANIM_MS, 0, true);
        return;
    }
    if (s_route == AVO_ROUTE_AOD) {
        return;
    }
    s_cc_prev_scr = lv_screen_active();
    s_cc_prev_route = s_route;
    s_cc_prev_leave = s_leave;
    s_leave = NULL;
    s_modal = false;
    lv_obj_t *scr = new_screen();
    avo_cc_build(scr);
    load(scr, LV_SCREEN_LOAD_ANIM_OVER_TOP, AVO_ROUTE_CC);
}

void avo_nav_reload(void)
{
    switch (s_route) {
    case AVO_ROUTE_GRID: avo_nav_grid(); break;
    case AVO_ROUTE_APP: open_app(s_app, LV_SCREEN_LOAD_ANIM_NONE); break;
    case AVO_ROUTE_SUB:
        if (s_sub_build) {
            void (*b)(lv_obj_t *) = s_sub_build;
            void (*l)(void) = s_leave;
            run_leave();
            lv_obj_t *scr = new_screen();
            b(scr);
            s_sub_build = b;
            s_leave = l;
            load(scr, LV_SCREEN_LOAD_ANIM_NONE, AVO_ROUTE_SUB);
        }
        break;
    case AVO_ROUTE_CC: {
        lv_obj_t *scr = new_screen();
        avo_cc_build(scr);
        lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
        break;
    }
    case AVO_ROUTE_NOTIF: {
        lv_obj_t *scr = new_screen();
        avo_notif_build(scr);
        lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true);
        break;
    }
    default: break;
    }
}

void avo_nav_face_select(int index)
{
    if (s_face_tv && index >= 0 && index < avo_faces_count()) {
        lv_tileview_set_tile_by_index(s_face_tv, (uint32_t)index, 0, LV_ANIM_OFF);
    }
}

/* ---------------------------------------------------------------- face screen */
static void face_changed_cb(lv_event_t *e)
{
    lv_obj_t *tile = lv_tileview_get_tile_active(lv_event_get_target(e));
    int idx = (int)lv_obj_get_index(tile);
    if (idx != s_settings.face) {
        s_settings.face = (uint8_t)idx;
        avo_hal_settings_save(&s_settings);
        avo_hal_click();
    }
}

static void face_long_press_cb(lv_event_t *e)
{
    (void)e;
    if (!avo_nav_locked() && s_route == AVO_ROUTE_FACE) {
        avo_nav_app(&AVO_APP_FACES);
    }
}

static void rebuild_face_screen(void)
{
    lv_obj_t *old = s_face_scr;
    avo_faces_forget();
    s_face_scr = new_screen();
    s_face_tv = lv_tileview_create(s_face_scr);
    lv_obj_set_size(s_face_tv, AVO_W, AVO_H);
    lv_obj_set_style_bg_opa(s_face_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_face_tv, LV_SCROLLBAR_MODE_OFF);
    int n = avo_faces_count();
    for (int i = 0; i < n; i++) {
        lv_obj_t *tile = lv_tileview_add_tile(s_face_tv, (uint8_t)i, 0,
                                              (i > 0 ? LV_DIR_LEFT : 0) | (i < n - 1 ? LV_DIR_RIGHT : 0));
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *face = avo_face_create(i, tile);
        lv_obj_add_event_cb(face, face_long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
    }
    lv_tileview_set_tile_by_index(s_face_tv, s_settings.face, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_face_tv, face_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (old) {
        if (lv_screen_active() == old) {
            lv_screen_load(s_face_scr);
        }
        if (s_cc_prev_scr == old) {
            s_cc_prev_scr = s_face_scr;
        }
        safe_delete_screen(old);
    }
}

/* ---------------------------------------------------------------- buttons */
#define BTN_BIT(btn, press) (1 << ((btn) * 3 + (press)))

void avo_ui_post_button(avo_btn_t btn, avo_press_t press) { atomic_fetch_or(&s_btn_events, BTN_BIT(btn, press)); }
void avo_ui_post_wake(void) { atomic_store(&s_wake_req, true); }
void avo_ui_post_double_tap(void) { atomic_store(&s_double_tap, true); }
void avo_ui_post_flick(void) { atomic_store(&s_flick, true); }
void avo_motion_set_probe(void (*probe)(bool flick)) { s_motion_probe = probe; }
void avo_ui_post_sleep(void) { atomic_store(&s_sleep_req, true); }
bool avo_ui_is_awake(void) { return atomic_load(&s_awake); }

static void handle_button(avo_btn_t btn, avo_press_t press)
{
    if (avo_overlay_dismiss()) {
        return; /* first press closes a banner / charging screen */
    }
    if (avo_nav_locked()) {
        return;
    }
    if (btn == AVO_BTN_BOOT) {
        if (press == AVO_PRESS_LONG) {
            return; /* the Smart Stack moved to swipe up */
        }
        if (press == AVO_PRESS_DOUBLE && s_last_app) {
            avo_nav_app(s_last_app);
        } else if (s_route == AVO_ROUTE_FACE) {
            avo_nav_grid();
        } else {
            avo_nav_face();
        }
        return;
    }
    if (press == AVO_PRESS_LONG) {
        avo_pwr_sleep(&s_pwr);
    } else {
        avo_nav_control_center();
    }
}

/* ---------------------------------------------------------------- motion gestures */
/* Double tap: the main action of what is on screen (answer, snooze, stop,
 * open), else play/pause the music. Wrist flick: dismiss, else go home. */
static void handle_double_tap(void)
{
    if (s_motion_probe) {
        s_motion_probe(false);
        return;
    }
    if (avo_overlay_double_tap()) {
        return;
    }
    avo_media_t m;
    avo_hal_media(&m);
    if (m.available && avo_media_recent()) {
        avo_hal_media_command(AVO_AMS_CMD_TOGGLE);
        avo_hal_click();
    }
}

static void handle_flick(void)
{
    if (s_motion_probe) {
        s_motion_probe(true);
        return;
    }
    if (avo_overlay_flick()) {
        return;
    }
    if (s_route != AVO_ROUTE_FACE && !avo_nav_locked() && !s_modal) {
        avo_nav_face();
    }
}

/* ---------------------------------------------------------------- power */
static void enter_state(avo_pwr_state_t st)
{
    bool sleeping = (st == AVO_PWR_AOD || st == AVO_PWR_OFF);
    atomic_store(&s_awake, !sleeping);
    if (sleeping && s_route != AVO_ROUTE_AOD) {
        avo_overlay_dismiss();
        run_leave();
        if (s_route == AVO_ROUTE_CC) {
            cc_discard_prev();
        }
        lv_obj_t *scr = new_screen();
        avo_aod_build(scr);
        load(scr, LV_SCREEN_LOAD_ANIM_NONE, AVO_ROUTE_AOD);
    } else if (!sleeping && s_route == AVO_ROUTE_AOD) {
        avo_nav_face();
    }
    apply_brightness_for_state();
}

void avo_nav_wake(void)
{
    avo_pwr_state_t before = s_pwr.state;
    avo_pwr_activity(&s_pwr, avo_hal_millis());
    if (before != AVO_PWR_ACTIVE) {
        enter_state(AVO_PWR_ACTIVE);
    }
}

/* ---------------------------------------------------------------- periodic checks */
static void check_phone_and_battery(void)
{
    avo_notif_t n;
    if (avo_hal_notif_take_alert(&n)) {
        avo_nav_wake();
        if (n.category == AVO_ANCS_CAT_INCOMING_CALL) {
            avo_overlay_call(&n);
        } else {
            avo_overlay_banner(&n);
        }
    }
    if (s_tick_count % BATTERY_POLL_TICKS == 0) {
        avo_battery_t b;
        avo_hal_battery(&b);
        if (b.usb && !s_prev_usb) {
            avo_nav_wake();
            avo_overlay_charging(&b);
        }
        s_prev_usb = b.usb;
    }
    int16_t off;
    if (s_tick_count % OFFSET_POLL_TICKS == 0 && avo_hal_time_phone_offset(&off) && off != s_settings.utc_offset_min) {
        s_settings.utc_offset_min = off; /* the iPhone knows the time zone best */
        avo_settings_commit();
    }
    avo_media_tick();
    avo_overlays_tick();
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = avo_hal_millis();
    s_tick_count++;
    avo_pwr_state_t before = s_pwr.state;

    int ev = atomic_exchange(&s_btn_events, 0);
    bool woke = false;
    if (ev || atomic_exchange(&s_wake_req, false) || lv_display_get_inactive_time(NULL) < WAKE_ACTIVITY_MS) {
        woke = (before == AVO_PWR_AOD || before == AVO_PWR_OFF);
        avo_pwr_activity(&s_pwr, now);
    }
    if (atomic_exchange(&s_sleep_req, false) && !avo_overlay_keeps_awake()) {
        avo_pwr_sleep(&s_pwr);
    }
    if (avo_overlay_keeps_awake()) {
        avo_pwr_activity(&s_pwr, now); /* a ringing alarm or a call never dims away */
    }
    bool tap = atomic_exchange(&s_double_tap, false);
    bool flick = atomic_exchange(&s_flick, false);
    if ((tap || flick) && (before == AVO_PWR_ACTIVE || before == AVO_PWR_DIM)) {
        avo_pwr_activity(&s_pwr, now);
        if (tap) handle_double_tap();
        if (flick) handle_flick();
    }
    if (ev && !woke) {
        for (int b = 0; b < 2; b++) {
            for (int p = 0; p < 3; p++) {
                if (ev & BTN_BIT(b, p)) {
                    handle_button((avo_btn_t)b, (avo_press_t)p);
                }
            }
        }
    }
    avo_pwr_state_t st = avo_pwr_tick(&s_pwr, now);
    if (st != before) {
        enter_state(st);
    }
    check_phone_and_battery(); /* also while asleep: a call or a charger wakes the watch */
}

static void clock_cb(lv_timer_t *t)
{
    (void)t;
    avo_time_t now;
    avo_hal_time_now(&now);
    avo_alarms_tick(&now); /* also while the screen sleeps */
    if (s_route == AVO_ROUTE_AOD) {
        avo_aod_tick(&now);
        return;
    }
    avo_faces_tick(&now);
    for (int i = 0; i < MAX_HEADER_CLOCKS; i++) {
        if (s_header_clocks[i]) {
            avo_header_clock_refresh(s_header_clocks[i], &now);
        }
    }
}

/* ---------------------------------------------------------------- start */
static void splash_done_cb(lv_timer_t *t)
{
    (void)t;
    s_route = AVO_ROUTE_FACE;
    /* auto_del: LVGL frees the splash only after the fade has finished */
    lv_screen_load_anim(s_face_scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 300, 0, true);
}

static lv_obj_t *build_splash(void)
{
    lv_obj_t *scr = avo_screen_create();
    bool avo = avo_theme_is_avocado();
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, avo ? &avo_img_mark_160 : &avo_img_mark_56);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, avo ? -40 : -30);
    lv_obj_t *word = avo_label(scr, &avo_font_40, avo_pal()->label, "avocatOS");
    lv_obj_align_to(word, img, LV_ALIGN_OUT_BOTTOM_MID, 0, avo ? 18 : 14);
    return scr;
}

void avo_ui_start(void)
{
    if (!avo_hal_settings_load(&s_settings)) {
        avo_settings_defaults(&s_settings);
    }
    avo_settings_sanitize(&s_settings, AVO_FACE_MAX);
    avo_hal_settings_save(&s_settings); /* first boot / upgrade: board code reads the saved copy */
    avo_alarms_init();
    s_applied_theme = (avo_theme_mode_t)s_settings.theme;
    avo_theme_init(s_applied_theme);
    if (s_settings.face >= avo_faces_count()) {
        s_settings.face = 0;
    }

    avo_hal_time_set_utc_offset(s_settings.utc_offset_min);
    avo_hal_bt_enable(s_settings.bluetooth);
    avo_hal_wifi_enable(s_settings.wifi);
    avo_pwr_init(&s_pwr, &(avo_pwr_cfg_t){ 0 }, avo_hal_millis());
    pwr_config_from_settings(avo_hal_millis());
    apply_brightness_for_state();
    if (s_settings.show_fps) {
        lv_sysmon_show_performance(NULL);
    } else {
        lv_sysmon_hide_performance(NULL);
    }
    avo_battery_t b;
    avo_hal_battery(&b);
    s_prev_usb = b.usb; /* no charging animation for a cable that was already there */

    lv_obj_t *splash = build_splash();
    lv_screen_load(splash);
    rebuild_face_screen();
    s_route = AVO_ROUTE_AOD; /* ignore input until the face is shown */
    lv_timer_t *st = lv_timer_create(splash_done_cb, SPLASH_MS, NULL);
    lv_timer_set_repeat_count(st, 1);

    avo_gesture_install();
    lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_timer_create(clock_cb, 1000, NULL);
}
