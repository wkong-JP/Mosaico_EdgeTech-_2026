// SPDX-License-Identifier: Apache-2.0
#include "dev_magnet.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static const char *TAG = "MAG_DOCK";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static dev_mag_engine_t s_engine;
static dev_mag_dock_status_t s_status;
static bool s_presented_logged = false;
static uint32_t s_attach_start_ms = 0;
static uint32_t s_attach_confirm_ms = 0;
static dev_mag_calib_state_t s_last_calib_state = DEV_MAG_CALIB_CALIBRATING;

static const char *state_to_str(dev_mag_dock_state_t st)
{
    switch (st) {
    case DEV_MAG_STATE_NORMAL: return "NORMAL";
    case DEV_MAG_STATE_ATTACH_PENDING: return "ATTACH_PENDING";
    case DEV_MAG_STATE_LOGO_ACTIVE: return "LOGO_ACTIVE";
    case DEV_MAG_STATE_DETACH_PENDING: return "DETACH_PENDING";
    default: return "UNKNOWN";
    }
}

void dev_magnet_init(void)
{
    portENTER_CRITICAL(&s_lock);
    dev_mag_calib_config_t calib = DEV_MAG_CALIB_DEFAULT;
    dev_mag_dock_config_t dock_cfg = DEV_MAG_DOCK_CONFIG_DEFAULT();
    dev_mag_engine_init(&s_engine, &calib, &dock_cfg);
    memset(&s_status, 0, sizeof(s_status));
    s_presented_logged = false;
    s_attach_start_ms = 0;
    s_attach_confirm_ms = 0;
    s_last_calib_state = DEV_MAG_CALIB_CALIBRATING;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "init OK: filter=%u, attach_frames=%u, detach_frames=%u",
             calib.filter_samples, dock_cfg.attach_confirm_frames, dock_cfg.detach_confirm_frames);
}

void dev_magnet_process_sample(const dev_mag_sample_t *sample)
{
    if (!sample) return;

    portENTER_CRITICAL(&s_lock);
    const dev_mag_dock_state_t prev_state = s_engine.sm.state;
    const bool changed = dev_mag_engine_feed(&s_engine, sample);
    const dev_mag_dock_state_t new_state = s_engine.sm.state;
    const dev_mag_calib_state_t calib_st = s_engine.classifier.calib_state;

    // Update status snapshot
    s_status.state = new_state;
    s_status.calib_state = calib_st;
    s_status.edge_mask = s_engine.sm.last_edge_mask;
    s_status.logo_active = (new_state == DEV_MAG_STATE_LOGO_ACTIVE);
    s_status.baseline_ready = s_engine.classifier.baseline_ready;
    s_status.baseline_right_y = s_engine.classifier.baseline_right_y;
    s_status.last_raw_right[0] = sample->right.x;
    s_status.last_raw_right[1] = sample->right.y;
    s_status.last_raw_right[2] = sample->right.z;
    s_status.last_raw_left[0] = sample->left.x;
    s_status.last_raw_left[1] = sample->left.y;
    s_status.last_raw_left[2] = sample->left.z;
    s_status.last_sample_ms = sample->timestamp_ms;
    s_status.last_transition_ms = s_engine.sm.last_transition_ms;
    s_status.total_contacts = s_engine.sm.total_contacts;
    s_status.total_releases = s_engine.sm.total_releases;
    s_status.sample_errors = s_engine.sm.sample_errors;

    if (new_state == DEV_MAG_STATE_ATTACH_PENDING && prev_state == DEV_MAG_STATE_NORMAL) {
        s_attach_start_ms = sample->timestamp_ms;
    }
    if (new_state == DEV_MAG_STATE_LOGO_ACTIVE && prev_state != DEV_MAG_STATE_LOGO_ACTIVE) {
        s_attach_confirm_ms = sample->timestamp_ms;
        s_status.detect_latency_ms = (s_attach_start_ms > 0 && sample->timestamp_ms >= s_attach_start_ms) ?
                                     (sample->timestamp_ms - s_attach_start_ms) : 0;
        s_presented_logged = false;
    }
    if (new_state == DEV_MAG_STATE_NORMAL && prev_state != DEV_MAG_STATE_NORMAL) {
        s_presented_logged = false;
        s_attach_start_ms = 0;
        s_attach_confirm_ms = 0;
    }

    portEXIT_CRITICAL(&s_lock);

    // Calibration state logging
    if (calib_st != s_last_calib_state) {
        s_last_calib_state = calib_st;
        if (calib_st == DEV_MAG_CALIB_READY) {
            ESP_LOGI(TAG, "calibration READY: baseline_right_y=%ld, baseline_right_z=%ld",
                     (long)s_status.baseline_right_y, (long)s_engine.classifier.baseline_right_z);
        } else if (calib_st == DEV_MAG_CALIB_FAILED) {
            ESP_LOGW(TAG, "calibration FAILED (device may be docked at boot): rejections=%u",
                     s_engine.classifier.calib_rejections);
        }
    }

    // State transition logging
    if (changed) {
        ESP_LOGI(TAG, "%s -> %s (edges=0x%02x, raw_R=[%d,%d,%d] raw_L=[%d,%d,%d])",
                 state_to_str(prev_state), state_to_str(new_state),
                 s_status.edge_mask,
                 sample->right.x, sample->right.y, sample->right.z,
                 sample->left.x, sample->left.y, sample->left.z);

        if (new_state == DEV_MAG_STATE_LOGO_ACTIVE) {
            ESP_LOGI(TAG, "RIGHT contact confirmed: detect latency=%lu ms", (unsigned long)s_status.detect_latency_ms);
            ESP_LOGI(TAG, "NORMAL -> LOGO (requesting Espressif logo display)");
        } else if (new_state == DEV_MAG_STATE_NORMAL && prev_state == DEV_MAG_STATE_DETACH_PENDING) {
            ESP_LOGI(TAG, "RIGHT released: LOGO -> NORMAL (restoring original UI)");
        }
    }
}

void dev_magnet_take_status(dev_mag_dock_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
}

bool dev_magnet_is_logo_active(void)
{
    portENTER_CRITICAL(&s_lock);
    const bool act = s_status.logo_active;
    portEXIT_CRITICAL(&s_lock);
    return act;
}

void dev_magnet_notify_presented(void)
{
    bool log_this = false;
    uint32_t present_lat = 0;
    uint32_t total_lat = 0;

    portENTER_CRITICAL(&s_lock);
    if (s_status.logo_active && !s_presented_logged) {
        const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        present_lat = (s_attach_confirm_ms > 0 && now_ms >= s_attach_confirm_ms) ?
                      (now_ms - s_attach_confirm_ms) : 0;
        total_lat = (s_attach_start_ms > 0 && now_ms >= s_attach_start_ms) ?
                    (now_ms - s_attach_start_ms) : present_lat;
        s_status.present_latency_ms = present_lat;
        s_presented_logged = true;
        log_this = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (log_this) {
        ESP_LOGI(TAG, "Espressif logo presented: present latency=%lu ms (total from contact=%lu ms)",
                 (unsigned long)present_lat, (unsigned long)total_lat);
        ESP_LOGI(TAG, "Diagnostics: heap_internal=%u, psram=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}
