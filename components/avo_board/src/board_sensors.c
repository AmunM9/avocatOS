/* I2C bus + AXP2101 (battery, PWR key), PCF85063 (RTC), QMI8658 (accel). */
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_priv.h"

static const char *TAG = "board_sensors";

#define I2C_TIMEOUT_MS 50

i2c_master_bus_handle_t g_board_i2c;
static i2c_master_dev_handle_t s_axp, s_rtc, s_imu;
static bool s_imu_ok;

/* ================================================================= I2C */

esp_err_t board_i2c_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &g_board_i2c);
}

i2c_master_dev_handle_t board_i2c_add(uint8_t addr)
{
    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_HZ,
    };
    i2c_master_dev_handle_t h = NULL;
    if (i2c_master_bus_add_device(g_board_i2c, &dev, &h) != ESP_OK) {
        return NULL;
    }
    return h;
}

esp_err_t board_reg_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

esp_err_t board_reg_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    if (!dev) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t b[2] = { reg, val };
    return i2c_master_transmit(dev, b, sizeof b, I2C_TIMEOUT_MS);
}

static esp_err_t reg_set_bits(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t bits)
{
    uint8_t v = 0;
    esp_err_t err = board_reg_read(dev, reg, &v, 1);
    if (err == ESP_OK && (v & bits) != bits) {
        err = board_reg_write(dev, reg, v | bits);
    }
    return err;
}

/* ================================================================= AXP2101 */

#define AXP_STATUS1 0x00      /* b3 battery present, b5 VBUS good         */
#define AXP_STATUS2 0x01      /* b6:5 == 01 charging, b3 VBUS "insert"     */
#define AXP_GAUGE_CTRL 0x18   /* b3 fuel gauge enable                     */
#define AXP_ADC_CTRL 0x30     /* b0 battery voltage measurement            */
#define AXP_IRQ_EN1 0x41      /* b3 PWR short press, b2 PWR long press     */
#define AXP_IRQ_ST1 0x49
#define AXP_BAT_DET 0x68      /* b0 battery detection enable               */
#define AXP_VBAT_H 0x34
#define AXP_BAT_PERCENT 0xA4
#define AXP_KEY_SHORT (1 << 3)
#define AXP_KEY_LONG (1 << 2)

esp_err_t board_pmu_init(void)
{
    s_axp = board_i2c_add(I2C_ADDR_AXP2101);
    uint8_t st = 0;
    esp_err_t err = board_reg_read(s_axp, AXP_STATUS1, &st, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not responding: %s", esp_err_to_name(err));
        return err;
    }
    /* measurement + key flags only; voltages and rails are left untouched */
    reg_set_bits(s_axp, AXP_BAT_DET, 0x01);
    reg_set_bits(s_axp, AXP_ADC_CTRL, 0x01);
    reg_set_bits(s_axp, AXP_GAUGE_CTRL, 0x08);
    reg_set_bits(s_axp, AXP_IRQ_EN1, AXP_KEY_SHORT | AXP_KEY_LONG);
    board_reg_write(s_axp, AXP_IRQ_ST1, AXP_KEY_SHORT | AXP_KEY_LONG); /* clear stale flags */
    ESP_LOGI(TAG, "AXP2101 ready (status 0x%02x)", st);
    return ESP_OK;
}

void board_pmu_read(avo_battery_t *out)
{
    memset(out, 0, sizeof *out);
    out->percent = -1;
    uint8_t s[2] = { 0 };
    if (board_reg_read(s_axp, AXP_STATUS1, s, 2) != ESP_OK) {
        return;
    }
    out->present = s[0] & (1 << 3);
    out->usb = (s[0] & (1 << 5)) != 0;
    out->charging = ((s[1] >> 5) & 0x03) == 0x01;
    if (!out->present) {
        return;
    }
    uint8_t v[2] = { 0 };
    if (board_reg_read(s_axp, AXP_VBAT_H, v, 2) == ESP_OK) {
        out->millivolts = ((v[0] & 0x1F) << 8) | v[1];
    }
    uint8_t pct = 0xFF;
    if (board_reg_read(s_axp, AXP_BAT_PERCENT, &pct, 1) == ESP_OK && pct <= 100) {
        out->percent = pct;
    } else if (out->millivolts > 0) {
        out->percent = avo_batt_percent_from_mv(out->millivolts);
    }
}

void board_pmu_poll_key(bool *short_press, bool *long_press)
{
    *short_press = *long_press = false;
    uint8_t st = 0;
    if (board_reg_read(s_axp, AXP_IRQ_ST1, &st, 1) != ESP_OK) {
        return;
    }
    st &= (AXP_KEY_SHORT | AXP_KEY_LONG);
    if (st) {
        board_reg_write(s_axp, AXP_IRQ_ST1, st); /* write-1-to-clear */
        *short_press = st & AXP_KEY_SHORT;
        *long_press = st & AXP_KEY_LONG;
    }
}

/* ================================================================= PCF85063 */

#define RTC_SECONDS 0x04
#define RTC_OS_FLAG 0x80
#define RTC_MIN_VALID_YEAR 2025
#define BOOT_FALLBACK_UTC_OFFSET_S (5 * 3600) /* build clock is UTC-5 */

static uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static time_t build_time_utc(void)
{
    static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = { 0 };
    struct tm tm = { 0 };
    sscanf(__DATE__, "%3s %d %d", mon, &tm.tm_mday, &tm.tm_year);
    sscanf(__TIME__, "%d:%d:%d", &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_mon = (int)((strstr(MONTHS, mon) - MONTHS) / 3);
    tm.tm_year -= 1900;
    setenv("TZ", "UTC0", 1);
    tzset();
    return mktime(&tm) + BOOT_FALLBACK_UTC_OFFSET_S;
}

esp_err_t board_rtc_init(void)
{
    setenv("TZ", "UTC0", 1); /* system clock is UTC; avocatOS applies its own offset */
    tzset();
    s_rtc = board_i2c_add(I2C_ADDR_PCF85063);
    uint8_t r[7] = { 0 };
    time_t now = 0;
    if (board_reg_read(s_rtc, RTC_SECONDS, r, sizeof r) == ESP_OK && !(r[0] & RTC_OS_FLAG)) {
        struct tm tm = {
            .tm_sec = bcd2bin(r[0] & 0x7F),
            .tm_min = bcd2bin(r[1] & 0x7F),
            .tm_hour = bcd2bin(r[2] & 0x3F),
            .tm_mday = bcd2bin(r[3] & 0x3F),
            .tm_mon = bcd2bin(r[5] & 0x1F) - 1,
            .tm_year = bcd2bin(r[6]) + 100,
        };
        if (tm.tm_year + 1900 >= RTC_MIN_VALID_YEAR) {
            now = mktime(&tm);
        }
    }
    if (now == 0) {
        now = build_time_utc();
        ESP_LOGW(TAG, "RTC time invalid, using build time");
    }
    struct timeval tv = { .tv_sec = now };
    settimeofday(&tv, NULL);
    return ESP_OK;
}

void board_rtc_store(time_t utc)
{
    struct tm tm;
    gmtime_r(&utc, &tm);
    const uint8_t buf[8] = {
        RTC_SECONDS, bin2bcd(tm.tm_sec), bin2bcd(tm.tm_min), bin2bcd(tm.tm_hour),
        bin2bcd(tm.tm_mday), (uint8_t)tm.tm_wday, bin2bcd(tm.tm_mon + 1), bin2bcd(tm.tm_year - 100),
    };
    if (s_rtc) {
        i2c_master_transmit(s_rtc, buf, sizeof buf, I2C_TIMEOUT_MS);
    }
}

/* ================================================================= QMI8658 */

#define QMI_WHO_AM_I 0x00
#define QMI_ID 0x05
#define QMI_CTRL1 0x02
#define QMI_CTRL2 0x03
#define QMI_CTRL3 0x04
#define QMI_CTRL5 0x06
#define QMI_CTRL7 0x08
#define QMI_CTRL8 0x09
#define QMI_CTRL9 0x0A
#define QMI_CAL1_L 0x0B                   /* CAL1_L..CAL3_H: 0x0B..0x10     */
#define QMI_CAL4_H 0x12
#define QMI_STATUS_INT 0x2D
#define QMI_STATUS1 0x2F
#define QMI_AX_L 0x35                     /* ax ay az gx gy gz, 12 bytes    */
#define QMI_TAP_STATUS 0x59
#define QMI_ACC_SCALE (4.0f / 32768.0f)   /* +-4 g                          */
#define QMI_GYR_SCALE (512.0f / 32768.0f) /* +-512 dps                      */
/* The tap engine needs >= 500 Hz and an unfiltered accelerometer (SensorLib
 * qmi8658_tap_detection notes); our 100 Hz reads keep their own low-pass. */
#define QMI_ACC_CFG 0x14                  /* +-4 g, 500 Hz                  */
#define QMI_GYR_CFG 0x54                  /* +-512 dps, 500 Hz              */
#define QMI_LPF_CFG 0x70                  /* gyro LPF 13 % of ODR, accel off */
#define QMI_EN_ACC 0x01
#define QMI_EN_GYR 0x02
#define QMI_CTRL8_HANDSHAKE 0x80          /* CTRL9 done flag in STATUS_INT  */
#define QMI_CTRL8_TAP 0x01
#define QMI_CMD_ACK 0x00
#define QMI_CMD_CONFIGURE_TAP 0x0C
#define QMI_CMD_DONE 0x80
#define QMI_STATUS1_TAP 0x02
#define QMI_TAP_TYPE_MASK 0x03
#define QMI_CMD_TIMEOUT_MS 50

/* Tap engine parameters in samples at 500 Hz and g^2 (the values of the
 * SensorLib example, with a longer double-tap window for a relaxed pace). */
#define TAP_PEAK_WINDOW 20
#define TAP_PRIORITY_Z_Y_X 0x05           /* the case is knocked along z    */
#define TAP_WINDOW 55                     /* 110 ms                         */
#define TAP_DOUBLE_WINDOW 180             /* 360 ms between the two taps    */
#define TAP_ALPHA 8                       /* 0.0625 * 128                   */
#define TAP_GAMMA 32                      /* 0.25 * 128                     */
#define TAP_PEAK_MG2 300                  /* 0.30 g^2                       */
#define TAP_QUIET_MG2 180                 /* 0.18 g^2                       */

static bool s_gyro_on, s_tap_hw;

static bool wait_cmd_flag(bool set)
{
    for (int t = 0; t < QMI_CMD_TIMEOUT_MS; t++) {
        uint8_t st = 0;
        if (board_reg_read(s_imu, QMI_STATUS_INT, &st, 1) == ESP_OK && st != 0xFF &&
            ((st & QMI_CMD_DONE) != 0) == set) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}

/* CTRL9 command with the STATUS_INT handshake: done -> ack -> cleared. */
static bool imu_command(uint8_t cmd)
{
    if (board_reg_write(s_imu, QMI_CTRL9, cmd) != ESP_OK || !wait_cmd_flag(true)) {
        return false;
    }
    board_reg_write(s_imu, QMI_CTRL9, QMI_CMD_ACK);
    return wait_cmd_flag(false);
}

/* CAL1_L..CAL3_H, then CAL4_H (which selects the parameter page); CAL4_L
 * is left alone, as in SensorLib. */
static void write_cal(const uint8_t v[7])
{
    for (int i = 0; i < 6; i++) {
        board_reg_write(s_imu, QMI_CAL1_L + i, v[i]);
    }
    board_reg_write(s_imu, QMI_CAL4_H, v[6]);
}

/* Configure the hardware tap detector (sensors must be disabled). */
static bool tap_configure(void)
{
    const uint8_t first[7] = {
        TAP_PEAK_WINDOW, TAP_PRIORITY_Z_Y_X,
        TAP_WINDOW & 0xFF, TAP_WINDOW >> 8,
        TAP_DOUBLE_WINDOW & 0xFF, TAP_DOUBLE_WINDOW >> 8,
        0x01,
    };
    const uint8_t second[7] = {
        TAP_ALPHA, TAP_GAMMA,
        TAP_PEAK_MG2 & 0xFF, TAP_PEAK_MG2 >> 8,
        TAP_QUIET_MG2 & 0xFF, TAP_QUIET_MG2 >> 8,
        0x02,
    };
    write_cal(first);
    if (!imu_command(QMI_CMD_CONFIGURE_TAP)) {
        return false;
    }
    write_cal(second);
    return imu_command(QMI_CMD_CONFIGURE_TAP);
}

esp_err_t board_imu_init(void)
{
    s_imu = board_i2c_add(I2C_ADDR_QMI8658);
    uint8_t id = 0;
    if (board_reg_read(s_imu, QMI_WHO_AM_I, &id, 1) != ESP_OK || id != QMI_ID) {
        ESP_LOGW(TAG, "QMI8658 not found (id 0x%02x)", id);
        return ESP_ERR_NOT_FOUND;
    }
    board_reg_write(s_imu, QMI_CTRL1, 0x40);   /* address auto-increment */
    board_reg_write(s_imu, QMI_CTRL7, 0x00);   /* sensors off while configuring */
    board_reg_write(s_imu, QMI_CTRL8, QMI_CTRL8_HANDSHAKE);
    board_reg_write(s_imu, QMI_CTRL2, QMI_ACC_CFG);
    board_reg_write(s_imu, QMI_CTRL3, QMI_GYR_CFG);
    board_reg_write(s_imu, QMI_CTRL5, QMI_LPF_CFG);
    s_tap_hw = tap_configure();
    board_reg_write(s_imu, QMI_CTRL7, QMI_EN_ACC); /* gyro only while wrist flick is on */
    if (s_tap_hw) {
        board_reg_write(s_imu, QMI_CTRL8, QMI_CTRL8_HANDSHAKE | QMI_CTRL8_TAP);
    }
    ESP_LOGI(TAG, "QMI8658 ready, hardware tap detector %s", s_tap_hw ? "on" : "unavailable");
    s_imu_ok = true;
    return ESP_OK;
}

bool board_imu_has_tap(void) { return s_tap_hw; }

int board_imu_poll_tap(void)
{
    uint8_t st = 0, tap = 0;
    if (!s_tap_hw || board_reg_read(s_imu, QMI_STATUS1, &st, 1) != ESP_OK || !(st & QMI_STATUS1_TAP)) {
        return 0;
    }
    if (board_reg_read(s_imu, QMI_TAP_STATUS, &tap, 1) != ESP_OK) {
        return 0;
    }
    return tap & QMI_TAP_TYPE_MASK; /* 1 single, 2 double */
}

void board_imu_gyro(bool on)
{
    if (s_imu_ok && on != s_gyro_on) {
        board_reg_write(s_imu, QMI_CTRL7, on ? (QMI_EN_ACC | QMI_EN_GYR) : QMI_EN_ACC);
        s_gyro_on = on;
    }
}

bool board_imu_read6(float a[3], float g[3])
{
    if (!s_imu_ok) {
        return false;
    }
    uint8_t b[12];
    if (board_reg_read(s_imu, QMI_AX_L, b, sizeof b) != ESP_OK) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        a[i] = (int16_t)(b[2 * i + 1] << 8 | b[2 * i]) * QMI_ACC_SCALE;
        g[i] = s_gyro_on ? (int16_t)(b[2 * i + 7] << 8 | b[2 * i + 6]) * QMI_GYR_SCALE : 0.0f;
    }
    return true;
}
