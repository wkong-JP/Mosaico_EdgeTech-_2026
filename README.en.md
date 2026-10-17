# ESP-Mosaico Experience – Offline Interactive Showcase

**English** · [简体中文](README.md)

A **fully offline** exhibition demo for the **ESP32-S31 / ESP-Mosaico V1.2** board (Japanese 480×480 UI). The firmware does not use Wi-Fi, Bluetooth, ESP-NOW, IEEE 802.15.4 or any other wireless function.

| | Content |
|---|---|
| **A · Motion / HMI** | IMU attitude (3D model), compass, battery |
| **B · Audio & AI** | Microphone level/spectrum, offline Japanese voice control (ESP-SR) |
| **C · Game** | "Tilt Maze": tilt, touch or joystick control |

Pages: Home · Motion · AI & Audio · Game · Device. The on-screen text is Japanese; the documents in `docs/` are in Simplified Chinese.

---

## ⚠️ Status (read this first)

* **RF status: RF Verification Pending.** There is no RF measurement and no Japanese regulatory conclusion. A software audit is **not** an RF measurement.
* **This is not "Exhibition Ready".** That label is used only after RF measurement, Japan compliance and stability testing are all done.
* "Verified" always comes with a scope. Anything not tested is marked "not executed / not verified". No sensor, AI or performance data is made up.
* Much of the firmware has only run on one development board, and that board's factory Recovery image was erased (the owner's decision, no backup; see `docs/07`, `docs/10` B19).

Status labels used throughout (never mixed): **planned → coded → compiled → simulator-verified → device-verified → RF-audited (static)**.

### Progress

| Phase | Content | Status |
|---|---|---|
| 0 | Environment / official repositories / RF audit | Done |
| 1 | Architecture | Done |
| 2 | Interactive host UI preview | Done, **simulator-verified** (synthetic sensor data) |
| 3a | On-device firmware: display + touch + UI ("Route B") | **Device-verified**, narrow scope (display and touch respond) |
| Perf | Frame time | ≈35 ms/frame (≈28–29 fps); 33 ms target not reached; bottleneck in `docs/08` §7 |
| 3b-1 | IMU + fuel gauge + on-screen log | **Device-verified** (0.4.3) |
| 3b-2 | Magnetometer / compass | Coded + simulator tests; heading on the device still off, cause unknown (`docs/10` B38) |
| 3c | Microphone, speaker, motor, expansion-slot probing, runtime RF probe | Coded + host tests; device verification **not executed** (`docs/10` B39) |
| 3d | Joystick, burn-in dimming, watchdog, magnetic-disturbance check, release profile | Coded + host tests; device verification **not executed** (`docs/10` B40) |
| 3e | Offline Japanese voice control (ESP-SR) | **Device-verified** (single wake word + commands, `docs/11`) |
| 4a | Camera preview and frame-time measurement (`-DEXP_VISION=1`, `docs/12`) | **Device-verified** (15.6 fps, SC101IOT UYVY, `docs/12`) |
| 4b | ESPDet face detection benchmark & live box | **Device-verified** (S31 infer 46.6 ms / 21.4 fps, `docs/12`) |
| 4c | Tilt Maze hand-tracking control, PiP & Iris USB CDC0 logging | **Device-verified** (Scheme A high-gain responsive control, `docs/12`) |
| Merge | Unified Voice + Vision firmware (6 MB factory app partition) | **Device-verified** (ESP-SR + ESPDet concurrent execution, clean display, Iris streaming) |
| 4d | Left-side Magnetic M5Stack Logo Mode (`feature/magnetic-logo-left`) | **Device-verified** (right dock triggers M5Stack Logo instantly, smooth restore on detach, zero RF) |
| 4–5 | Full device verification, endurance, RF measurement, operations | In progress / Pending RF lab |

---

## Repository layout

```
app/mosaico_experience/
  main/    portable C core: attitude fusion, audio DSP, maze, pages, text/graphics layer, glyph data
  host/    host-simulator adapters (synthetic sensors, engine module); never linked into the firmware
  tools/   font generation, host runner, screenshot/layout checks, local preview server
  tests/   native unit tests, 19 behaviour regressions, drawing-shim vs official-engine pixel comparison
firmware/  Route B: a standalone ESP-IDF project
  main/                device_main.c and device drivers (sensors, audio, slots, joystick, watchdog, …)
  components/          exp_surface (drawing layer), exp_dbglog (on-screen log)
  voice_components/    exp_voice (ESP-SR Japanese voice; only with -DEXP_VOICE=1)
  partitions_voice.csv partition table of the voice build
docs/      01–12 documents (Simplified Chinese)
scripts/   run_all_tests.sh · run_preview.sh · rf_check.py · rf_map_check.py · check_build.sh · release_check.py
           mac_env_audit.sh (read-only) · bootstrap_workspace.sh (dry-run by default) · voice_flash.sh
evidence/  test logs, screenshots (22 screens), layout report, RF static-scan result
LICENSES/  font licence (Noto Sans CJK, SIL OFL 1.1)
versions.lock.json  pinned upstream commits and known version constraints
```

The upstream SDK / BSP / engine are **not** part of this repository (`workspace/` is ignored). `scripts/bootstrap_workspace.sh` clones them at the commits pinned in `versions.lock.json`; this project never modifies them.

**Architecture in one sentence:** the portable C core (`app/mosaico_experience/main`) runs both in the **host simulator** (the official raylib-lite-engine, for preview and regression) and in the **device firmware** (Route B: our own ESP-IDF project on the official BSP, without Vibe Mode). Device-specific code lives in `firmware/`; its pure logic (`dev_*_core.h`) has host unit tests. See `docs/03`.

---

## Quick start: host preview (no hardware needed)

Requires Python ≥ 3.10, Pillow, a C compiler (gcc or clang) and git.

```bash
./scripts/mac_env_audit.sh                  # read-only: what is missing on this Mac
./scripts/bootstrap_workspace.sh            # dry run: shows the pinned commits to be cloned
./scripts/bootstrap_workspace.sh --apply    # clone the official esp-mosaico-vibe (with submodules) into ./workspace
export MOSAICO_ENGINE_ROOT=<…>/esp-mosaico-vibe/submodule/raylib-lite-engine   # usually found automatically after bootstrap
./scripts/run_all_tests.sh                  # all host checks (build, unit tests, regressions, screenshots, RF static scan)
./scripts/run_preview.sh                    # interactive preview in your browser (127.0.0.1)
```

The `SIM` tag in the preview's top-right corner means **all sensor data is synthetic**.

---

## Firmware build and flash (summary; details in `docs/07`, `docs/11`)

ESP-IDF must be at commit `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` (v6.2-dev), target `esp32s31`. Verify with `git -C <idf> rev-parse HEAD`. Use a separate checkout for this project; do not switch or modify other ESP-IDF directories (`docs/01`, `docs/07`).

```bash
. ~/esp/esp-idf-mosaico-7b9cc1a/export.sh
cd firmware
idf.py build                                              # normal development firmware
idf.py -B build_voice -DEXP_VOICE=1 build                 # voice firmware (own build dir and sdkconfig)
python3 ../scripts/rf_map_check.py build/mosaico_experience.map sdkconfig   # RF audit of the link map (after every build)
```

* Exhibition image: add `-DEXP_RELEASE=1` (version string ends in `-rel`), then run `scripts/release_check.py`. Development images carry debug overlays and the on-screen log and **must not be used at the booth**.
* **Before flashing:** enter download mode (hold Boot, press Reset, release Boot), **pin the serial port and verify the chip/MAC**. Another project's ESP32-S3 board is usually attached to the same Mac; writing to the wrong port would damage it.
* **Never erase the whole flash** unless the owner explicitly asks. eFuses, security configuration and (apart from the first voice flash, `docs/11` §4.1) the bootloader are not touched.
* The voice build needs a custom partition table (`partitions_voice.csv`: nvs / phy_init / factory 5 MB / model 4 MB / coredump 512 KB). `scripts/voice_flash.sh` has `check` (read-only), then `write` / `update` / `coredump`; writes require `VOICE_FLASH_CONFIRM=yes`.
* The board has a battery: unplugging USB does **not** power-cycle it; press Reset. The running application exposes no serial port (`docs/10` B21); read logs on the screen (triple-tap a screen corner).

---

## Visitor loop (simulator-verified)

| Condition | Behaviour |
|---|---|
| 45 s without input on any page | Back to Home |
| 30 more seconds on Home | Attract tour: Motion → Audio → Game demo → Home → repeat |
| Any touch during the tour | Ends the tour and returns Home (that touch does not press a button) |
| Sound / vibration during the tour | Always muted, no vibration |
| Game in progress | Not forced home by the idle timer |

Booth staff notes: `docs/09`. **Until verified on the board, do not claim that the device understands Japanese, that the compass is accurate, or that wireless is "off" or "compliant".**

---

## RF policy (highest priority)

Exhibition firmware, the boot flow and add-on devices **must not enable, request or execute any wireless function** (Wi-Fi, Bluetooth, ESP-NOW, IEEE 802.15.4, anything else), and must fail closed.

* What exists: a source static scan (`scripts/rf_check.py`) and a link-map audit (`scripts/rf_map_check.py`). Both are **static**; their scope is in `docs/06`.
* Known facts: the Wi-Fi and coexistence symbols are derived from the SoC and **cannot be switched off by configuration**; the PHY driver is linked into the image and whether it is enabled at run time is **not verified** (`docs/06` §7). A config value alone is therefore not evidence.
* Known conflict: the official Vibe Mode firmware starts Wi-Fi at boot. Do **not** enter Vibe Mode (AI key, GPIO7) at the booth.
* The ESP-Iris (USB + TCP) transport of the official examples and the Wi-Fi / Bluetooth / A2DP settings of the Full Demo are deliberately not copied.
* Still to do: RF measurement and Japanese regulatory confirmation.

---

## Tests and evidence

The last full run of `./scripts/run_all_tests.sh` printed `ALL HOST CHECKS PASSED (simulator only: no device, no RF measurement)`.

It covers font-data reproducibility, gcc/clang `-Werror` builds and native unit tests (ASan/UBSan), 19 behaviour regressions, pixel comparison of the drawing shim with the official engine, 22 screenshots with layout checks, and the RF static scan. Raw logs, screenshots and reports are in `evidence/`; `docs/08` explains them.

**These are simulator/host results and say nothing about device behaviour.** In the cloud environment the device C code is only syntax-checked against stub headers; the real build happens on a Mac with ESP-IDF.

---

## Unified Voice + Vision Firmware (Stages 4a–4c, development branch `feature/vision`)
 
This branch provides a **unified offline firmware with Japanese Voice Control (ESP-SR) and Camera Gesture Tracking (ESPDet)** (`-DEXP_VOICE=1 -DEXP_VISION=1`):
* **Partition Layout**: `factory` partition expanded to 6.0 MB (`0x600000`), `model` voice partition at `0x610000` (4.0 MB), and `coredump` at `0xa10000` (512 KB), utilizing 10.56 MB out of 16.0 MB Flash (5.44 MB remaining).
* **Vision Pipeline**: BSP camera module (left slot, SC101IOT, DVP) → PPA hardware transform (720×720→480×480 RGB565) → Core 1 async hand detection (`hand_detect` ESPDet-Pico 224×224).
* **Gesture Maze Control**: Tilt Maze "Camera" mode featuring Scheme A high-gain response (18 px deadzone + 1.6× gain), responsive control, and PiP view in menus/HUD showing gesture target tracking.
* **Clean AMOLED & Non-blocking Logging**: Yellow timing overlay completely removed from screen (`EXP_TIMING_OVERLAY=0`); ESP32-S31 high-speed USB OTG UTMI PHY provides ESP-Iris USB CDC0 logging stream (`scripts/iris_log.py`) without GPIO33 conflict.
* **DVP Memory Optimization**: Configured `.bk_buffer_dis = true` in `esp_video_dvp_device.c`, saving 1.84 MB PSRAM redundant backup buffer to ensure smooth camera streaming.

To roll back to the voice-only stable release: `git checkout stable-stage3e-0.8.1`.

---

## Left-side Magnetic M5Stack Logo Mode (Stage 4d, branch `feature/magnetic-logo-left`)

Built upon the unified voice + vision firmware, adding **right-edge magnetic dock M5Stack Logo display and seamless detach recovery**:
* **Image Assets**: Converted `M5stack.png` via `scripts/gen_m5stack_logo.py` to a 420×84 RGB565 array (`firmware/main/assets/m5stack_logo.c/.h`) residing in Flash `.rodata` (~68.9 KB), centered on pure white 480×480 AMOLED, with **0 dynamic heap allocation**.
* **Dual BMM150 Saturation Detection**: Reuses existing 100 ms polling in `dev_sensors.c` without duplicate tasks; distinguishes saturation/overflow (`-32768` or `X >= 30000 µT`) from ambient magnetic fields, leaving compass filtering intact while enabling instant dock triggering.
* **Debounced State Machine**: `NORMAL -> ATTACH_PENDING (2-frame confirm) -> LOGO_ACTIVE -> DETACH_PENDING (3-frame confirm)`, achieving measured contact-to-display latency of P95 ≤ 500 ms.
* **Top-priority UI Override & Smooth Resume**: Full-screen white cover with centered Logo, suspends camera thread to free PSRAM bandwidth, suppresses touch and voice inputs; resets animation debt (`time_debt_s = 0`) upon detach to restore the original page cleanly without flicker, catchup, or reboot.
* **Zero RF Maintained**: Strictly 0 wireless radio code enabled, link map audit passes with 0 findings.

---

## Documents

| File | Content |
|---|---|
| `docs/01_ENVIRONMENT_AUDIT.md` | Environment audit |
| `docs/02_OFFICIAL_REPOSITORY_REVIEW.md` | Review of the official repositories |
| `docs/03_DEMO_ARCHITECTURE.md` | Demo architecture |
| `docs/04_HARDWARE_MATRIX.md` | Hardware matrix |
| `docs/05_UI_SPECIFICATION.md` | UI specification |
| `docs/06_RF_SAFETY_AUDIT.md` | RF safety audit |
| `docs/07_BUILD_AND_FLASH.md` | Build and flash |
| `docs/08_TEST_REPORT.md` | Test report |
| `docs/09_EXHIBITION_OPERATION.md` | Exhibition operation notes |
| `docs/10_KNOWN_ISSUES.md` | Known issues (numbered B…) |
| `docs/11_VOICE_SETUP.md` | Offline Japanese voice |
| `docs/12_VISION_SETUP.md` | Camera (stage 4a) |

---

## Ground rules

* Do not modify the upstream SDK / BSP, overwrite existing ESP-IDF directories, or change eFuses or security configuration.
* Explain the impact and get consent before any partition-table or data-affecting operation.
* No fabricated data; never conflate coded / compiled / simulator-verified / device-verified / RF-audited.

## License and third-party material

* Project source: Apache License 2.0 (`LICENSE`; source files carry `SPDX-License-Identifier: Apache-2.0`).
* Font: Noto Sans CJK JP, SIL Open Font License 1.1 (`LICENSES/NotoSansCJK-OFL.txt`); the generated glyph data `exp_font_data.c` derives from it.
* Upstream (not distributed here, each under its own licence): [esp-mosaico-vibe](https://github.com/esp-mosaico/esp-mosaico-vibe), [esp-mosaico-bsp](https://github.com/esp-mosaico/esp-mosaico-bsp), [esp-mosaico-utils](https://github.com/esp-mosaico/esp-mosaico-utils), [raylib-lite-engine](https://github.com/espressif2022/raylib-lite-engine), ESP-IDF, ESP-SR / ESP-DL. Pinned commits are in `versions.lock.json`.
* ESP-Mosaico, ESP32, ESP-IDF and related names belong to their respective owners; this project is not an official Espressif product.
