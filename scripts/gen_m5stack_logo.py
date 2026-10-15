#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
Generate C asset and 480x480 preview image for M5Stack Logo on ESP-MOSAICO.

Reads the input PNG (default: /Users/kongweilu/Development/Mosaico EdgeTech 2026/M5stack.png),
crops transparent borders, composites with white background using alpha blending,
scales to target dimensions with Lanczos filter, and generates:
1. firmware/main/assets/m5stack_logo.h
2. firmware/main/assets/m5stack_logo.c (with both native and byte-swapped RGB565 arrays)
3. evidence/m5stack_logo_480x480.png (simulating RGB565 quantization on 480x480 display)
"""

import argparse
import os
import sys
from PIL import Image

DEFAULT_SRC = "/Users/kongweilu/Development/Mosaico EdgeTech 2026/M5stack.png"
SCREEN_W = 480
SCREEN_H = 480
TARGET_W = 420
TARGET_H = 84

def rgb888_to_rgb565(r, g, b):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5

def swap_bytes16(v):
    return ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)

def rgb565_to_rgb888(v):
    r5 = (v >> 11) & 0x1F
    g6 = (v >> 5) & 0x3F
    b5 = v & 0x1F
    r8 = (r5 << 3) | (r5 >> 2)
    g8 = (g6 << 2) | (g6 >> 4)
    b8 = (b5 << 3) | (b5 >> 2)
    return (r8, g8, b8)

def main():
    parser = argparse.ArgumentParser(description="Convert M5Stack logo PNG to RGB565 C assets.")
    parser.add_argument("--input", "-i", default=DEFAULT_SRC, help=f"Path to input PNG (default: {DEFAULT_SRC})")
    parser.add_argument("--outdir", "-o", default="firmware/main/assets", help="Directory for output C files")
    parser.add_argument("--preview", "-p", default="evidence/m5stack_logo_480x480.png", help="Path for preview PNG")
    parser.add_argument("--width", "-W", type=int, default=TARGET_W, help=f"Target logo width (default: {TARGET_W})")
    parser.add_argument("--height", "-H", type=int, default=TARGET_H, help=f"Target logo height (default: {TARGET_H})")
    args = parser.parse_args()

    input_path = os.path.abspath(args.input)
    if not os.path.isfile(input_path):
        print(f"ERROR: Input file not found: {input_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Reading input image: {input_path}")
    im = Image.open(input_path)
    print(f"  Format: {im.format}, Mode: {im.mode}, Size: {im.size} (W={im.width}, H={im.height})")

    # Find visual bounding box
    bbox = im.getbbox()
    if not bbox:
        print("ERROR: Image has no non-transparent content!", file=sys.stderr)
        sys.exit(1)

    orig_content_w = bbox[2] - bbox[0]
    orig_content_h = bbox[3] - bbox[1]
    print(f"  Visual content bounding box: {bbox}")
    print(f"  Content size: {orig_content_w} x {orig_content_h} (aspect ratio: {orig_content_w / orig_content_h:.4f})")

    cropped = im.crop(bbox)

    # Composite over pure white using alpha channel if present
    if "A" in cropped.getbands():
        white_bg = Image.new("RGBA", cropped.size, (255, 255, 255, 255))
        composite = Image.alpha_composite(white_bg, cropped).convert("RGB")
    else:
        composite = cropped.convert("RGB")

    target_w = args.width
    target_h = args.height
    print(f"Scaling logo to: {target_w} x {target_h} (aspect ratio: {target_w / target_h:.4f}) via Lanczos")
    resized = composite.resize((target_w, target_h), Image.Resampling.LANCZOS)

    offset_x = (SCREEN_W - target_w) // 2
    offset_y = (SCREEN_H - target_h) // 2
    print(f"Display layout on {SCREEN_W}x{SCREEN_H}: X offset={offset_x}, Y offset={offset_y}")

    # Generate 480x480 simulated preview
    canvas = Image.new("RGB", (SCREEN_W, SCREEN_H), (255, 255, 255))
    canvas.paste(resized, (offset_x, offset_y))

    # Apply RGB565 quantization to canvas for realistic preview
    canvas_pixels = canvas.load()
    native_words = []
    swapped_words = []

    resized_pixels = resized.load()
    for y in range(target_h):
        for x in range(target_w):
            r, g, b = resized_pixels[x, y]
            w16 = rgb888_to_rgb565(r, g, b)
            native_words.append(w16)
            swapped_words.append(swap_bytes16(w16))

    for y in range(SCREEN_H):
        for x in range(SCREEN_W):
            r, g, b = canvas_pixels[x, y]
            w16 = rgb888_to_rgb565(r, g, b)
            canvas_pixels[x, y] = rgb565_to_rgb888(w16)

    os.makedirs(os.path.dirname(os.path.abspath(args.preview)), exist_ok=True)
    canvas.save(args.preview)
    print(f"Generated simulated RGB565 preview: {args.preview}")

    # Generate C header and source
    os.makedirs(args.outdir, exist_ok=True)
    h_path = os.path.join(args.outdir, "m5stack_logo.h")
    c_path = os.path.join(args.outdir, "m5stack_logo.c")

    resource_bytes = target_w * target_h * 2
    print(f"Generating C assets (RGB565 array size: {resource_bytes} bytes / {resource_bytes / 1024:.1f} KB)...")

    with open(h_path, "w", encoding="utf-8") as fh:
        fh.write(f"""// SPDX-License-Identifier: Apache-2.0
// Generated automatically by scripts/gen_m5stack_logo.py. Do not edit manually.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define M5STACK_LOGO_WIDTH    {target_w}
#define M5STACK_LOGO_HEIGHT   {target_h}
#define M5STACK_LOGO_OFFSET_X {offset_x}
#define M5STACK_LOGO_OFFSET_Y {offset_y}

#ifdef __cplusplus
extern "C" {{
#endif

extern const uint16_t m5stack_logo_pixels[{target_w * target_h}];

/**
 * Draws the M5Stack Logo centered on a full 480x480 pure white background.
 * Writes directly into the target RGB565 double buffer (psram or s_fb).
 */
void m5stack_logo_draw(uint16_t *fb, size_t stride_px);

#ifdef __cplusplus
}}
#endif
""")

    with open(c_path, "w", encoding="utf-8") as fc:
        fc.write(f"""// SPDX-License-Identifier: Apache-2.0
// Generated automatically by scripts/gen_m5stack_logo.py. Do not edit manually.
#include "m5stack_logo.h"
#include <string.h>

#ifdef EXP_SURFACE_SWAP_BYTES
const uint16_t m5stack_logo_pixels[{target_w * target_h}] = {{
""")
        for i in range(0, len(swapped_words), 16):
            chunk = swapped_words[i:i + 16]
            fc.write("    " + ", ".join(f"0x{v:04X}" for v in chunk) + ",\n")
        fc.write(f"""}};
#else
const uint16_t m5stack_logo_pixels[{target_w * target_h}] = {{
""")
        for i in range(0, len(native_words), 16):
            chunk = native_words[i:i + 16]
            fc.write("    " + ", ".join(f"0x{v:04X}" for v in chunk) + ",\n")
        fc.write(f"""}};
#endif

void m5stack_logo_draw(uint16_t *fb, size_t stride_px)
{{
    if (!fb || stride_px < {SCREEN_W}) return;

    // Fill 480x480 screen with pure white (0xFFFF in RGB565, identical byte-swapped or native)
    for (size_t y = 0; y < {SCREEN_H}; ++y) {{
        uint16_t *row = fb + y * stride_px;
        uint32_t *row32 = (uint32_t *)row;
        for (size_t x = 0; x < {SCREEN_W // 2}; ++x) {{
            row32[x] = 0xFFFFFFFFu;
        }}
    }}

    // Blit the logo pixels in the center
    for (size_t r = 0; r < M5STACK_LOGO_HEIGHT; ++r) {{
        uint16_t *dst = fb + (M5STACK_LOGO_OFFSET_Y + r) * stride_px + M5STACK_LOGO_OFFSET_X;
        const uint16_t *src = &m5stack_logo_pixels[r * M5STACK_LOGO_WIDTH];
        memcpy(dst, src, M5STACK_LOGO_WIDTH * sizeof(uint16_t));
    }}
}}
""")

    print(f"Header written to: {h_path}")
    print(f"Source written to: {c_path}")
    print("Asset generation complete.")

if __name__ == "__main__":
    main()
