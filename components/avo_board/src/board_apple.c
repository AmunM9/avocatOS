/*
 * iPhone services over the encrypted BLE link (GATT client, peripheral role):
 *   ANCS  notifications (list, alerts, accept/decline calls, clear)
 *   AMS   media remote (track, artist, state, volume; play/pause/next/prev)
 *   CTS   current time + time zone
 * Everything runs in the NimBLE host task as a chain of GATT procedures;
 * results live in a mutex-protected store read by the HAL functions.
 * Parsing is done by the unit-tested helpers in avo_core (avo_apple.c).
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "host/ble_hs.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function" /* FreeRTOS atomic.h helpers */
#include "nimble/nimble_port.h"
#pragma GCC diagnostic pop
#include "board_priv.h"

static const char *TAG = "board_apple";

/* ---------------------------------------------------------------- UUIDs */
static const ble_uuid128_t UUID_ANCS_SVC = BLE_UUID128_INIT(0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4, 0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79);
static const ble_uuid128_t UUID_ANCS_NS = BLE_UUID128_INIT(0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c, 0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f);
static const ble_uuid128_t UUID_ANCS_CP = BLE_UUID128_INIT(0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98, 0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69);
static const ble_uuid128_t UUID_ANCS_DS = BLE_UUID128_INIT(0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe, 0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22);
static const ble_uuid128_t UUID_AMS_SVC = BLE_UUID128_INIT(0xdc, 0xf8, 0x55, 0xad, 0x02, 0xc5, 0xf4, 0x8e, 0x3a, 0x43, 0x36, 0x0f, 0x2b, 0x50, 0xd3, 0x89);
static const ble_uuid128_t UUID_AMS_RC = BLE_UUID128_INIT(0xc2, 0x51, 0xca, 0xf7, 0x56, 0x0e, 0xdf, 0xb8, 0x8a, 0x4a, 0xb1, 0x57, 0xd8, 0x81, 0x3c, 0x9b);
static const ble_uuid128_t UUID_AMS_EU = BLE_UUID128_INIT(0x02, 0xc1, 0x96, 0xba, 0x92, 0xbb, 0x0c, 0x9a, 0x1f, 0x41, 0x8d, 0x80, 0xce, 0xab, 0x7c, 0x2f);
static const ble_uuid16_t UUID_CTS_SVC = BLE_UUID16_INIT(0x1805);
static const ble_uuid16_t UUID_CTS_TIME = BLE_UUID16_INIT(0x2A2B);
static const ble_uuid16_t UUID_CTS_LOCAL = BLE_UUID16_INIT(0x2A0F);
static const ble_uuid16_t UUID_CCCD = BLE_UUID16_INIT(0x2902);

#define DS_BUF_SIZE 768
#define QUEUE_MAX 16
#define REQUEST_TIMEOUT_US (3 * 1000 * 1000)

enum { SVC_ANCS, SVC_AMS, SVC_CTS, SVC_COUNT };
enum { C_NS, C_CP, C_DS, C_RC, C_EU, C_TIME, C_LOCAL, C_COUNT };

typedef struct {
    uint16_t def, val, end, cccd;
} chr_t;

/* which characteristics to subscribe to, in this order (Data Source first) */
static const int SUBSCRIBE_ORDER[] = { C_DS, C_NS, C_EU, C_TIME };
#define SUBSCRIBE_N (sizeof SUBSCRIBE_ORDER / sizeof SUBSCRIBE_ORDER[0])

typedef enum {
    ST_IDLE, ST_SVCS, ST_CHRS, ST_DSCS, ST_SUBSCRIBE, ST_AMS_PLAYER, ST_AMS_TRACK, ST_CTS_LOCAL, ST_CTS_TIME, ST_READY,
} step_t;

typedef struct {
    uint32_t uid;
    uint8_t category, flags;
    bool full;        /* the whole message of an opened notification */
} pending_t;

static struct {
    uint16_t conn;
    step_t step;
    int idx;          /* service / characteristic index inside the step  */
    int last_chr;     /* previous characteristic, to close its range     */
    uint16_t svc_start[SVC_COUNT], svc_end[SVC_COUNT];
    chr_t chr[C_COUNT];
    /* ANCS request pipeline */
    pending_t queue[QUEUE_MAX];
    int q_len;
    bool outstanding;
    int64_t sent_at;
    uint8_t ds[DS_BUF_SIZE];
    size_t ds_len;
    pending_t current;
} ap;

/* ---------------------------------------------------------------- store (shared with the UI) */
static SemaphoreHandle_t s_mtx;
static avo_notif_t s_notifs[AVO_NOTIF_MAX];
static int s_notif_count;
static uint32_t s_notif_version;
static avo_notif_t s_alert;
static bool s_alert_pending;
static avo_media_t s_media;

#define LOCK() xSemaphoreTake(s_mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mtx)

static void step_next(void);

/* The whole message of the notification opened on the watch. The response
 * (up to AVO_ANCS_FULL_MAX bytes over several packets) is assembled in PSRAM. */
#define FULL_DS_SIZE (AVO_ANCS_FULL_MAX + 16)
static struct {
    uint32_t uid;
    bool ready;
    char *text;                 /* guarded by s_mtx */
    uint8_t *ds;                /* host task only   */
    size_t ds_len;
} s_full;
static struct ble_npl_event s_full_ev;
static volatile uint32_t s_full_want;

/* ================================================================= store helpers */

static int notif_find(uint32_t uid)
{
    for (int i = 0; i < s_notif_count; i++) {
        if (s_notifs[i].uid == uid) {
            return i;
        }
    }
    return -1;
}

static void notif_remove_locked(uint32_t uid)
{
    int i = notif_find(uid);
    if (i < 0) {
        return;
    }
    memmove(&s_notifs[i], &s_notifs[i + 1], (size_t)(s_notif_count - i - 1) * sizeof s_notifs[0]);
    s_notif_count--;
    s_notif_version++;
}

static void notif_store(const avo_ancs_attrs_t *a, const pending_t *p)
{
    avo_notif_t n = { .uid = a->uid, .category = p->category };
    avo_app_display_name(a->app_id, n.app, sizeof n.app);
    strlcpy(n.title, a->title, sizeof n.title);
    strlcpy(n.message, a->message, sizeof n.message);
    avo_text_clean(n.title);   /* skin tones, joiners... the emoji font lacks them */
    avo_text_clean(n.message);
    avo_hal_time_now(&n.when);

    LOCK();
    notif_remove_locked(n.uid); /* a modification moves it to the top */
    int keep = s_notif_count < AVO_NOTIF_MAX ? s_notif_count : AVO_NOTIF_MAX - 1;
    memmove(&s_notifs[1], &s_notifs[0], (size_t)keep * sizeof s_notifs[0]);
    s_notifs[0] = n;
    s_notif_count = keep + 1;
    s_notif_version++;
    if (!(p->flags & (AVO_ANCS_FLAG_SILENT | AVO_ANCS_FLAG_PRE_EXISTING))) {
        s_alert = n;
        s_alert_pending = true;
    }
    UNLOCK();
}

/* ================================================================= ANCS request pipeline */

static int cp_write_cb(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    if (err->status != 0) {
        /* e.g. the notification vanished before we asked: skip it */
        ap.outstanding = false;
        ap.ds_len = 0;
    }
    return 0;
}

static void ancs_pump(void)
{
    if (ap.outstanding && esp_timer_get_time() - ap.sent_at > REQUEST_TIMEOUT_US) {
        ap.outstanding = false;
        ap.ds_len = 0;
    }
    if (ap.outstanding || ap.q_len == 0 || !ap.chr[C_CP].val) {
        return;
    }
    ap.current = ap.queue[0];
    memmove(&ap.queue[0], &ap.queue[1], (size_t)(ap.q_len - 1) * sizeof ap.queue[0]);
    ap.q_len--;
    uint8_t req[16];
    size_t n = ap.current.full ? avo_ancs_build_get_message(ap.current.uid, AVO_ANCS_FULL_MAX - 1, req, sizeof req)
                               : avo_ancs_build_get_attrs(ap.current.uid, req, sizeof req);
    ap.ds_len = 0;
    s_full.ds_len = 0;
    ap.outstanding = ble_gattc_write_flat(ap.conn, ap.chr[C_CP].val, req, (uint16_t)n, cp_write_cb, NULL) == 0;
    ap.sent_at = esp_timer_get_time();
}

static void on_notification_source(const uint8_t *d, size_t n)
{
    avo_ancs_source_t s;
    if (!avo_ancs_parse_source(d, n, &s)) {
        return;
    }
    if (s.event_id == AVO_ANCS_REMOVED) {
        LOCK();
        notif_remove_locked(s.uid);
        UNLOCK();
        return;
    }
    if (ap.q_len < QUEUE_MAX) {
        ap.queue[ap.q_len++] = (pending_t){ .uid = s.uid, .category = s.category, .flags = s.flags };
    }
    ancs_pump();
}

static void on_full_message(const uint8_t *d, size_t n)
{
    if (!s_full.ds || s_full.ds_len + n > FULL_DS_SIZE) {
        s_full.ds_len = 0;
        ap.outstanding = false;
        ancs_pump();
        return;
    }
    memcpy(s_full.ds + s_full.ds_len, d, n);
    s_full.ds_len += n;
    uint32_t uid = 0;
    LOCK();
    int used = avo_ancs_parse_message(s_full.ds, s_full.ds_len, &uid, s_full.text, AVO_ANCS_FULL_MAX);
    if (used > 0) {
        avo_text_clean(s_full.text);
        s_full.uid = uid;
        s_full.ready = true;
    }
    UNLOCK();
    if (used == 0) {
        return; /* wait for the next fragment */
    }
    s_full.ds_len = 0;
    ap.outstanding = false;
    ancs_pump();
}

static void on_data_source(const uint8_t *d, size_t n)
{
    if (ap.current.full) {
        on_full_message(d, n);
        return;
    }
    if (ap.ds_len + n > sizeof ap.ds) {
        ap.ds_len = 0; /* runaway response: drop it */
        ap.outstanding = false;
        ancs_pump();
        return;
    }
    memcpy(ap.ds + ap.ds_len, d, n);
    ap.ds_len += n;
    avo_ancs_attrs_t a;
    int used = avo_ancs_parse_attrs(ap.ds, ap.ds_len, &a);
    if (used == 0) {
        return; /* wait for the next fragment */
    }
    if (used > 0) {
        notif_store(&a, &ap.current);
    }
    ap.ds_len = 0;
    ap.outstanding = false;
    ancs_pump();
}

/* ================================================================= AMS */

static void on_entity_update(const uint8_t *d, size_t n)
{
    avo_ams_update_t u;
    if (!avo_ams_parse_update(d, n, &u)) {
        return;
    }
    avo_text_clean(u.value);
    LOCK();
    if (u.entity == AVO_AMS_ENTITY_PLAYER) {
        if (u.attr == AVO_AMS_PLAYER_NAME) {
            strlcpy(s_media.app, u.value, sizeof s_media.app);
        } else if (u.attr == AVO_AMS_PLAYER_PLAYBACK) {
            s_media.playing = avo_ams_playback_state(u.value) == 1;
        } else if (u.attr == AVO_AMS_PLAYER_VOLUME) {
            s_media.volume = (uint8_t)(atof(u.value) * 100.0 + 0.5);
        }
    } else if (u.entity == AVO_AMS_ENTITY_TRACK) {
        if (u.attr == AVO_AMS_TRACK_TITLE) {
            strlcpy(s_media.title, u.value, sizeof s_media.title);
        } else if (u.attr == AVO_AMS_TRACK_ARTIST) {
            strlcpy(s_media.artist, u.value, sizeof s_media.artist);
        } else if (u.attr == AVO_AMS_TRACK_ALBUM) {
            strlcpy(s_media.album, u.value, sizeof s_media.album);
        }
    }
    s_media.version++;
    UNLOCK();
}

/* ================================================================= CTS */

static int16_t s_phone_offset = INT16_MIN;

static void apply_phone_time(const uint8_t *d, size_t n)
{
    avo_time_t local;
    if (!avo_cts_parse_time(d, n, &local)) {
        return;
    }
    int16_t off = s_phone_offset != INT16_MIN ? s_phone_offset : board_time_utc_offset();
    struct timeval tv = { .tv_sec = (time_t)avo_time_to_epoch(&local, off) };
    settimeofday(&tv, NULL);
    board_rtc_store(tv.tv_sec);
    board_time_mark_synced(AVO_TIME_SRC_PHONE);
    ESP_LOGI(TAG, "time from iPhone: %04d-%02d-%02d %02d:%02d (UTC%+d min)", local.year, local.month, local.day,
             local.hour, local.min, off);
}

/* ================================================================= notifications from the phone */

void board_apple_on_notify(uint16_t handle, const struct os_mbuf *om)
{
    uint8_t buf[256];
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len > sizeof buf) {
        len = sizeof buf;
    }
    if (ble_hs_mbuf_to_flat(om, buf, len, &len) != 0 || handle == 0) {
        return;
    }
    if (handle == ap.chr[C_NS].val) {
        on_notification_source(buf, len);
    } else if (handle == ap.chr[C_DS].val) {
        on_data_source(buf, len);
    } else if (handle == ap.chr[C_EU].val) {
        on_entity_update(buf, len);
    } else if (handle == ap.chr[C_TIME].val) {
        apply_phone_time(buf, len);
    }
}

/* ================================================================= discovery chain */

static int svc_cb(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    (void)conn;
    (void)arg;
    if (err->status == 0 && svc) {
        int i = -1;
        if (ble_uuid_cmp(&svc->uuid.u, &UUID_ANCS_SVC.u) == 0) i = SVC_ANCS;
        else if (ble_uuid_cmp(&svc->uuid.u, &UUID_AMS_SVC.u) == 0) i = SVC_AMS;
        else if (ble_uuid_cmp(&svc->uuid.u, &UUID_CTS_SVC.u) == 0) i = SVC_CTS;
        if (i >= 0) {
            ap.svc_start[i] = svc->start_handle;
            ap.svc_end[i] = svc->end_handle;
        }
        return 0;
    }
    step_next(); /* BLE_HS_EDONE or error: move on with what we found */
    return 0;
}

static int chr_index(const ble_uuid_t *u)
{
    if (ble_uuid_cmp(u, &UUID_ANCS_NS.u) == 0) return C_NS;
    if (ble_uuid_cmp(u, &UUID_ANCS_CP.u) == 0) return C_CP;
    if (ble_uuid_cmp(u, &UUID_ANCS_DS.u) == 0) return C_DS;
    if (ble_uuid_cmp(u, &UUID_AMS_RC.u) == 0) return C_RC;
    if (ble_uuid_cmp(u, &UUID_AMS_EU.u) == 0) return C_EU;
    if (ble_uuid_cmp(u, &UUID_CTS_TIME.u) == 0) return C_TIME;
    if (ble_uuid_cmp(u, &UUID_CTS_LOCAL.u) == 0) return C_LOCAL;
    return -1;
}

static int chr_cb(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn;
    int svc = (int)(intptr_t)arg;
    if (err->status == 0 && chr) {
        if (ap.last_chr >= 0) {
            ap.chr[ap.last_chr].end = chr->def_handle - 1;
        }
        int i = chr_index(&chr->uuid.u);
        ap.last_chr = i;
        if (i >= 0) {
            ap.chr[i].def = chr->def_handle;
            ap.chr[i].val = chr->val_handle;
        }
        return 0;
    }
    if (ap.last_chr >= 0) {
        ap.chr[ap.last_chr].end = ap.svc_end[svc];
    }
    step_next();
    return 0;
}

static int dsc_cb(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn;
    (void)chr_val;
    int c = (int)(intptr_t)arg;
    if (err->status == 0 && dsc) {
        if (ble_uuid_cmp(&dsc->uuid.u, &UUID_CCCD.u) == 0) {
            ap.chr[c].cccd = dsc->handle;
        }
        return 0;
    }
    step_next();
    return 0;
}

static int write_done_cb(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn;
    (void)attr;
    (void)arg;
    if (err->status != 0) {
        ESP_LOGW(TAG, "write failed at step %d (0x%x)", ap.step, err->status);
    }
    step_next();
    return 0;
}

static int read_done_cb(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn;
    (void)arg;
    if (err->status == 0 && attr && attr->om) {
        uint8_t b[16];
        uint16_t len = 0;
        if (ble_hs_mbuf_to_flat(attr->om, b, sizeof b, &len) == 0) {
            if (ap.step == ST_CTS_LOCAL) {
                int16_t off;
                if (avo_cts_parse_local_info(b, len, &off)) {
                    s_phone_offset = off;
                    board_time_set_phone_offset(off);
                }
            } else {
                apply_phone_time(b, len);
            }
        }
    }
    step_next(); /* a missing characteristic must not stall the chain */
    return 0;
}

/* Advance the chain by one GATT procedure. Called from each completion. */
static void step_next(void)
{
    static const uint8_t cccd_on[2] = { 0x01, 0x00 };
    static const uint8_t ams_player[] = { AVO_AMS_ENTITY_PLAYER, AVO_AMS_PLAYER_NAME, AVO_AMS_PLAYER_PLAYBACK, AVO_AMS_PLAYER_VOLUME };
    static const uint8_t ams_track[] = { AVO_AMS_ENTITY_TRACK, AVO_AMS_TRACK_ARTIST, AVO_AMS_TRACK_ALBUM, AVO_AMS_TRACK_TITLE };
    if (ap.conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    for (;;) {
        switch (ap.step) {
        case ST_IDLE:
            ap.step = ST_SVCS;
            if (ble_gattc_disc_all_svcs(ap.conn, svc_cb, NULL) == 0) return;
            break;
        case ST_SVCS:
            ap.step = ST_CHRS;
            ap.idx = -1;
            /* fallthrough */
        case ST_CHRS:
            while (++ap.idx < SVC_COUNT) {
                if (ap.svc_start[ap.idx]) {
                    ap.last_chr = -1;
                    if (ble_gattc_disc_all_chrs(ap.conn, ap.svc_start[ap.idx], ap.svc_end[ap.idx], chr_cb,
                                                (void *)(intptr_t)ap.idx) == 0) return;
                }
            }
            ap.step = ST_DSCS;
            ap.idx = -1;
            /* fallthrough */
        case ST_DSCS:
            while (++ap.idx < (int)SUBSCRIBE_N) {
                chr_t *c = &ap.chr[SUBSCRIBE_ORDER[ap.idx]];
                if (c->val && c->end > c->val) {
                    if (ble_gattc_disc_all_dscs(ap.conn, c->val, c->end, dsc_cb,
                                                (void *)(intptr_t)SUBSCRIBE_ORDER[ap.idx]) == 0) return;
                }
            }
            ap.step = ST_SUBSCRIBE;
            ap.idx = -1;
            /* fallthrough */
        case ST_SUBSCRIBE:
            while (++ap.idx < (int)SUBSCRIBE_N) {
                chr_t *c = &ap.chr[SUBSCRIBE_ORDER[ap.idx]];
                if (c->cccd && ble_gattc_write_flat(ap.conn, c->cccd, cccd_on, sizeof cccd_on, write_done_cb, NULL) == 0) {
                    return;
                }
            }
            ap.step = ST_AMS_PLAYER;
            if (ap.chr[C_EU].val &&
                ble_gattc_write_flat(ap.conn, ap.chr[C_EU].val, ams_player, sizeof ams_player, write_done_cb, NULL) == 0) {
                return;
            }
            /* fallthrough */
        case ST_AMS_PLAYER:
            ap.step = ST_AMS_TRACK;
            if (ap.chr[C_EU].val &&
                ble_gattc_write_flat(ap.conn, ap.chr[C_EU].val, ams_track, sizeof ams_track, write_done_cb, NULL) == 0) {
                return;
            }
            /* fallthrough */
        case ST_AMS_TRACK:
            LOCK();
            s_media.available = ap.chr[C_EU].cccd != 0 && ap.chr[C_RC].val != 0;
            s_media.version++;
            UNLOCK();
            ap.step = ST_CTS_LOCAL;
            if (ap.chr[C_LOCAL].val && ble_gattc_read(ap.conn, ap.chr[C_LOCAL].val, read_done_cb, NULL) == 0) {
                return;
            }
            /* fallthrough */
        case ST_CTS_LOCAL:
            ap.step = ST_CTS_TIME;
            if (ap.chr[C_TIME].val && ble_gattc_read(ap.conn, ap.chr[C_TIME].val, read_done_cb, NULL) == 0) {
                return;
            }
            /* fallthrough */
        case ST_CTS_TIME:
            ap.step = ST_READY;
            board_ble_set_phone_state(AVO_PHONE_READY);
            ESP_LOGI(TAG, "iPhone ready: ANCS %s, AMS %s, CTS %s", ap.chr[C_NS].cccd ? "yes" : "no",
                     ap.chr[C_EU].cccd ? "yes" : "no", ap.chr[C_TIME].val ? "yes" : "no");
            ancs_pump();
            return;
        case ST_READY:
        default:
            return;
        }
    }
}

/* ================================================================= lifecycle */

/* Runs in the NimBLE host task: the request goes first in the queue. */
static void full_request_ev(struct ble_npl_event *ev)
{
    (void)ev;
    if (ap.conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    if (ap.q_len == QUEUE_MAX) {
        ap.q_len--; /* the oldest pending title can wait for the next event */
    }
    memmove(&ap.queue[1], &ap.queue[0], (size_t)ap.q_len * sizeof ap.queue[0]);
    ap.queue[0] = (pending_t){ .uid = s_full_want, .full = true };
    ap.q_len++;
    ancs_pump();
}

void board_apple_init(void)
{
    if (!s_mtx) {
        s_mtx = xSemaphoreCreateMutex();
        s_full.text = heap_caps_calloc(1, AVO_ANCS_FULL_MAX, MALLOC_CAP_SPIRAM);
        s_full.ds = heap_caps_malloc(FULL_DS_SIZE, MALLOC_CAP_SPIRAM);
        ble_npl_event_init(&s_full_ev, full_request_ev, NULL);
    }
    memset(&ap, 0, sizeof ap);
    ap.conn = BLE_HS_CONN_HANDLE_NONE;
}

void board_apple_start(uint16_t conn)
{
    memset(&ap, 0, sizeof ap);
    ap.conn = conn;
    ap.step = ST_IDLE;
    step_next();
}

void board_apple_stop(void)
{
    ap.conn = BLE_HS_CONN_HANDLE_NONE;
    ap.step = ST_IDLE;
    ap.q_len = 0;
    ap.outstanding = false;
    LOCK();
    s_media.available = false;
    s_media.playing = false;
    s_media.version++;
    UNLOCK();
}

/* ================================================================= HAL */

uint32_t avo_hal_notif_version(void)
{
    return s_notif_version;
}

int avo_hal_notif_list(avo_notif_t *out, int max)
{
    if (!s_mtx) {
        return 0;
    }
    LOCK();
    int n = s_notif_count < max ? s_notif_count : max;
    memcpy(out, s_notifs, (size_t)n * sizeof *out);
    UNLOCK();
    return n;
}

bool avo_hal_notif_take_alert(avo_notif_t *out)
{
    if (!s_mtx) {
        return false;
    }
    LOCK();
    bool had = s_alert_pending;
    if (had) {
        *out = s_alert;
        s_alert_pending = false;
    }
    UNLOCK();
    return had;
}

void avo_hal_notif_action(uint32_t uid, bool positive)
{
    uint8_t b[8];
    size_t n = avo_ancs_build_action(uid, positive, b, sizeof b);
    if (ap.conn != BLE_HS_CONN_HANDLE_NONE && ap.chr[C_CP].val) {
        ble_gattc_write_flat(ap.conn, ap.chr[C_CP].val, b, (uint16_t)n, NULL, NULL);
    }
    if (!positive) {
        avo_hal_notif_dismiss_local(uid);
    }
}

void avo_hal_notif_request_full(uint32_t uid)
{
    if (!s_mtx || !s_full.text) {
        return;
    }
    LOCK();
    s_full.ready = false;
    s_full.uid = uid;
    UNLOCK();
    s_full_want = uid;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_full_ev);
}

bool avo_hal_notif_full(uint32_t uid, char *out, size_t cap)
{
    if (!s_mtx || !s_full.text) {
        return false;
    }
    LOCK();
    bool ok = s_full.ready && s_full.uid == uid;
    if (ok) {
        strlcpy(out, s_full.text, cap);
    }
    UNLOCK();
    return ok;
}

void avo_hal_notif_dismiss_local(uint32_t uid)
{
    if (!s_mtx) {
        return;
    }
    LOCK();
    notif_remove_locked(uid);
    UNLOCK();
}

void avo_hal_media(avo_media_t *out)
{
    if (!s_mtx) {
        memset(out, 0, sizeof *out);
        return;
    }
    LOCK();
    *out = s_media;
    UNLOCK();
}

bool avo_hal_media_command(uint8_t cmd)
{
    if (ap.conn == BLE_HS_CONN_HANDLE_NONE || !ap.chr[C_RC].val) {
        return false;
    }
    return ble_gattc_write_flat(ap.conn, ap.chr[C_RC].val, &cmd, 1, NULL, NULL) == 0;
}
