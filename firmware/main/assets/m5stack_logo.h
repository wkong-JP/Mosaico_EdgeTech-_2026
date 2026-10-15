// SPDX-License-Identifier: Apache-2.0
// Generated automatically by scripts/gen_m5stack_logo.py. Do not edit manually.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define M5STACK_LOGO_WIDTH    420
#define M5STACK_LOGO_HEIGHT   84
#define M5STACK_LOGO_OFFSET_X 30
#define M5STACK_LOGO_OFFSET_Y 198

#ifdef __cplusplus
extern "C" {
#endif

extern const uint16_t m5stack_logo_pixels[35280];

/**
 * Draws the M5Stack Logo centered on a full 480x480 pure white background.
 * Writes directly into the target RGB565 double buffer (psram or s_fb).
 */
void m5stack_logo_draw(uint16_t *fb, size_t stride_px);

#ifdef __cplusplus
}
#endif
