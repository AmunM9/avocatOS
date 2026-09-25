/*
 * System swipes. LVGL's own gesture detection gives up as soon as a
 * scrollable parent (the face tileview, a list) claims the drag, which made
 * vertical swipes unreliable. Instead, the pointer read callback is wrapped
 * and every raw sample goes through avo_swipe_feed(); the resulting swipe is
 * mapped to an action for the current screen and run after LVGL finishes
 * processing the input.
 */
#include "avo_ui_internal.h"

/* A swipe that closes a list screen must start on the screen edge (like the
 * home indicator), otherwise it is just scrolling the list. */
#define EDGE_TOP 56
#define EDGE_BOTTOM (AVO_H - 56)

typedef enum {
    ACT_NONE = 0,
    ACT_CONTROL_CENTER,  /* open or close */
    ACT_NOTIFICATIONS,
    ACT_FACE,
    ACT_BACK,
    ACT_DISMISS_OVERLAY,
} action_t;

static lv_indev_read_cb_t s_orig_read;
static avo_swipe_tracker_t s_tracker;
static action_t s_pending;

/* scroll position of the screen's list when the finger touched down: the
 * swipe itself scrolls the list, so reading it at lift time is too late */
static struct {
    bool at_top;
    bool at_bottom;
    bool scrollable;
} s_press;

static void capture_scroll_state(void)
{
    lv_obj_t *s = avo_nav_scroller();
    int32_t top = s ? lv_obj_get_scroll_top(s) : 0;
    int32_t bottom = s ? lv_obj_get_scroll_bottom(s) : 0;
    s_press.at_top = top <= 0;
    s_press.at_bottom = bottom <= 0;
    s_press.scrollable = top > 0 || bottom > 0;
}

static action_t map_swipe(const avo_swipe_t *sw)
{
    if (avo_overlay_active()) {
        return (sw->dir == AVO_SWIPE_UP || sw->dir == AVO_SWIPE_RIGHT) ? ACT_DISMISS_OVERLAY : ACT_NONE;
    }
    if (avo_nav_modal() || avo_nav_locked()) {
        return ACT_NONE;
    }
    switch (avo_nav_route()) {
    case AVO_ROUTE_FACE:
        if (sw->dir == AVO_SWIPE_UP) return ACT_CONTROL_CENTER;
        if (sw->dir == AVO_SWIPE_DOWN) return ACT_NOTIFICATIONS;
        return ACT_NONE; /* left/right pages the faces */
    case AVO_ROUTE_CC:
        /* the Control Center may hold a Now Playing page below it */
        if (sw->dir == AVO_SWIPE_RIGHT) return ACT_CONTROL_CENTER;
        return (sw->dir == AVO_SWIPE_DOWN && s_press.at_top) ? ACT_CONTROL_CENTER : ACT_NONE;
    case AVO_ROUTE_NOTIF:
        if (sw->dir == AVO_SWIPE_RIGHT) return ACT_FACE;
        if (sw->dir == AVO_SWIPE_UP && (sw->start_y >= EDGE_BOTTOM || !s_press.scrollable)) return ACT_FACE;
        return ACT_NONE;
    case AVO_ROUTE_STACK:
        if (sw->dir == AVO_SWIPE_RIGHT) return ACT_FACE;
        if (sw->dir == AVO_SWIPE_DOWN && (sw->start_y <= EDGE_TOP || s_press.at_top)) return ACT_FACE;
        return ACT_NONE;
    case AVO_ROUTE_APP:
    case AVO_ROUTE_SUB:
        return sw->dir == AVO_SWIPE_RIGHT ? ACT_BACK : ACT_NONE;
    default:
        return ACT_NONE;
    }
}

static void run_pending(void *arg)
{
    (void)arg;
    action_t a = s_pending;
    s_pending = ACT_NONE;
    avo_hal_click();
    switch (a) {
    case ACT_CONTROL_CENTER: avo_nav_control_center(); break;
    case ACT_NOTIFICATIONS: avo_nav_notifications(); break;
    case ACT_FACE: avo_nav_face(); break;
    case ACT_BACK: avo_nav_back(); break;
    case ACT_DISMISS_OVERLAY: avo_overlay_dismiss(); break;
    default: break;
    }
}

static void read_wrapper(lv_indev_t *indev, lv_indev_data_t *data)
{
    s_orig_read(indev, data);
    avo_swipe_t sw;
    bool pressed = data->state == LV_INDEV_STATE_PRESSED;
    if (pressed && !s_tracker.down) {
        capture_scroll_state();
    }
    if (!avo_swipe_feed(&s_tracker, pressed, (int16_t)data->point.x, (int16_t)data->point.y, lv_tick_get(), &sw)) {
        return;
    }
    action_t a = map_swipe(&sw);
    if (a == ACT_NONE || s_pending != ACT_NONE) {
        return;
    }
    s_pending = a;
    if (!lv_indev_get_scroll_obj(indev)) {
        /* no scroll ran: forget the press so the lift does not also "click" */
        lv_indev_reset(indev, NULL);
    }
    lv_async_call(run_pending, NULL);
}

void avo_gesture_install(void)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER && lv_indev_get_read_cb(i) != read_wrapper) {
            s_orig_read = lv_indev_get_read_cb(i);
            lv_indev_set_read_cb(i, read_wrapper);
            avo_swipe_reset(&s_tracker);
            return;
        }
    }
}
