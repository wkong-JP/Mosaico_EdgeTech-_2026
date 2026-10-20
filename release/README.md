# Release Package: ESP-MOSAICO Experience Flagship Firmware

This directory contains the latest production artifacts for the **ESP-MOSAICO Experience** flagship firmware, ready for publishing to **Mosaico Ideas** or local flashing.

## Package Contents

| File | Size | Description |
| :--- | :--- | :--- |
| `esp32s31_mosaico_experience_full_0x0.bin` | 9.25 MB (9,701,827 bytes) | Unified full partition firmware image for offset `0x0`. |
| `cover_mosaico_experience.png` | 338 KB (1920 × 1080) | High-resolution 16:9 Hero cover image with device mockups & Espressif logo. |
| `cover_mosaico_experience.webp` | 130 KB (1920 × 1080) | Web-optimized 16:9 Hero cover image (ideal for Mosaico Ideas upload). |
| `cover_screens_showcase.png` | 340 KB (1920 × 1080) | 16:9 Five-screen feature showcase banner. |
| `cover_screens_showcase.webp` | 138 KB (1920 × 1080) | Web-optimized 16:9 feature showcase banner. |

## Checksums

- **SHA-256**: `2f4bb03080c6ac8268f9fa8cc6acfeedeecf004451da039f598f875a982e17b3`
- **MD5**: `0daa34a87efb7375ac80dc1d41b6d4ef`

## One-Line Flash Command

Connect ESP-MOSAICO via USB, enter download mode (Hold BOOT $\to$ Click RESET $\to$ Release BOOT), then run:

```bash
esptool --chip esp32s31 -b 460800 write-flash 0x0 esp32s31_mosaico_experience_full_0x0.bin
```

## Documentation

- **Mosaico Ideas Ready-to-Publish Markdown**: [`docs/Mosaico_Ideas_Description.md`](../docs/Mosaico_Ideas_Description.md)
- **Detailed Chinese Documentation**: [`docs/Mosaico_Experience_Release_CN.md`](../docs/Mosaico_Experience_Release_CN.md)
- **Detailed English Documentation**: [`docs/Mosaico_Experience_Release_EN.md`](../docs/Mosaico_Experience_Release_EN.md)
