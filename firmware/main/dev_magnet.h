// SPDX-License-Identifier: Apache-2.0
// Device magnetic dock detection driver for Left MOSAICO board.
// Connects sensor samples to the dock state machine and logs via Iris CDC0.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "dev_magnet_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    dev_mag_dock_state_t state;
    dev_mag_calib_state_t calib_state;
    dev_mag_edge_mask_t edge_mask;
    bool logo_active;
    bool baseline_ready;
    int32_t baseline_right_y;
    int16_t last_raw_right[3];
    int16_t last_raw_left[3];
    uint32_t last_sample_ms;
    uint32_t last_transition_ms;
    uint32_t detect_latency_ms;
    uint32_t present_latency_ms;
    uint32_t total_contacts;
    uint32_t total_releases;
    uint32_t sample_errors;
} dev_mag_dock_status_t;

void dev_magnet_init(void);
void dev_magnet_process_sample(const dev_mag_sample_t *sample);
void dev_magnet_take_status(dev_mag_dock_status_t *out);
bool dev_magnet_is_logo_active(void);
void dev_magnet_notify_presented(void);

#ifdef __cplusplus
}
#endif
