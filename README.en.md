# ESP-MOSAICO Experience — EdgeTech+ 2026 Flagship Offline Showcase Firmware

**English** · [简体中文](README.md)

[![Target: ESP32-S31](https://img.shields.io/badge/Target-ESP32--S31-red.svg)](https://www.espressif.com/)
[![PSRAM: 16MB Octal @ 250MHz](https://img.shields.io/badge/Octal_PSRAM-16MB_%40_250MHz-blue.svg)](https://www.espressif.com/)
[![Display: 480x480 AMOLED](https://img.shields.io/badge/Display-480x480_AMOLED-orange.svg)]()
[![RF: Zero-RF Route B](https://img.shields.io/badge/RF_Policy-Strictly_Offline_(Zero--RF)-green.svg)]()
[![Firmware: v0.9.4-stage4d](https://img.shields.io/badge/Firmware-v0.9.4--stage4d-brightgreen.svg)]()
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)

This firmware is exclusively crafted for the **EdgeTech+ 2026 Exhibition in Yokohama, Japan**, serving as the official flagship offline multimodal interactive showcase for **ESP-MOSAICO V1.2 (powered by Espressif's next-generation flagship SoC ESP32-S31)**.

Adhering strictly to the **Route B Zero-RF (Fail-Closed)** exhibition compliance policy, the firmware operates completely isolated from wireless network stacks. It fully unleashes the computational power of the dual-core RISC-V architecture and **16 MB Octal PSRAM (clocked at 250 MHz DDR)** on the ESP32-S31. It seamlessly integrates **offline dual-language voice control (ESP-SR)**, **real-time AI camera gesture tracking (ESPDet)**, **6-axis IMU & dual-magnetometer 3D attitude fusion**, **a high-precision physics maze engine**, **AMOLED 480×480 fluid visual effects**, and **instant M5Stack magnetic dock branding**, delivering a responsive, captivating, and highly interactive visitor experience.

---

## 🌟 Key Engineering & Architectural Highlights

- 🛡️ **Strictly Offline Route B Zero-RF (Fail-Closed Policy)**  
  Completely strips out Wi-Fi, BLE, ESP-NOW, and 802.15.4 stacks. Wireless initialization is strictly prohibited during boot. All static link symbols and binary maps are audited with 0 RF findings, eliminating local radio regulatory risks and on-site channel interference.
- ⚡ **16 MB Octal PSRAM at 250 MHz DDR**  
  Allocates dual full-color rendering buffers, DVP camera DMA channels, ESPDet-Pico deep learning network weights, and a 24-band audio spectrum circular buffer. Even under concurrent peak loads of vision and voice AI, the system maintains **over 5.0 MB of free heap**, demonstrating exceptional throughput and headroom.
- 🎙️ **Multimodal Offline Dual-Language Voice Control (ESP-SR)**  
  Powered by WakeNet and MultiNet7 offline engines. Features dual wake words (Japanese **"こんにちは ESP"** and English **"Hi ESP"**). Registers **58 offline commands** covering menu navigation, camera shutter, and game interactions, complete with G2P phonetic compensation for non-native pronunciations.
- 👁️ **Real-Time Camera Hand Gesture Tracking (ESPDet-Pico + PPA)**  
  Drives the left-slot SC101IOT DVP camera sensor with on-chip PPA hardware acceleration (1280×720 UYVY → 480×480 RGB565). Core 1 independently runs lightweight asynchronous hand detection, achieving responsive non-contact game control with an 18 px deadband and 1.6× dynamic gain.
- 🧲 **Instant M5Stack Magnetic Dock Recognition & Seamless Recovery**  
  Utilizes the onboard dual BMM150 magnetic saturation detection. Upon contact with the right-edge magnetic dock, hardware interrupts trigger a **17 ms instantaneous full-screen M5Stack logo display**, safely suspending camera streaming to conserve power. Upon detachment, the screen seamlessly restores the previous state with **0 dynamic heap allocation, 0 flicker, and 0 reboots**.
- 🎮 **Real Gravity Physics Maze (Tilt Maze)**  
  Equipped with a custom momentum and collision physics engine across three difficulty tiers. Supports **Tilt**, **Joystick**, and **Camera Gesture** control, featuring a **gesture comet glow trail** and real-time Picture-in-Picture (PiP) tracking HUD.
- 📱 **Curved AMOLED Anti-Clipping Layout**  
  Tailored for 480×480 circular/rounded AMOLED displays (CO5300 controller). Safe insets are applied to headers, battery indicators, exit buttons, and navigation tabs to prevent visual corner clipping.
- 🔌 **ESP-Iris USB CDC0 Real-Time Non-Intrusive Streaming Logs**  
  Leverages the ESP32-S31 high-speed USB OTG UTMI PHY for dedicated hardware logging. Eliminates intrusive on-screen yellow debug overlays (`EXP_TIMING_OVERLAY=0`). Visitors enjoy a clean, polished UI, while engineers monitor live, zero-packet-drop telemetry on PC.

---

## 📱 Five Interactive Showcase Pages

```
[Home Dashboard] ──► [Motion 3D] ──► [Audio & AI] ──► [Game Maze] ──► [Device Info]
       ▲                                                                      │
       └────────────────── (45s Idle Auto Cruise: Attract Mode) ──────────────┘
```

| Page | Features & Highlights |
|---|---|
| **1. Home Dashboard** | System status, precision battery gauge calibrated against 80 mAh real discharge curves, right-edge magnetic module status, and 45-second automatic Attract Mode cruise loop. |
| **2. Motion & Compass** | Real-time 3D attitude (pitch & roll) calculated via BMI270; dual BMM150 magnetometers drive a tilt-compensated compass pointing true magnetic north, with 1-tap baseline calibration. |
| **3. Audio & AI Voice** | ES8311 codec microphone sampling, PSRAM circular buffers rendering a **24-band neon audio spectrum waterfall**, with live speech recognition status and detected command feedback. |
| **4. Game (Tilt Maze)** | Easy / Normal / Hard tiers, switchable between Tilt, Joystick, and Hand Gesture modes; features comet tail glow and PiP camera HUD. |
| **5. Device Info** | Visual breakdown of 16 MB Octal PSRAM and 16 MB Flash memory partitions; automatic live detection of sub-board modules (Camera, Joystick, Sensors) via EEPROM descriptors. |
| **Easter Egg: M5Stack Logo** | Attaching the right magnetic dock triggers an instantaneous white M5Stack branding display, recovering smoothly upon removal. |

---

## 🎙️ Offline Voice Commands Matrix (ESP-SR)

* **Wake Words**:
  * **Japanese**: 「こんにちは ESP」 (*Konnichiwa ESP*)
  * **English**: "Hi ESP"
* **Supported Commands (58 Registered Phrases)**:

| Category | Japanese Command | English Command | Action Triggered |
|---|---|---|---|
| **Navigation** | ホーム / 最初のページ | Home / Main menu | Navigate to Home page |
| | モーション / コンパス | Motion / Compass | Navigate to Motion page |
| | オーディオ / 音声 | Audio / Music | Navigate to Audio & AI page |
| | ゲーム / 迷路 | Game / Maze | Navigate to Game page |
| | デバイス / システム | Device / Info | Navigate to Device Info page |
| **Maze Control** | 右へ / みぎ | Turn Right | Tilt maze physics rightward |
| | 左へ / ひだり | Turn Left | Tilt maze physics leftward |
| | 上へ / うえ | Move Up | Tilt maze physics upward |
| | 下へ / した | Move Down | Tilt maze physics downward |
| | スタート / 始め | Start | Start game timer |
| | リセット / やり直し | Reset | Reset ball to initial spawn point |
| **Quick Actions** | 写真を撮って / チーズ | Take Photo | Trigger shutter simulation |
| | 戻る / 前のページ | Go Back | Return to previous view |

---

## 🛠️ Hardware & Architecture

### Hardware Specifications

| Component | Model / Specification | Description |
|---|---|---|
| **Core SoC** | **ESP32-S31** (Dual-core RISC-V @ 240 MHz) | Flagship multimedia & edge AI SoC |
| **External RAM** | **16 MB Octal PSRAM** (250 MHz DDR) | High-speed cache for AI inference & video pipelines |
| **External Flash** | **16 MB Quad SPI Flash** | Houses Bootloader, App, 68KB logo asset & 4MB voice models |
| **Display** | 1.43" 480 × 480 AMOLED (CO5300) | Full-lamination AMOLED, vibrant colors, low power |
| **IMU Sensor** | Bosch BMI270 (6-axis Accel + Gyro) | High-precision 3D orientation & motion tracking |
| **Magnetometer** | Dual Bosch BMM150 (Magnetometer array) | Compass navigation & strong magnetic dock detection |
| **Camera Sensor** | SC101IOT (DVP interface, 1280×720 UYVY) | Left expansion slot module, hardware PPA accelerated |
| **Audio Codec** | ES8311 Codec + MEMS Mic + Speaker | High-fidelity audio capture for ESP-SR voice recognition |
| **Touch Controller**| Hynitron CST9220 | Multi-point capacitive touch panel |
| **USB Telemetry** | USB OTG High-Speed UTMI PHY (ESP-Iris) | Hardware USB CDC0 lossless streaming logs |

---

## ⚡ Key Reliability Fixes (Senior Dev Notes)

1. **Zero-Starvation DVP DMA Driver Optimization**  
   * **Issue**: Older revisions waited for both PPA scale and AI hand detection (200~300ms) before returning buffers, starving the DVP DMA queue and causing ISR panic crashes.  
   * **Fix**: In `dev_camera.c`, the sensor DMA buffer is returned to the queue **immediately after PPA hardware transfer completes** (~3ms). Verified with 1200+ continuous frames without a single drop or restart.
2. **PSRAM Allocation Sizing**  
   * Configured camera DVP frame buffers strictly to 2 ($2 \times 1.84\text{ MB} = 3.68\text{ MB}$), preserving over 5.0 MB of free PSRAM and eliminating `ENOMEM` (errno 12) errors.
3. **Battery-Backed Crash-Guard Auto-Reset**  
   * **Issue**: Because the MOSAICO board has an onboard lithium battery (~4.19V), `__NOINIT_ATTR` registers persist across USB unplugs. Unrelated startup crashes latched the crash-guard strikes, permanently showing "音声操作 エラー".  
   * **Fix**: Crash guards are released immediately upon reaching `READY`, and boot routines actively clear residual strike counts, guaranteeing clean recovery after soft resets.

---

## 🚀 Installation & Flashing

A pre-built **single unified flash image (Offset 0x0)** is available in `release/`, containing Bootloader, Partition Table, Application, and offline Voice Models.

### Binary Info

* **Path**: `release/esp32s31_mosaico_experience_full_0x0.bin`
* **Offset**: `0x0`
* **Size**: 9,701,827 bytes (~9.25 MB)
* **SHA-256**: `2f4bb03080c6ac8268f9fa8cc6acfeedeecf004451da039f598f875a982e17b3`

### Flashing Instructions

1. **Enter ROM Download Mode**:
   * Connect the board to your Mac/PC via USB-C;
   * Press and hold the **BOOT button** $\rightarrow$ Click the **RESET button** $\rightarrow$ Release the **BOOT button**.
2. **Flash with esptool** (at 921600 baud, ~15 seconds):

```bash
esptool --chip esp32s31 -b 921600 write-flash 0x0 release/esp32s31_mosaico_experience_full_0x0.bin
```

3. Press the **RESET button** once to boot up.

### Building from Source

```bash
# Set up ESP-IDF environment (v6.1 or v6.2-dev recommended)
. ~/esp/esp-idf/export.sh

cd mosaico-experience/firmware
idf.py build

# Flash to device
idf.py -p /dev/cu.usbmodem11401 flash

# Run live telemetry monitor
python3 ../scripts/iris_log.py -p /dev/cu.usbmodem11401
```

---

## 📄 License & Credits

* **Firmware Code**: Distributed under the [Apache License 2.0](LICENSE).
* **Font Assets**: Derived from Noto Sans CJK JP, licensed under [SIL Open Font License 1.1](LICENSES/NotoSansCJK-OFL.txt).
* **Trademarks**: `ESP32`, `ESP-MOSAICO`, and `ESP-IDF` are trademarks of Espressif Systems. `M5Stack` is a trademark of Shenzhen Mingstac Technology Co., Ltd.
