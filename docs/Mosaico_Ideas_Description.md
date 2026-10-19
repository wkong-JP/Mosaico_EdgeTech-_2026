## ESP-MOSAICO 旗舰全能体验 — EdgeTech+ 2026 统一体验固件

将 ESP-MOSAICO 转化为一台集成了离线语音、视觉手势追踪、全功能传感器融合与高帧率动画的掌上 AIoT 旗舰展示机。充分释放 **ESP32-S31** 双核 RISC-V 与 **16 MB Octal PSRAM（250 MHz）** 的极致硬件潜能，带来细腻丝滑的交互体验。100% 纯本地离线运行（严格经过 Route B 零无线射频审计，无 Wi-Fi / 蓝牙），开机即玩，安全可靠。

![ESP-MOSAICO Experience Cover](cover_mosaico_experience.webp)

---

### 五大体验界面

- **Home（首页导航）**：
  采用国际化英文排版与深色玻璃拟物风格，实时显示板载电池电压百分比、右侧磁吸拓展状态、动态呼吸氛围光圈；提供流畅的页面触摸切换与自动轮播（Attract Mode）演示。
- **Motion（姿态与磁力计融合）**：
  融合 BMI270 六轴姿态与双 BMM150 磁力计，实时驱动 3D 俯仰/横滚罗盘与航向角指针。支持触摸校准环境磁场基线，精准直观展现 MOSAICO 的高精度运动感知力。
- **Audio & AI（动态频谱与双语离线语音）**：
  利用 16 MB PSRAM 建立动态历史环形缓存，呈现灵动的 24 频段霓虹音频瀑布图；内置 ESP-SR 离线命令词引擎，支持中英日多语言智能声学响应。
- **Tilt Maze（重力迷宫与手势追踪）**：
  经典物理引擎滚球迷宫游戏。支持 **IMU 重力倾角**、**左侧拓展摇杆** 与 **SC101IOT 摄像头手势** 三重控制。在摄像头手势模式下，采用 30 FPS 连续指数平滑插值配合 PSRAM 轨迹缓存，随手掌移动划出绚丽的流光彗星尾迹。
- **Device Info（系统与硬件看板）**：
  直观以图表方式展现 **16 MB Octal PSRAM** 与 **16 MB SPI Flash** 的内存拓扑分布与实时负载，动态侦测左/右拓展槽子板 ID，体现乐鑫芯片的强劲系统承载力。
- **彩蛋：乐鑫官方 Logo 磁吸秒切**：
  当右侧强磁贴合乐鑫拓展套件时，硬件中断以 **17 ms** 极速挂起摄像头并全屏呈现乐鑫官方高清标识；移开磁吸后秒级无缝复原当前页面与所有任务。

---

### 操作方法与交互指南

- **触摸导航**：轻触底部导航栏（Home / Motion / Audio / Game / Device）快速切换。
- **离线语音唤醒与控制**：
  1. 对设备说出唤醒词：**"Hi ESP"**（或日语 **"こんにちは ESP"**）；
  2. 屏幕亮起监听状态后，说出英文目标指令：
     - 主页：`"Home"`, `"Go Home"`, `"Go Back Home"`
     - 姿态：`"Motion"`, `"Open Motion"`, `"Sensor"`
     - 音频：`"Audio"`, `"Open Audio"`, `"Voice"`
     - 游戏：`"Game"`, `"Open Game"`, `"Play Game"`, `"Maze"`
     - 设备：`"Device"`, `"System"`, `"Settings"`, `"Hardware"`, `"About"`
  3. 亦支持对应日语指令（`"ホーム"`, `"モーション"`, `"オーディオ"`, `"ゲーム"`, `"デバイス"`）。
- **手势重力迷宫**：
  在 Game 页面选择 **Camera** 模式并启动游戏，在镜头前（15~50cm）伸出手掌并移动，黄色光标与彗星光晕将随手势实时跟随，控制重力小球穿过迷宫直达终点。
- **磁吸互动**：将配备强磁的模块贴近机身右侧，屏幕秒切乐鑫官方标识。

---

### 硬件事实与运行特性

- **主控平台**：ESP-MOSAICO V1.2（ESP32-S31NRV16，双核 32 位 RISC-V @ 240 MHz）。
- **超大内存**：**16 MB Octal PSRAM @ 250 MHz**（空闲内存超 6 MB），**16 MB Quad SPI Flash**。
- **屏幕显示**：480 × 480 AMOLED 全贴合显示屏（CO5300），针对外框圆角深度优化文字与按钮排版，杜绝边缘截断。
- **传感器与多媒体**：BMI270 IMU、双 BMM150 磁力计、ES8311 音频编解码芯片、SC101IOT 高清摄像头子板、板载锂电池与精密电源管理。
- **RF 零辐射铁律**：100% 纯本地离线固件，禁用 Wi-Fi / 蓝牙 / ESP-NOW 等全部射频协议栈，通过全套固件 Map 与静态符号审计。

---

### 安装与烧录说明

本发布提供 **完整单文件 BIN 镜像（Offset 0x0）**，包含完整的 Bootloader、分区表、Factory App 与 ESP-SR 语音模型数据。

#### 快速烧录步骤：
1. 使用 USB Type-C 数据线将 ESP-MOSAICO 连接至电脑；
2. **按住板子上的 BOOT 键 $\to$ 按一下 RESET 键 $\to$ 松开 BOOT 键**，使设备进入 ROM 下载模式；
3. 使用 `esptool` 将完整镜像一次性写入 **0x0**：
   ```bash
   esptool --chip esp32s31 -b 460800 write-flash 0x0 esp32s31_mosaico_experience_full_0x0.bin
   ```
4. 烧录完成后按一下 RESET 键即可启动体验。

---

### 开源许可与致谢

- 本项目基于乐鑫官方 ESP-IDF 及 ESP-MOSAICO BSP 硬件体系开发。
- 遵循 Apache-2.0 开源许可协议。
- 语音唤醒与离线指令识别由乐鑫官方 **ESP-SR** 提供动力；手势检测由 **ESP-DL** 与 **ESPDet-Pico** 提供动力。

---

## ESP-MOSAICO Experience — EdgeTech+ 2026 Flagship Firmware

Transform your ESP-MOSAICO into an ultimate offline AIoT flagship exhibition console. This unified firmware fully unlocks the capabilities of the **ESP32-S31** dual-core RISC-V SoC and **16 MB Octal PSRAM @ 250 MHz**, delivering a responsive, multi-modal interaction experience featuring offline voice recognition, vision hand tracking, sensor fusion, and fluid UI animations.

100% offline and verified zero-RF emission (strict Route B audit: no Wi-Fi, Bluetooth, or ESP-NOW). Flash and experience immediately with zero cloud dependency.

---

### Five Core Experiences

- **Home**: Elegant dark-glass dashboard with battery percentage, magnetic dock status, and touch navigation.
- **Motion**: Dual-BMM150 magnetometer and BMI270 sensor fusion powering an interactive 3D attitude compass with touch baseline calibration.
- **Audio & AI**: 24-band dynamic audio spectrum waterfall backed by PSRAM circular buffers; dual-wake offline speech command system.
- **Tilt Maze**: Real-time physics ball maze controllable by IMU tilt, joystick, or camera hand gestures. Gesture mode features continuous 30 FPS interpolation and glowing comet trail.
- **Device Info**: Real-time architectural visualization highlighting the 16 MB PSRAM & Flash memory distribution, module slot detection, and hardware health.
- **Espressif Magnetic Showcase**: Right magnetic dock triggers an instantaneous (17 ms) transition to the official Espressif logo, smoothly returning to the previous screen when detached.

---

### Voice Commands ("Hi ESP")

Wake up the device by saying **"Hi ESP"** (or Japanese **"こんにちは ESP"**), then speak any of the English commands within the 5-second window:
- **Home**: `"Home"`, `"Go Home"`, `"Go Back Home"`
- **Motion**: `"Motion"`, `"Open Motion"`, `"Sensor"`
- **Audio**: `"Audio"`, `"Open Audio"`, `"Voice"`
- **Game**: `"Game"`, `"Open Game"`, `"Play Game"`, `"Maze"`
- **Device**: `"Device"`, `"System"`, `"Settings"`, `"Hardware"`, `"About"`

---

### Hardware & Runtime Specs

- **Chip**: ESP32-S31NRV16 (Dual-core 32-bit RISC-V @ 240 MHz).
- **Memory**: 16 MB Octal PSRAM @ 250 MHz (over 6 MB free headroom), 16 MB Quad SPI Flash.
- **Display**: 480 × 480 AMOLED (CO5300), tuned for rounded corner bezels with no visual clipping.
- **Sensors**: BMI270 IMU, dual BMM150 magnetometers, ES8311 audio codec, SC101IOT camera.
- **RF Rule**: 100% strictly offline (Zero-RF verified).

---

### Flashing Instructions

A unified **full partition image (Offset 0x0)** is provided, containing bootloader, partition table, application, and speech models.

```bash
# Enter ROM download mode: Hold BOOT -> Click RESET -> Release BOOT
esptool --chip esp32s31 -b 460800 write-flash 0x0 esp32s31_mosaico_experience_full_0x0.bin
```

---

### License

Licensed under the Apache License, Version 2.0. Offline AI algorithms powered by Espressif ESP-SR and ESP-DL.
