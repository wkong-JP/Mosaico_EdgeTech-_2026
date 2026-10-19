# ESP-MOSAICO Experience Firmware (EdgeTech+ 2026) — English Documentation

## 1. Introduction

Built for the EdgeTech+ 2026 exhibition in Japan, this is the official flagship offline experience firmware for **ESP-MOSAICO V1.2** powered by the **ESP32-S31** dual-core RISC-V SoC.

The firmware fully capitalizes on the chip's ultra-fast **16 MB Octal PSRAM clocked at 250 MHz** and dual 32-bit RISC-V cores. It brings together on-device multi-modal AI (ESP-SR offline speech recognition and ESP-DL hand gesture tracking), multi-sensor fusion (dual BMM150 magnetometers + BMI270 6-axis IMU), real-time physics simulation, and fluid 480 × 480 AMOLED animations.

Operating under the strict **Route B Zero-RF** guideline, all wireless radios (Wi-Fi, Bluetooth, ESP-NOW) are completely omitted from the build, providing a safe, reliable, and compliant out-of-the-box offline experience.

---

## 2. Core Features & Five Screen Layouts

### Screen 1: Home Dashboard
- Status bar displaying accurate battery percentage (calibrated with 80 mAh cell discharge characteristics) and system metrics.
- Real-time magnetic accessory detection on the right dock.
- Automatic exhibition mode (Attract Mode) cycling through screens after 30 seconds of inactivity; touch instantly returns to interactive mode.

### Screen 2: Motion Sensor Fusion
- Sensor fusion combining the BMI270 6-axis IMU and dual BMM150 magnetometers.
- Real-time 3D attitude dials (Pitch and Roll) and heading compass.
- Touch-to-calibrate magnetic baseline deduction for reliable compass readings in exhibition environments.

### Screen 3: Audio & AI Speech
- **24-Band Dynamic Waterfall Spectrum**: Buffered in PSRAM circular history for smooth neon frequency visualizations.
- **Dual-Language Offline Voice Navigation**:
  - Wake word: `"Hi ESP"` (also supports Japanese `"こんにちは ESP"`).
  - 58 recognized command phrases across English and Japanese with G2P phonetic compensation for natural accents.
  - Commands: `"Home"`, `"Motion"`, `"Audio"`, `"Game"`, `"Device"`, `"System"`, `"Settings"`, `"Hardware"`, `"About"`.

### Screen 4: Tilt Maze (Game)
- Real-time physics ball maze simulation with Easy, Normal, and Hard layouts.
- **Three Input Modes**:
  1. **Tilt**: Tilt the device to steer the ball with gravity;
  2. **Joystick**: Plug in the left joystick module to control via thumbstick;
  3. **Camera**: Use the SC101IOT camera and ESPDet-Pico hand detector to navigate by waving your hand in front of the lens.
- **Continuous 30 FPS Gesture Smoothing & Comet Trail**: Bridges the 4.5 FPS neural net inference with smooth exponential lerp, leaving a fluid glowing comet tail in PSRAM without CPU lag.

### Screen 5: Device Architecture Visualizer
- **16 MB PSRAM & Flash Chart**: Highlighting the dual-memory architecture and memory distribution with over 6.0 MB of free PSRAM headroom.
- **Sub-Board Slot Probing**: Automatic identification of left and right expansion modules and EEPROM descriptors.

### Easter Egg: Espressif Official Logo Snap
- Attaching a magnetic module to the right side triggers a 17 ms hardware interrupt, suspending the camera and displaying the full-screen official Espressif logo; detaching instantly restores the previous application state.

---

## 3. Display & Curved Corner Optimization

- Display: 480 × 480 AMOLED with CO5300 driver.
- **Bezel Clipping Protection**: Title and battery status are inset by 28 px horizontally; game HUD buttons and time text are pulled away from corners; outer tab panels feature an 18 px corner radius to match the glass curve.

---

## 4. Flashing Instructions

This release is packaged as a **single unified BIN image at Offset 0x0**, combining the bootloader, partition table, application, and speech models.

### Image Details:
- File name: `esp32s31_mosaico_experience_full_0x0.bin`
- Flash offset: `0x0`
- Size: 9,701,827 bytes (~9.25 MB)
- SHA256: `2f4bb03080c6ac8268f9fa8cc6acfeedeecf004451da039f598f875a982e17b3`
- MD5: `0daa34a87efb7375ac80dc1d41b6d4ef`

### Flash Command:
```bash
# 1. Enter ROM download mode: Hold BOOT -> Click RESET -> Release BOOT
# 2. Flash at 460800 baud:
esptool --chip esp32s31 -b 460800 write-flash 0x0 esp32s31_mosaico_experience_full_0x0.bin
```
Press RESET after flashing completes to boot the firmware.
