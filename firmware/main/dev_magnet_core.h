// SPDX-License-Identifier: Apache-2.0
// Pure C magnetic classification and dock state machine for ESP-MOSAICO.
// Host-testable without ESP-IDF dependencies.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DEV_MAG_MAX_FILTER_SAMPLES 8U

typedef uint8_t dev_mag_edge_mask_t;
#define DEV_MAG_EDGE_TOP    (1U << 0)
#define DEV_MAG_EDGE_RIGHT  (1U << 1)
#define DEV_MAG_EDGE_BOTTOM (1U << 2)
#define DEV_MAG_EDGE_LEFT   (1U << 3)
#define DEV_MAG_EDGE_ALL    (DEV_MAG_EDGE_TOP | DEV_MAG_EDGE_RIGHT | DEV_MAG_EDGE_BOTTOM | DEV_MAG_EDGE_LEFT)

typedef enum {
    DEV_MAG_CALIB_CALIBRATING = 0,
    DEV_MAG_CALIB_READY,
    DEV_MAG_CALIB_FAILED,
} dev_mag_calib_state_t;

typedef enum {
    DEV_MAG_STATE_NORMAL = 0,
    DEV_MAG_STATE_ATTACH_PENDING,
    DEV_MAG_STATE_LOGO_ACTIVE,
    DEV_MAG_STATE_DETACH_PENDING,
} dev_mag_dock_state_t;

typedef struct {
    bool valid;
    int16_t x, y, z;
} dev_mag_channel_sample_t;

typedef struct {
    dev_mag_channel_sample_t right; /* BMM150 at 0x11 */
    dev_mag_channel_sample_t left;  /* BMM150 at 0x12 */
    uint32_t timestamp_ms;
} dev_mag_sample_t;

typedef struct {
    const char *name;
    uint8_t filter_samples;
    int16_t saturation_limit;
    int16_t left_reject_z;
    int16_t left_y_high;
    int16_t down_y_both_sides;
    int16_t down_y_with_side;
    int16_t down_y_without_side;
    int16_t down_right_y_without_side;
    int16_t down_right_y_delta;
    int16_t up_right_y;
    int16_t up_right_y_delta;
    int16_t up_right_y_high;
    int16_t up_right_y_with_down;
    int16_t up_right_y_both_with_down;
    int16_t up_right_z_delta_with_down;
    int16_t up_right_y_fallback;
    int16_t up_left_z_with_down;
    int16_t baseline_right_y_min;
    int16_t baseline_right_y_max;
    uint16_t baseline_right_y_max_span;
} dev_mag_calib_config_t;

/* Official S31 V1 profile tuned for fast contact detection */
static const dev_mag_calib_config_t DEV_MAG_CALIB_DEFAULT = {
    .name = "esp-mosaico-v1",
    .filter_samples = 3,         /* 3 samples window for fast response (<350 ms) */
    .saturation_limit = 30000,
    .left_reject_z = 650,
    .left_y_high = 1050,
    .down_y_both_sides = 60,
    .down_y_with_side = 300,
    .down_y_without_side = 300,
    .down_right_y_without_side = 240,
    .down_right_y_delta = 240,
    .up_right_y = -280,
    .up_right_y_delta = -300,
    .up_right_y_high = -400,
    .up_right_y_with_down = 20,
    .up_right_y_both_with_down = 80,
    .up_right_z_delta_with_down = 50,
    .up_right_y_fallback = -200,
    .up_left_z_with_down = 840,
    .baseline_right_y_min = -100,
    .baseline_right_y_max = 260,
    .baseline_right_y_max_span = 60,
};

typedef struct {
    dev_mag_edge_mask_t edge_mask;
    bool valid;
    bool saturated;
    uint8_t filtered_samples;
    uint32_t timestamp_ms;
} dev_mag_classification_t;

typedef struct {
    dev_mag_calib_config_t calib;
    int16_t left_x[DEV_MAG_MAX_FILTER_SAMPLES];
    int16_t left_y[DEV_MAG_MAX_FILTER_SAMPLES];
    int16_t left_z[DEV_MAG_MAX_FILTER_SAMPLES];
    int16_t right_x[DEV_MAG_MAX_FILTER_SAMPLES];
    int16_t right_y[DEV_MAG_MAX_FILTER_SAMPLES];
    int16_t right_z[DEV_MAG_MAX_FILTER_SAMPLES];
    uint8_t write_idx;
    uint8_t sample_cnt;
    int32_t baseline_right_y;
    int32_t baseline_right_z;
    bool baseline_ready;
    uint8_t calib_rejections;
    dev_mag_calib_state_t calib_state;
} dev_mag_classifier_core_t;

typedef struct {
    uint8_t attach_confirm_frames;
    uint8_t detach_confirm_frames;
    uint32_t sample_timeout_ms;
} dev_mag_dock_config_t;

#define DEV_MAG_DOCK_CONFIG_DEFAULT() { \
    .attach_confirm_frames = 2,         \
    .detach_confirm_frames = 3,         \
    .sample_timeout_ms = 1000,          \
}

typedef struct {
    dev_mag_dock_state_t state;
    dev_mag_dock_state_t prev_state;
    uint8_t attach_debounce_cnt;
    uint8_t detach_debounce_cnt;
    dev_mag_dock_config_t config;
    dev_mag_edge_mask_t last_edge_mask;
    uint32_t last_sample_ms;
    uint32_t last_transition_ms;
    uint32_t attach_detected_ms;
    uint32_t logo_presented_ms;
    uint32_t total_contacts;
    uint32_t total_releases;
    uint32_t total_transitions;
    uint32_t sample_errors;
} dev_mag_dock_sm_t;

typedef struct {
    dev_mag_classifier_core_t classifier;
    dev_mag_dock_sm_t sm;
} dev_mag_engine_t;

/* Returns true if value is saturated or overflowed (-32768) */
static inline bool dev_mag_is_saturated(int32_t limit, int32_t val)
{
    return val <= -limit || val >= limit || val == -32768;
}

static inline void dev_mag_classifier_init(dev_mag_classifier_core_t *c, const dev_mag_calib_config_t *cfg)
{
    memset(c, 0, sizeof(*c));
    c->calib = cfg ? *cfg : DEV_MAG_CALIB_DEFAULT;
    c->calib_state = DEV_MAG_CALIB_CALIBRATING;
}

static inline void dev_mag_classifier_reset(dev_mag_classifier_core_t *c)
{
    const dev_mag_calib_config_t cfg = c->calib;
    memset(c, 0, sizeof(*c));
    c->calib = cfg;
    c->calib_state = DEV_MAG_CALIB_CALIBRATING;
}

static inline dev_mag_edge_mask_t dev_mag_classify_axes(
    const dev_mag_calib_config_t *cal,
    int32_t left_x, int32_t left_y, int32_t left_z,
    int32_t right_x, int32_t right_y,
    int32_t right_y_delta, int32_t right_z_delta,
    int32_t latest_lx, int32_t latest_rx)
{
    const bool has_left = (dev_mag_is_saturated(cal->saturation_limit, left_x) ||
                           dev_mag_is_saturated(cal->saturation_limit, latest_lx)) &&
                          !(dev_mag_is_saturated(cal->saturation_limit, left_y) && left_z < cal->left_reject_z);
    const bool has_right = dev_mag_is_saturated(cal->saturation_limit, right_x) ||
                           dev_mag_is_saturated(cal->saturation_limit, latest_rx);
    const bool left_y_high = left_y > cal->left_y_high;
    bool has_top = false;
    bool has_bottom = false;

    if (has_left && has_right) {
        if (left_y_high) {
            has_top = right_y_delta < cal->up_right_y_delta;
        } else if (dev_mag_is_saturated(cal->saturation_limit, left_y)) {
            has_top = right_y < cal->up_right_y || right_y_delta < cal->up_right_y_delta;
        } else {
            has_bottom = left_y > cal->down_y_both_sides;
            has_top = right_y < cal->up_right_y || right_y_delta < cal->up_right_y_delta ||
                      (has_bottom && (left_z >= cal->up_left_z_with_down || right_y < cal->up_right_y_both_with_down));
            if (!has_bottom && right_y < cal->up_right_y_fallback) has_top = true;
        }
    } else if (has_left) {
        if (!left_y_high) {
            has_bottom = left_y > cal->down_y_with_side && right_y > cal->down_right_y_without_side;
            has_top = right_y < cal->up_right_y || right_y_delta < cal->up_right_y_delta ||
                      (has_bottom && right_y < cal->up_right_y_with_down);
        }
    } else if (has_right) {
        if (left_y_high) {
            has_top = right_y < cal->up_right_y_high || right_y_delta < cal->up_right_y_delta;
        } else {
            has_bottom = left_y > cal->down_y_with_side;
            has_top = right_y < cal->up_right_y || right_y_delta < cal->up_right_y_delta ||
                      (has_bottom && right_y < cal->up_right_y_with_down);
        }
    } else {
        has_bottom = right_y_delta > cal->down_right_y_delta || (!left_y_high && left_y > cal->down_y_without_side);
        has_top = right_y_delta < cal->up_right_y_delta ||
                  (has_bottom && (right_y < cal->up_right_y_with_down ||
                   (right_y < cal->up_right_y_both_with_down && right_z_delta > cal->up_right_z_delta_with_down)));
    }

    dev_mag_edge_mask_t mask = 0;
    if (has_top)    mask |= DEV_MAG_EDGE_TOP;
    if (has_right)  mask |= DEV_MAG_EDGE_RIGHT;
    if (has_bottom) mask |= DEV_MAG_EDGE_BOTTOM;
    if (has_left)   mask |= DEV_MAG_EDGE_LEFT;
    return mask;
}

static inline bool dev_mag_classifier_process(
    dev_mag_classifier_core_t *c,
    const dev_mag_sample_t *sample,
    dev_mag_classification_t *out)
{
    if (!c || !sample || !out) return false;
    memset(out, 0, sizeof(*out));
    out->timestamp_ms = sample->timestamp_ms;

    if (!sample->left.valid || !sample->right.valid) {
        c->sample_cnt = 0;
        c->write_idx = 0;
        return false;
    }

    const uint8_t idx = c->write_idx;
    c->left_x[idx] = sample->left.x;
    c->left_y[idx] = sample->left.y;
    c->left_z[idx] = sample->left.z;
    c->right_x[idx] = sample->right.x;
    c->right_y[idx] = sample->right.y;
    c->right_z[idx] = sample->right.z;
    c->write_idx = (uint8_t)((idx + 1) % c->calib.filter_samples);
    if (c->sample_cnt < c->calib.filter_samples) c->sample_cnt++;
    out->filtered_samples = c->sample_cnt;

    if (c->sample_cnt < c->calib.filter_samples) return false;

    int32_t lx = 0, ly = 0, lz = 0, rx = 0, ry = 0, rz = 0;
    for (uint8_t i = 0; i < c->sample_cnt; ++i) {
        lx += c->left_x[i]; ly += c->left_y[i]; lz += c->left_z[i];
        rx += c->right_x[i]; ry += c->right_y[i]; rz += c->right_z[i];
    }
    lx /= c->sample_cnt; ly /= c->sample_cnt; lz /= c->sample_cnt;
    rx /= c->sample_cnt; ry /= c->sample_cnt; rz /= c->sample_cnt;

    if (!c->baseline_ready) {
        int16_t ry_min = c->right_y[0], ry_max = c->right_y[0];
        for (uint8_t i = 1; i < c->sample_cnt; ++i) {
            if (c->right_y[i] < ry_min) ry_min = c->right_y[i];
            if (c->right_y[i] > ry_max) ry_max = c->right_y[i];
        }
        const dev_mag_edge_mask_t startup_edges = dev_mag_classify_axes(
            &c->calib, lx, ly, lz, rx, ry, 0, 0, sample->left.x, sample->right.x);
        const bool baseline_valid = ry >= c->calib.baseline_right_y_min &&
                                    ry <= c->calib.baseline_right_y_max &&
                                    (uint16_t)(ry_max - ry_min) <= c->calib.baseline_right_y_max_span &&
                                    startup_edges == 0;
        if (!baseline_valid) {
            c->calib_rejections++;
            c->calib_state = c->calib_rejections >= 3 ? DEV_MAG_CALIB_FAILED : DEV_MAG_CALIB_CALIBRATING;
            c->sample_cnt = 0;
            c->write_idx = 0;
            return false;
        }
        c->baseline_right_y = ry;
        c->baseline_right_z = rz;
        c->baseline_ready = true;
        c->calib_state = DEV_MAG_CALIB_READY;
        return false;
    }

    const int32_t ry_delta = ry - c->baseline_right_y;
    const int32_t rz_delta = rz - c->baseline_right_z;

    out->edge_mask = dev_mag_classify_axes(
        &c->calib, lx, ly, lz, rx, ry, ry_delta, rz_delta, sample->left.x, sample->right.x);
    out->saturated = dev_mag_is_saturated(c->calib.saturation_limit, lx) ||
                     dev_mag_is_saturated(c->calib.saturation_limit, ly) ||
                     dev_mag_is_saturated(c->calib.saturation_limit, lz) ||
                     dev_mag_is_saturated(c->calib.saturation_limit, rx) ||
                     dev_mag_is_saturated(c->calib.saturation_limit, ry);
    out->valid = true;
    return true;
}

static inline void dev_mag_dock_sm_init(dev_mag_dock_sm_t *sm, const dev_mag_dock_config_t *cfg)
{
    memset(sm, 0, sizeof(*sm));
    sm->config = cfg ? *cfg : (dev_mag_dock_config_t)DEV_MAG_DOCK_CONFIG_DEFAULT();
    sm->state = DEV_MAG_STATE_NORMAL;
    sm->prev_state = DEV_MAG_STATE_NORMAL;
}

/* Processes one classification and updates the dock state machine */
static inline bool dev_mag_dock_sm_process(
    dev_mag_dock_sm_t *sm,
    const dev_mag_classification_t *cls,
    uint32_t now_ms)
{
    if (!sm) return false;
    sm->last_sample_ms = now_ms;

    if (!cls || !cls->valid) {
        sm->sample_errors++;
        return false;
    }

    sm->last_edge_mask = cls->edge_mask;
    const bool right_active = (cls->edge_mask & DEV_MAG_EDGE_RIGHT) != 0;
    const dev_mag_dock_state_t old_state = sm->state;

    switch (sm->state) {
    case DEV_MAG_STATE_NORMAL:
        if (right_active) {
            sm->attach_debounce_cnt = 1;
            sm->state = DEV_MAG_STATE_ATTACH_PENDING;
            sm->attach_detected_ms = now_ms;
        }
        break;

    case DEV_MAG_STATE_ATTACH_PENDING:
        if (right_active) {
            sm->attach_debounce_cnt++;
            if (sm->attach_debounce_cnt >= sm->config.attach_confirm_frames) {
                sm->state = DEV_MAG_STATE_LOGO_ACTIVE;
                sm->attach_debounce_cnt = 0;
                sm->total_contacts++;
            }
        } else {
            // Glitch / debounce canceled
            sm->attach_debounce_cnt = 0;
            sm->state = DEV_MAG_STATE_NORMAL;
        }
        break;

    case DEV_MAG_STATE_LOGO_ACTIVE:
        if (!right_active) {
            sm->detach_debounce_cnt = 1;
            sm->state = DEV_MAG_STATE_DETACH_PENDING;
        }
        break;

    case DEV_MAG_STATE_DETACH_PENDING:
        if (!right_active) {
            sm->detach_debounce_cnt++;
            if (sm->detach_debounce_cnt >= sm->config.detach_confirm_frames) {
                sm->state = DEV_MAG_STATE_NORMAL;
                sm->detach_debounce_cnt = 0;
                sm->total_releases++;
            }
        } else {
            // Still attached / bounce
            sm->detach_debounce_cnt = 0;
            sm->state = DEV_MAG_STATE_LOGO_ACTIVE;
        }
        break;
    }

    if (sm->state != old_state) {
        sm->prev_state = old_state;
        sm->last_transition_ms = now_ms;
        sm->total_transitions++;
        return true; // State changed
    }
    return false;
}

static inline void dev_mag_engine_init(dev_mag_engine_t *eng, const dev_mag_calib_config_t *calib, const dev_mag_dock_config_t *dock_cfg)
{
    dev_mag_classifier_init(&eng->classifier, calib);
    dev_mag_dock_sm_init(&eng->sm, dock_cfg);
}

static inline bool dev_mag_engine_feed(dev_mag_engine_t *eng, const dev_mag_sample_t *sample)
{
    dev_mag_classification_t cls;
    const bool valid = dev_mag_classifier_process(&eng->classifier, sample, &cls);
    return dev_mag_dock_sm_process(&eng->sm, valid ? &cls : NULL, sample->timestamp_ms);
}
