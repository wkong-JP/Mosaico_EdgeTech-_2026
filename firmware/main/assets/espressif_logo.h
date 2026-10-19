// SPDX-License-Identifier: Apache-2.0
// Generated automatically by scripts/gen_espressif_logo.py. Do not edit manually.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define ESPRESSIF_LOGO_WIDTH    420
#define ESPRESSIF_LOGO_HEIGHT   76
#define ESPRESSIF_LOGO_OFFSET_X 30
#define ESPRESSIF_LOGO_OFFSET_Y 202

#ifdef __cplusplus
extern "C" {
#endif

extern const uint16_t espressif_logo_pixels[31920];

/**
 * Draws the Espressif Logo centered on a full 480x480 pure white background.
 * Writes directly into the target RGB565 double buffer (psram or s_fb).
 */
void espressif_logo_draw(uint16_t *fb, size_t stride_px);

#ifdef __cplusplus
}
#endif
