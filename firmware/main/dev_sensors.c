// SPDX-License-Identifier: Apache-2.0
#include "dev_sensors.h"

#include <string.h>

#include "bsp/esp_mosaico.h"
#include "dev_audio.h"
#if EXP_VOICE
#include "dev_voice.h"
#endif
#include "dev_joystick.h"
#include "dev_magnet.h"
#include "dev_sensors_core.h"
#include "dev_slots.h"
#include "dev_watchdog.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dev_sensors";

#define IMU_PERIOD_MS 20          /* 50 Hz poll; the sensor runs at 100 Hz */
#define IMU_FRESH_MS 300
#define BATT_PERIOD_MS 2000
#define BATT_FRESH_MS 6000        /* three missed polls */
#define BATT_START_DELAY_MS 1500  /* let the IMU settle first; the shared I2C bus already exists */
#define BATT_MV_MIN 2500          /* a single Li-ion cell outside this range is not a believable reading */
#define BATT_MV_MAX 4600
#define GAUGE_I2C_ADDR 0x55       /* BSP_BATTERY_I2C_ADDR */
#define GAUGE_I2C_HZ 400000
#define GAUGE_I2C_TIMEOUT_MS 100  /* the read-only path never waits forever */
#define GAUGE_CMD_VOLTAGE 0x08
#define GAUGE_CMD_CURRENT 0x0C
#define GAUGE_CMD_SOC 0x2C

static const int8_t s_map[3][3] = EXP_IMU_MAP;
#if EXP_USE_MAG
#define MAG_PERIOD_MS 100         /* BMM150 regular preset converts at about 10 Hz */
#define MAG_FRESH_MS 1000
static const int8_t s_mag_map[DEV_MAG_N][3][3] = {EXP_MAG_MAP_0, EXP_MAG_MAP_1};
static int64_t s_last_mag_us[DEV_MAG_N] = {-1, -1};
#endif

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dev_sensors_t s_d = {.imu_state = "starting",
                            .imu_age_ms = UINT32_MAX,
#if EXP_USE_MAG
                            .mag_state = {"starting", "starting"},
#else
                            .mag_state = {"off", "off"},
#endif
#if EXP_USE_BATTERY
                            .batt_state = "starting"
#else
                            .batt_state = "off"
#endif
};
static dev_mean3_t s_gyr_mean;
static int64_t s_last_ok_us = -1;
static int64_t s_last_batt_us = -1;
static dev_angle3_t s_angle;
static int64_t s_angle_last_us = -1;  /* time of the previous good sample, for the integral */
static bool s_angle_reset;

static uint32_t age_ms(void)
{
    return s_last_ok_us < 0 ? UINT32_MAX : (uint32_t)((esp_timer_get_time() - s_last_ok_us) / 1000);
}

#if EXP_USE_BATTERY
static void batt_set_state(const char *state)
{
    portENTER_CRITICAL(&s_lock);
    s_d.batt_state = state;
    portEXIT_CRITICAL(&s_lock);
}

/* Records one poll. A reading counts only if the gauge answered and the numbers are believable. */
static void battery_apply(esp_err_t e, int mv, int ma, int raw_soc)
{
    const int soc = dev_battery_resolve_soc(mv, raw_soc);
    portENTER_CRITICAL(&s_lock);
    if (e != ESP_OK) {
        ++s_d.batt_errors;
        s_d.batt_state = "read failed";
    } else if (mv < BATT_MV_MIN || mv > BATT_MV_MAX) {
        ++s_d.batt_errors;
        s_d.batt_state = "implausible";
        s_d.batt_mv = mv;                  /* shown for diagnosis; batt_ok stays false because the state is not "ok" */
        s_d.batt_ma = ma;
        s_d.batt_pct = (uint8_t)(soc < 0 ? 0 : (soc > 255 ? 255 : soc));
    } else {
        ++s_d.batt_reads;
        s_d.batt_state = "ok";
        s_d.batt_pct = (uint8_t)soc;
        s_d.batt_mv = mv;
        s_d.batt_ma = ma;
        s_d.charging = ma > 5;
        s_last_batt_us = esp_timer_get_time();
    }
    const bool log_now = s_d.batt_reads == 1 || s_d.batt_errors == 1;
    const char *state = s_d.batt_state;
    portEXIT_CRITICAL(&s_lock);
    if (log_now) ESP_LOGI(TAG, "battery: %s, %d mV, %d mA, %d %% (raw %d %%)", state, mv, ma, soc, raw_soc);
}

#if EXP_BATT_WRITE_PROFILE
static void battery_poll(void)
{
    bsp_battery_status_t st;
    const esp_err_t e = bsp_battery_read(&st);
    battery_apply(e, st.voltage_mv, st.current_ma, st.state_of_charge);
}

static esp_err_t battery_open(void)
{
    return bsp_battery_init();         /* verifies, and if different REWRITES, the gauge profile */
}
#else
static i2c_master_dev_handle_t s_gauge;

static esp_err_t gauge_read16(uint8_t cmd, uint16_t *out)
{
    uint8_t b[2];
    const esp_err_t e = i2c_master_transmit_receive(s_gauge, &cmd, 1, b, 2, GAUGE_I2C_TIMEOUT_MS);
    if (e == ESP_OK) *out = (uint16_t)(b[0] | (b[1] << 8));    /* the gauge sends the low byte first */
    return e;
}

static void battery_poll(void)
{
    uint16_t v = 0, i = 0, c = 0;
    esp_err_t e = gauge_read16(GAUGE_CMD_VOLTAGE, &v);
    if (e == ESP_OK) e = gauge_read16(GAUGE_CMD_CURRENT, &i);
    if (e == ESP_OK) e = gauge_read16(GAUGE_CMD_SOC, &c);
    battery_apply(e, (int)v, (int)(int16_t)i, (int)c);
}

/* Read-only: three standard commands, no sub-command, no data-memory access, nothing is written to the gauge. */
static esp_err_t battery_open(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) return ESP_ERR_INVALID_STATE;
    esp_err_t e = i2c_master_probe(bus, GAUGE_I2C_ADDR, GAUGE_I2C_TIMEOUT_MS);
    if (e != ESP_OK) return e;
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GAUGE_I2C_ADDR,
        .scl_speed_hz = GAUGE_I2C_HZ,
    };
    e = i2c_master_bus_add_device(bus, &cfg, &s_gauge);
    if (e == ESP_OK) ESP_LOGI(TAG, "BQ27220 opened read-only (no configuration is written)");
    return e;
}
#endif

/* Own task: with EXP_BATT_WRITE_PROFILE the init may rewrite the gauge profile, which takes several hundred
 * milliseconds, and the IMU must not wait for that. */
static void battery_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(BATT_START_DELAY_MS));
    const esp_err_t e = battery_open();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "BQ27220 init failed: %s", esp_err_to_name(e));
        batt_set_state("init failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "BQ27220 gauge ready, polled every %d ms", BATT_PERIOD_MS);
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        battery_poll();
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(BATT_PERIOD_MS));
    }
}
#endif

#if EXP_USE_MAG
static bool s_mag_open[DEV_MAG_N];

static void mag_set_state(int id, const char *state)
{
    portENTER_CRITICAL(&s_lock);
    s_d.mag_state[id] = state;
    portEXIT_CRITICAL(&s_lock);
}

/* Called from the sensor task only, after the IMU is running: the BSP's shared I2C bus is created lazily and that is
 * not thread-safe, so every BSP peripheral init stays in one thread (see the touch race in 0.4.0). */
static void mag_open_all(void)
{
    for (int id = 0; id < DEV_MAG_N; ++id) {
        if (!dev_map_is_rotation(s_mag_map[id])) {
            ESP_LOGE(TAG, "EXP_MAG_MAP_%d is not a proper rotation; magnetometer %d is not used", id, id);
            mag_set_state(id, "bad axis map");
            continue;
        }
        const esp_err_t e = bsp_magnetometer_init((bsp_magnetometer_t)id);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "BMM150[%d] init failed: %s", id, esp_err_to_name(e));
            mag_set_state(id, "init failed");
        } else {
            s_mag_open[id] = true;
            ESP_LOGI(TAG, "BMM150[%d] running, polled every %d ms, %d LSB per uT", id, MAG_PERIOD_MS,
                     (int)EXP_MAG_LSB_PER_UT);
        }
    }
    dev_magnet_init();
}

static void mag_poll(void)
{
    struct bmm150_mag_data raw_data[DEV_MAG_N] = {0};
    bool raw_valid[DEV_MAG_N] = {false, false};

    for (int id = 0; id < DEV_MAG_N; ++id) {
        if (!s_mag_open[id]) continue;
        struct bmm150_mag_data d;
        const esp_err_t e = bsp_magnetometer_read((bsp_magnetometer_t)id, &d);
        if (e == ESP_OK) {
            raw_data[id] = d;
            raw_valid[id] = true;
        }
        portENTER_CRITICAL(&s_lock);
        if (e != ESP_OK) {
            ++s_d.mag_errors[id];
            s_d.mag_state[id] = "read failed";
        } else if (!dev_mag_valid(d.x, d.y, d.z)) {
            ++s_d.mag_errors[id];
            s_d.mag_state[id] = "overflow";
        } else {
            const float c[3] = {dev_mag_to_ut(d.x, EXP_MAG_LSB_PER_UT), dev_mag_to_ut(d.y, EXP_MAG_LSB_PER_UT),
                                dev_mag_to_ut(d.z, EXP_MAG_LSB_PER_UT)};
            float u[3];
            dev_map3(s_mag_map[id], c, u);
            s_d.mag_raw[id][0] = d.x; s_d.mag_raw[id][1] = d.y; s_d.mag_raw[id][2] = d.z;
            memcpy(s_d.mag_ut[id], u, sizeof(u));
            ++s_d.mag_reads[id];
            s_d.mag_state[id] = "ok";
            s_last_mag_us[id] = esp_timer_get_time();
        }
        portEXIT_CRITICAL(&s_lock);
    }

    const dev_mag_sample_t dock_sample = {
        .right = {
            .valid = raw_valid[0],
            .x = raw_data[0].x,
            .y = raw_data[0].y,
            .z = raw_data[0].z,
        },
        .left = {
            .valid = raw_valid[1],
            .x = raw_data[1].x,
            .y = raw_data[1].y,
            .z = raw_data[1].z,
        },
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    dev_magnet_process_sample(&dock_sample);
}
#endif

/* ---- Audio / motor / expansion slots (stage 3c) ---------------------------------------------------------------------
 * These drivers have never run on this board. They start in their own task AFTER the IMU, the magnetometers and the BSP
 * I2C bus exist, so a hang in one of them cannot stop the IMU, and each step is fenced by the start-up crash guard
 * (dev_io_core.h): if the previous boot ended abnormally inside a step, that step is skipped until the next power-on. */
#if EXP_USE_AUDIO || EXP_USE_MOTOR || EXP_USE_SLOTS
#define SLOT_PERIOD_MS 500
#define JOY_PERIOD_MS 20
static __NOINIT_ATTR dev_guard_t s_guard;
#if EXP_VOICE
static void voice_guard(bool begin)
{
    if (begin) dev_guard_begin(&s_guard, DEV_STEP_VOICE);
    else dev_guard_end(&s_guard, DEV_STEP_VOICE);
}
#endif

static void io_task(void *arg)
{
    (void)arg;
    dev_guard_boot(&s_guard, esp_reset_reason() == ESP_RST_POWERON);
#if EXP_USE_AUDIO
    if (dev_guard_allowed(&s_guard, DEV_STEP_AUDIO)) {
        dev_guard_begin(&s_guard, DEV_STEP_AUDIO);
        dev_audio_open_codecs();
        dev_guard_end(&s_guard, DEV_STEP_AUDIO);
    } else {
        dev_audio_mark_skipped(true, false);
    }
#endif
#if EXP_USE_MOTOR
    if (dev_guard_allowed(&s_guard, DEV_STEP_MOTOR)) {
        dev_guard_begin(&s_guard, DEV_STEP_MOTOR);
        dev_motor_open();
        dev_guard_end(&s_guard, DEV_STEP_MOTOR);
    } else {
        dev_audio_mark_skipped(false, true);
    }
#endif
#if EXP_VOICE && EXP_USE_AUDIO && EXP_USE_MIC
    if (dev_guard_allowed(&s_guard, DEV_STEP_VOICE)) {
        static const dev_voice_hooks_t hooks = {.guard = voice_guard, .mic_ready = dev_audio_mic_ready};
        dev_voice_open(&hooks);              /* returns at once: the models load in their own task */
    } else {
        dev_voice_mark_skipped();
    }
#endif
#if EXP_USE_SLOTS
    if (dev_guard_allowed(&s_guard, DEV_STEP_SLOTS)) {
        dev_guard_begin(&s_guard, DEV_STEP_SLOTS);
        dev_slots_open();
        dev_guard_end(&s_guard, DEV_STEP_SLOTS);
    } else {
        dev_slots_mark_skipped();
    }
    for (int tick = 0;; ++tick) {
        if (tick % (SLOT_PERIOD_MS / JOY_PERIOD_MS) == 0) dev_slots_poll();
#if EXP_USE_JOYSTICK
        dev_slots_t sl;
        dev_slots_take(&sl);
        dev_joystick_poll(&sl, &s_guard);      /* claims the module only for a valid Handle/Joystick descriptor */
#endif
        vTaskDelay(pdMS_TO_TICKS(JOY_PERIOD_MS));
    }
#endif
    vTaskDelete(NULL);
}
#endif

static void sensor_task(void *arg)
{
    (void)arg;
    bsp_board_variant_t variant;
    if (bsp_board_variant_get(&variant) == ESP_OK)
        ESP_LOGI(TAG, "board variant from eFuse: %s", variant == BSP_BOARD_VARIANT_V1_2 ? "V1.2" : "V1.0");
    else
        ESP_LOGW(TAG, "board variant could not be read");

    if (!dev_map_is_rotation(s_map)) {
        ESP_LOGE(TAG, "EXP_IMU_MAP is not a proper rotation; IMU data will not be used");
        s_d.imu_state = "bad axis map";
        vTaskDelete(NULL);
        return;
    }
    esp_err_t e = bsp_imu_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "BMI270 init failed: %s", esp_err_to_name(e));
        s_d.imu_state = "init failed";
        vTaskDelete(NULL);
        return;
    }
    const bsp_imu_config_t cfg = BSP_IMU_CONFIG_DEFAULT();
    e = bsp_imu_start(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "BMI270 start failed: %s", esp_err_to_name(e));
        s_d.imu_state = "start failed";
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "BMI270 running: +-4 g, +-2000 dps, 100 Hz, polled at %d ms", IMU_PERIOD_MS);

#if EXP_USE_MAG
    mag_open_all();
#endif
#if EXP_USE_AUDIO || EXP_USE_MOTOR || EXP_USE_SLOTS
    if (xTaskCreatePinnedToCore(io_task, "io", 8192, NULL, 3, NULL, 1) != pdPASS)
        ESP_LOGE(TAG, "io task could not be created; no audio, motor or slot data will be reported");
#endif
    dev_wdt_subscribe();                  /* from here on a stuck IMU/magnetometer read restarts the unit */
    TickType_t wake = xTaskGetTickCount();
    uint32_t errs = 0;
    uint32_t loops = 0;
    for (;;) {
        dev_wdt_feed();
#if EXP_USE_MAG
        if (++loops % (MAG_PERIOD_MS / IMU_PERIOD_MS) == 0) mag_poll();
#else
        (void)loops;
#endif
        float a[3], g[3];
        if (bsp_imu_get_accel(&a[0], &a[1], &a[2]) == ESP_OK && bsp_imu_get_gyro(&g[0], &g[1], &g[2]) == ESP_OK) {
            float ua[3], ug[3];
            dev_map3(s_map, a, ua);
            dev_map3(s_map, g, ug);
            portENTER_CRITICAL(&s_lock);
            memcpy(s_d.acc_raw, a, sizeof(a));
            memcpy(s_d.gyr_raw, g, sizeof(g));
            memcpy(s_d.acc, ua, sizeof(ua));
            const int64_t t_us = esp_timer_get_time();
            if (s_angle_reset) { dev_angle3_reset(&s_angle); s_angle_reset = false; }
            if (s_angle_last_us >= 0) dev_angle3_add(&s_angle, g, (float)(t_us - s_angle_last_us) / 1e6f);
            s_angle_last_us = t_us;
            memcpy(s_d.gyr_angle, s_angle.deg, sizeof(s_d.gyr_angle));
            dev_mean3_add(&s_gyr_mean, ug);
            ++s_d.imu_samples;
            s_d.imu_state = "ok";
            s_last_ok_us = esp_timer_get_time();
            portEXIT_CRITICAL(&s_lock);
        } else {
            portENTER_CRITICAL(&s_lock);
            ++s_d.imu_errors;
            portEXIT_CRITICAL(&s_lock);
            if (errs++ % 100 == 0) ESP_LOGW(TAG, "BMI270 read failed (%u so far)", (unsigned)errs);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(IMU_PERIOD_MS));
    }
}

void dev_sensors_start(void)
{
    if (xTaskCreatePinnedToCore(sensor_task, "sensors", 6144, NULL, 4, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "sensor task could not be created; no sensor data will be reported");
        s_d.imu_state = "no task";
    }
#if EXP_USE_BATTERY
    if (xTaskCreatePinnedToCore(battery_task, "battery", 6144, NULL, 3, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "battery task could not be created; no battery data will be reported");
        s_d.batt_state = "no task";
    }
#endif
}

void dev_sensors_reset_angles(void)
{
    portENTER_CRITICAL(&s_lock);
    s_angle_reset = true;
    portEXIT_CRITICAL(&s_lock);
}

void dev_sensors_take(dev_sensors_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_d;
    float g[3];
    dev_mean3_take(&s_gyr_mean, g);
    memcpy(out->gyr, g, sizeof(g));
    const int64_t bl = s_last_batt_us;
#if EXP_USE_MAG
    const int64_t ml[DEV_MAG_N] = {s_last_mag_us[0], s_last_mag_us[1]};
#endif
    portEXIT_CRITICAL(&s_lock);
#if EXP_USE_MAG
    for (int id = 0; id < DEV_MAG_N; ++id)
        out->mag_ok[id] = ml[id] >= 0 && (esp_timer_get_time() - ml[id]) / 1000 < MAG_FRESH_MS &&
                          out->mag_state[id] && strcmp(out->mag_state[id], "ok") == 0;
#endif
    out->imu_age_ms = age_ms();
    out->imu_ok = out->imu_age_ms < IMU_FRESH_MS;
    out->batt_ok = bl >= 0 && (esp_timer_get_time() - bl) / 1000 < BATT_FRESH_MS && out->batt_state &&
                   strcmp(out->batt_state, "ok") == 0;
}
