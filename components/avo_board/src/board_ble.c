/*
 * Bluetooth LE peripheral (NimBLE):
 *  - advertises the ANCS service solicitation so the watch shows up in the
 *    iPhone's Settings > Bluetooth list,
 *  - pairs with LE Secure Connections ("Just Works") and keeps the bond in
 *    NVS so the iPhone reconnects on its own,
 *  - hands the encrypted link to board_apple.c (ANCS / AMS / CTS).
 */
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "board_priv.h"

static const char *TAG = "board_ble";

#define BT_NAME "avocatOS"
#define APPEARANCE_WATCH 0x00C0
#define ADV_ITVL_MIN BLE_GAP_ADV_ITVL_MS(200)
#define ADV_ITVL_MAX BLE_GAP_ADV_ITVL_MS(400)

/* ANCS service, advertised as a solicitation (7905F431-B5CE-4E99-A40F-4B1E122D00D0) */
static const ble_uuid128_t ANCS_SOLICIT = BLE_UUID128_INIT(0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4, 0x99, 0x4e,
                                                           0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79);

void ble_store_config_init(void);

static struct {
    bool stack_ready, want_on;
    volatile bool synced;
    volatile avo_phone_state_t phone;
    volatile uint16_t conn;
} bt = { .conn = BLE_HS_CONN_HANDLE_NONE };

static int gap_event_cb(struct ble_gap_event *event, void *arg);

#define SECURE_DELAY_US (1500 * 1000) /* give a bonded iPhone time to encrypt first */

static esp_timer_handle_t s_secure_timer;
static volatile bool s_services_started;

static bool link_encrypted(uint16_t conn)
{
    struct ble_gap_conn_desc d;
    return ble_gap_conn_find(conn, &d) == 0 && d.sec_state.encrypted;
}

/* Only ask for pairing when the iPhone did not encrypt the link itself:
 * starting a second security procedure on an encrypted link makes iOS
 * ignore it and the attempt times out 30 s later. */
static void secure_timer_cb(void *arg)
{
    (void)arg;
    uint16_t c = bt.conn;
    if (c == BLE_HS_CONN_HANDLE_NONE || link_encrypted(c)) {
        return;
    }
    if (ble_gap_security_initiate(c) != 0) {
        ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void start_services_once(uint16_t conn)
{
    if (!s_services_started) {
        s_services_started = true;
        ESP_LOGI(TAG, "link encrypted, starting iPhone services");
        board_apple_start(conn);
    }
}

void board_ble_set_phone_state(avo_phone_state_t st) { bt.phone = st; }
uint16_t board_ble_conn(void) { return bt.conn; }

static void advertise(void)
{
    if (!bt.synced || !bt.want_on || bt.conn != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) {
        return;
    }
    /* 3 (flags) + 18 (ANCS solicitation) + 10 (name) = 31 bytes: the name
     * must be in the main packet or iOS lists the watch as "Accessory". */
    struct ble_hs_adv_fields adv = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .sol_uuids128 = &ANCS_SOLICIT,
        .sol_num_uuids128 = 1,
        .name = (const uint8_t *)BT_NAME,
        .name_len = sizeof(BT_NAME) - 1,
        .name_is_complete = 1,
    };
    struct ble_hs_adv_fields rsp = {
        .appearance = APPEARANCE_WATCH,
        .appearance_is_present = 1,
        .tx_pwr_lvl_is_present = 1,
        .tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO,
    };
    int rc = ble_gap_adv_set_fields(&adv);
    if (rc == 0) {
        rc = ble_gap_adv_rsp_set_fields(&rsp);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "advertising data rejected (%d)", rc);
        return;
    }
    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = ADV_ITVL_MIN,
        .itvl_max = ADV_ITVL_MAX,
    };
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc == 0) {
        bt.phone = AVO_PHONE_WAITING;
        ESP_LOGI(TAG, "advertising as %s (ANCS solicitation)", BT_NAME);
    } else {
        ESP_LOGE(TAG, "advertising failed (%d)", rc);
    }
}

static void forget_peer_of(uint16_t conn)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) == 0) {
        ble_store_util_delete_peer(&desc.peer_id_addr);
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            advertise();
            break;
        }
        bt.conn = event->connect.conn_handle;
        bt.phone = AVO_PHONE_PAIRING;
        s_services_started = false;
        ESP_LOGI(TAG, "iPhone connected, securing link");
        ble_gattc_exchange_mtu(bt.conn, NULL, NULL);
        esp_timer_stop(s_secure_timer);
        esp_timer_start_once(s_secure_timer, SECURE_DELAY_US);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        uint16_t c = event->enc_change.conn_handle;
        int st = event->enc_change.status;
        if (st == 0 || link_encrypted(c)) {
            start_services_once(c);
        } else if (st == BLE_HS_ERR_HCI_BASE + BLE_ERR_PINKEY_MISSING) {
            /* the iPhone lost our keys: forget the bond so it can pair again */
            ESP_LOGW(TAG, "iPhone has no keys for us (%d), clearing bond", st);
            forget_peer_of(c);
            ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM);
        } else {
            ESP_LOGW(TAG, "security procedure failed (%d), keeping the bond", st);
        }
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        forget_peer_of(event->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason %d)", event->disconnect.reason);
        bt.conn = BLE_HS_CONN_HANDLE_NONE;
        esp_timer_stop(s_secure_timer);
        s_services_started = false;
        board_apple_stop();
        bt.phone = bt.want_on ? AVO_PHONE_WAITING : AVO_PHONE_OFF;
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        board_apple_on_notify(event->notify_rx.attr_handle, event->notify_rx.om);
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    bt.synced = true;
    bt.phone = bt.want_on ? AVO_PHONE_WAITING : AVO_PHONE_OFF;
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset (%d)", reason);
    bt.synced = false;
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool stack_init(void)
{
    if (nimble_port_init() != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed");
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO; /* "Just Works": the iPhone asks to confirm */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(BT_NAME);
    ble_svc_gap_device_appearance_set(APPEARANCE_WATCH);
    ble_store_config_init();
    board_apple_init();
    const esp_timer_create_args_t targs = { .callback = secure_timer_cb, .name = "ble_secure" };
    esp_timer_create(&targs, &s_secure_timer);
    nimble_port_freertos_init(host_task);
    return true;
}

/* ================================================================= HAL */

void avo_hal_bt_enable(bool on)
{
    bt.want_on = on;
    if (on && !bt.stack_ready) {
        bt.stack_ready = stack_init();
        return; /* advertising starts in on_sync */
    }
    if (!bt.synced) {
        return;
    }
    if (on) {
        advertise();
        return;
    }
    ble_gap_adv_stop();
    if (bt.conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(bt.conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    bt.phone = AVO_PHONE_OFF;
}

avo_link_t avo_hal_bt_state(void)
{
    if (!bt.want_on) {
        return AVO_LINK_OFF;
    }
    switch (bt.phone) {
    case AVO_PHONE_READY: return AVO_LINK_CONNECTED;
    case AVO_PHONE_PAIRING:
    case AVO_PHONE_WAITING: return AVO_LINK_BUSY;
    default: return AVO_LINK_IDLE;
    }
}

const char *avo_hal_bt_name(void) { return BT_NAME; }

avo_phone_state_t avo_hal_phone_state(void)
{
    return bt.want_on ? bt.phone : AVO_PHONE_OFF;
}

bool avo_hal_phone_bonded(void)
{
    int count = 0;
    if (!bt.synced || ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count) != 0) {
        return false;
    }
    return count > 0;
}

void avo_hal_phone_forget(void)
{
    if (!bt.synced) {
        return;
    }
    if (bt.conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(bt.conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_store_clear();
    ESP_LOGI(TAG, "all bonds cleared");
}
