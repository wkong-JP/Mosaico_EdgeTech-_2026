# ESP-MOSAICO Experience — EdgeTech+ 2026 旗舰离线交互展会固件

[English](README.en.md) · **简体中文**

[![Target: ESP32-S31](https://img.shields.io/badge/Target-ESP32--S31-red.svg)](https://www.espressif.com/)
[![PSRAM: 16MB Octal @ 250MHz](https://img.shields.io/badge/Octal_PSRAM-16MB_%40_250MHz-blue.svg)](https://www.espressif.com/)
[![Screen: 480x480 AMOLED](https://img.shields.io/badge/Display-480x480_AMOLED-orange.svg)]()
[![RF: Zero-RF Route B](https://img.shields.io/badge/RF_Policy-Strictly_Offline_(Zero--RF)-green.svg)]()
[![Firmware: v0.9.4-stage4d](https://img.shields.io/badge/Firmware-v0.9.4--stage4d-brightgreen.svg)]()
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)

本固件专为 **2026 日本横滨 EdgeTech+ 展会**倾力打造，作为 **ESP-MOSAICO V1.2（搭载乐鑫新一代旗舰 SoC ESP32-S31）** 的官方旗舰离线多模态交互演示固件。

固件遵循严格的 **Route B 零无线射频（Zero-RF Fail-Closed）** 展会合规安全规范，在完全隔离无线协议栈的前提下，深度调优并极致榨干 ESP32-S31 强大的双核 RISC-V 架构与 **16 MB Octal PSRAM（250 MHz 极限频率）** 算力，集成了**离线双语语音控制（ESP-SR）**、**实时 AI 摄像头手势跟踪（ESPDet）**、**6 轴 IMU 与双磁力计 3D 姿态融合**、**高精度物理迷宫引擎**、**AMOLED 480×480 动效流光渲染** 以及 **M5Stack 磁吸套件秒切展示**，提供顺滑、惊艳且极具互动性的参展体验。

---

## 🌟 核心工程与技术亮点

- 🛡️ **严格零无线射频（Route B Fail-Closed Zero-RF）**  
  彻底剥离 Wi-Fi、BLE、ESP-NOW 及 802.15.4 协议栈。固件启动流程严禁无线初始化，静态链接符号与镜像图谱均通过全自动化 RF 安全审计（0 RF Findings），完全杜绝海外展会现场无线干扰与当地无线电法规认证合规风险。
- ⚡ **16 MB Octal PSRAM 极致承载与高吞吐**  
  将 16 MB PSRAM 划分为双重全彩高刷渲染缓冲、DVP 视频流 DMA 通道、ESPDet-Pico 深度学习网络模型与 24 频段音频频谱历史环形缓存；即便在 AI 视觉与语音并发跑满时，系统空闲堆内存依然保持在 **5.0 MB 以上**，展现出极强的抗重载弹性。
- 🎙️ **多模态离线双语语音操作（ESP-SR）**  
  内嵌 WakeNet 与 MultiNet7 离线语音算法，支持双唤醒词（日语 **「こんにちは ESP」** / 英语 **「Hi ESP」**）。注册并优化了 **58 条中英日离线控制指令**（包含页面跳转、快门拍照、迷宫交互等），采用专门的 G2P 音标补偿算法，非母语发音也能精准秒级响应。
- 👁️ **实时摄像头手势隔空追踪（ESPDet-Pico + PPA）**  
  驱动左侧扩展槽的 SC101IOT DVP 摄像头，由 ESP32-S31 片上 PPA 硬件引擎加速实现实时裁剪与旋转缩放（1280×720 UYVY → 480×480 RGB565），Core 1 独立运行异步轻量化手势检测网络，以 18 px 死区 + 1.6× 灵敏增益实现零延迟手势隔空操控。
- 🧲 **M5Stack 磁吸套件极速识别与无缝恢复**  
  复用板载双 BMM150 强磁饱和判决机制。当检测到右侧磁吸套件贴合时，系统硬件中断以 **17 ms 极速响应**全屏渲染 Flash 常量 M5Stack 纯白品牌 Logo，同时安全挂起摄像头以降低功耗；分离后系统无缝恢复原有页面与管线，**零动态堆分配、零重绘闪烁、不重启**。
- 🎮 **真实重力物理迷宫（Tilt Maze）**  
  内置完整碰撞检测与动量守恒物理引擎，支持三阶难度；提供 **重力倾斜（Tilt）**、**物理摇杆（Joystick）**、**隔空手势（Camera）** 三重操控方式，并在 16 MB PSRAM 历史轨迹插值中渲染绚丽的**彗星流光尾迹**与画中画（PiP）实时手势准星。
- 📱 **圆角 AMOLED 防裁切适配**  
  针对 480×480 全贴合圆形/大圆角 AMOLED（CO5300 控制器），对顶部状态栏、电池电量计、HUD 退出按键与底部 Tab 导航实施防裁切内缩保护，呈现视觉一致的精致曲面 UI。
- 🔌 **ESP-Iris USB CDC0 免冲突高速流式日志**  
  基于 ESP32-S31 高速 USB OTG UTMI PHY 建立专用调试通道，彻底移除屏幕上的黄色调试遮挡层（`EXP_TIMING_OVERLAY=0`），现场参展人员看到的是极简干净的 UI，工程师则可通过电脑免驱即插即用捕获零丢包的实时诊断日志。

---

## 📱 五大交互页面展示

```
[Home 首页] ──► [Motion 姿态] ──► [Audio 语音/音频] ──► [Game 迷宫] ──► [Device 看板]
     ▲                                                                           │
     └──────────────────────── (闲置 45 秒自动巡航 Attract Mode) ─────────────────┘
```

| 页面 | 核心功能与亮点 |
|---|---|
| **1. Home（首页看板）** | 实时显示系统运行状态、基于 80 mAh 实测放电曲线修正的高精度电池电量计、右侧磁吸扩展状态检测卡片；展台闲置 45 秒后自动开启 Attract Mode 巡展动画。 |
| **2. Motion（姿态与罗盘）** | 实时读取 BMI270 六轴传感器并计算 3D 俯仰角与翻滚角；双 BMM150 磁力计融合罗盘实时指向地磁北极；轻触罗盘卡片即可一键执行动态基线去干扰校准。 |
| **3. Audio & AI（语音与流光）** | ES8311 音频编解码器采集拾音，PSRAM 环形缓冲区实时渲染 **24 频段霓虹音频流光瀑布图**；底部实时显示离线语音监听状态与识别到的中英日指令。 |
| **4. Game（物理迷宫 Tilt Maze）** | 支持 Easy / Normal / Hard 三级难度，可在重力倾斜、左槽摇杆、隔空手势三种模式间自由切换；独创手势光晕彗星尾迹与 HUD 画中画实时取景。 |
| **5. Device Info（硬件拓扑看板）** | 直观展示 16 MB Octal PSRAM 和 16 MB Flash 的物理分区图谱与剩余内存；实时自动探测左右扩展槽（Camera / Joystick / Sensor 等）的插接状态与 EEPROM 描述符。 |
| **彩蛋：M5Stack Logo 模式** | 右侧磁吸贴合即刻触发全屏纯白 M5Stack 品牌 Logo，分离后秒级恢复，展示模块化硬件极速扩展能力。 |

---

## 🎙️ 离线语音指令清单（ESP-SR）

固件内嵌双语离线语音模型，已完成音标补偿与高噪声容错训练：

* **唤醒词**：
  * **日语**：「こんにちは ESP」（Konnichiwa ESP）
  * **英语**：「Hi ESP」
* **可用离线指令列表（共 58 条登记词汇）**：

| 指令类别 | 日语命令词（音译英文登记） | 英语命令词 | 功能触发 |
|---|---|---|---|
| **页面导航** | ホーム / 最初のページ | Home / Main menu | 跳转至 Home 页面 |
| | モーション / コンパス | Motion / Compass | 跳转至 Motion 姿态页面 |
| | オーディオ / 音声 | Audio / Music | 跳转至 Audio 频谱与语音页面 |
| | ゲーム / 迷路 | Game / Maze | 跳转至 Game 迷宫页面 |
| | デバイス / システム | Device / Info | 跳转至 Device 设备信息页面 |
| **迷宫交互** | 右へ / みぎ | Turn Right | 迷宫物理力向右偏置 |
| | 左へ / ひだり | Turn Left | 迷宫物理力向左偏置 |
| | 上へ / うえ | Move Up | 迷宫物理力向上偏置 |
| | 下へ / した | Move Down | 迷宫物理力向下偏置 |
| | スタート / 始め | Start | 开始游戏计时 |
| | リセット / やり直し | Reset | 重置迷宫滚球位置 |
| **快捷操作** | 写真を撮って / チーズ | Take Photo | 摄像头快门拍照模拟 |
| | 戻る / 前のページ | Go Back | 返回上一级或取消操作 |

---

## 🛠️ 硬件与系统架构

### 硬件选型规格

| 组件 | 型号 / 规格 | 说明 |
|---|---|---|
| **主控芯片** | **ESP32-S31** (Dual-core RISC-V @ 240 MHz) | 乐鑫新一代旗舰多媒体与边缘 AI SoC |
| **片外内存 (PSRAM)** | **16 MB Octal PSRAM** (250 MHz DDR) | 高速大容量缓存，支持 AI 推理与视频流 |
| **片外存储 (Flash)** | **16 MB Quad SPI Flash** | 存放 Bootloader、App、68KB Logo 资产及 4MB 语音模型 |
| **AMOLED 屏幕** | 1.43 英寸 480 × 480 CO5300 控制芯片 | 全贴合高清 AMOLED，色彩鲜艳，高刷低功耗 |
| **IMU 传感器** | Bosch BMI270 (6 轴加速度计 + 陀螺仪) | 提供精准的 3D 姿态解算与重力感知 |
| **地磁传感器** | Dual Bosch BMM150 (双磁力计阵列) | 双路地磁校验，同时承担罗盘指向与强磁吸附检测 |
| **摄像头模块** | SC101IOT (DVP 接口，1280×720 UYVY) | 左侧扩展槽模块，由硬件 PPA 加速处理 |
| **音频编解码** | ES8311 Codec + 模拟硅麦 + 板载喇叭 | 采集高质量麦克风信号供 ESP-SR 识别 |
| **触控芯片** | Hynitron CST9220 | 全面屏多点电容触控 |
| **USB 调试** | USB OTG High-Speed UTMI PHY (ESP-Iris) | 硬件 USB CDC0 高速无损实时日志传输 |

### 软件架构分层

```
┌─────────────────────────────────────────────────────────────┐
│                    用户交互层 (480x480 UI)                   │
│   Home  │  Motion  │  Audio & AI  │  Tilt Maze  │  Device   │
├─────────────────────────────────────────────────────────────┤
│                 核心应用与算法层 (Portable C)                │
│  • 物理滚球引擎 (app_maze)         • 24频段音频DSP (app_audio)  │
│  • 姿态解算滤波 (app_motion)       • 彗星插值动效 (exp_comet)   │
├──────────────────────────────┬──────────────────────────────┤
│       AI 视觉引擎 (ESP-DL)    │      AI 语音引擎 (ESP-SR)     │
│  • SC101IOT DVP 驱动 (2缓冲)  │  • AFE 声学前端去噪           │
│  • PPA 硬件旋转/缩放/对齐      │  • WakeNet9 离线双语唤醒      │
│  • ESPDet-Pico 异步手部检测    │  • MultiNet7 58条指令识别     │
├──────────────────────────────┴──────────────────────────────┤
│                底层驱动与板级支持包 (Route B)                │
│  • 双BMM150强磁检测状态机         • CST9220 触控中断驱动     │
│  • CO5300 AMOLED 硬件刷屏        • BQ27220 / ADC 电池建模   │
│  • ESP-Iris USB CDC0 调试通道     • Fail-Closed 零无线射频   │
└─────────────────────────────────────────────────────────────┘
```

---

## ⚡ 关键稳定性保障（Senior Dev 修复记录）

1. **DVP DMA 队列饿死修复（Zero-Starvation DMA）**  
   * **问题**：旧版本在摄像头采图后，等待人脸/手势 AI 检测算法（耗时 200~300ms）全部完毕才归还缓冲区，导致 DVP 驱动 DMA 队列耗尽触发 ISR 断言 Panic。  
   * **根治**：在 `dev_camera.c` 中，PPA 硬件搬运完成后（仅耗时 3ms）**立即调用 `mosaico_camera_return_frame` 归还缓冲**。实测经历 1200+ 帧高强度采样，连续数小时保持 `0 capture errors, 0 restarts`。
2. **PSRAM 显存科学分级与配比**  
   * 将 DVP 接收缓冲区严格限定为 2 帧（$2 \times 1.84\text{ MB} = 3.68\text{ MB}$），为系统其余模块保留 >5.0 MB 的空闲 PSRAM，彻底消除 `ENOMEM` (errno 12) 错误。
3. **电池供电 Crash-Guard 自动复位**  
   * **问题**：MOSAICO 主板带有内置锂电池（约 4.19V），`__NOINIT_ATTR` 区域在拔掉 USB 或软件重启时不会掉电清零。若外部模块（如早期摄像头）偶发异常，语音模块的防崩溃守卫会长期锁定处于 `SKIPPED` 状态，屏幕报错「音声操作 エラー」。  
   * **根治**：语音就绪后立即解除守卫，并在每次开机时主动清理残留的错误 strike，保证即使断开 USB 后重新启动也能稳定正常加载。

---

## 🚀 固件安装与烧录

本仓库在 `release/` 目录中预编译了**单文件完整系统镜像（Offset 0x0）**，包含引导程序（Bootloader）、分区表、主程序及离线语音大模型。

### 镜像信息

* **文件路径**：`release/esp32s31_mosaico_experience_full_0x0.bin`
* **烧录起始地址**：`0x0`
* **大小**：约 9.25 MB (9,701,827 bytes)
* **SHA-256**：`2f4bb03080c6ac8268f9fa8cc6acfeedeecf004451da039f598f875a982e17b3`

### 一键烧录操作步骤

1. **进入 ROM 下载模式**：
   * 用 USB 数据线将开发板连接至电脑；
   * 按住板载 **BOOT 键** $\rightarrow$ 按一下 **RESET 键** $\rightarrow$ 松开 **BOOT 键**。
2. **执行烧录命令**（以 921600 高速波特率写入，约 15 秒完成）：

```bash
# 自动检测端口并烧录（或显式指定 -p /dev/cu.usbmodem*）
esptool --chip esp32s31 -b 921600 write-flash 0x0 release/esp32s31_mosaico_experience_full_0x0.bin
```

3. 烧录完成后，按一下板载 **RESET 键** 即可进入全新体验。

### 源码编译（供二次开发）

```bash
# 激活配套 ESP-IDF 环境（建议基于 release/v6.1 或 v6.2-dev）
. ~/esp/esp-idf/export.sh

cd mosaico-experience/firmware
idf.py build

# 烧录到目标设备
idf.py -p /dev/cu.usbmodem11401 flash

# 启动专用实时日志监控工具（零丢包、免驱动）
python3 ../scripts/iris_log.py -p /dev/cu.usbmodem11401
```

---

## 📂 项目工程目录概览

```
mosaico-experience/
├── app/mosaico_experience/        # 跨平台可移植 C 核心应用逻辑
│   ├── main/                      # 核心物理引擎、DSP、UI 状态机、字形数据
│   ├── host/                      # PC 桌面端模拟器（raylib-lite 渲染）
│   ├── tests/                     # 单元测试与 19 项行为自动化回归套件
│   └── tools/                     # 字体子集生成、字形生成与切图工具
├── firmware/                      # ESP32-S31 原生 ESP-IDF 工程（Route B）
│   ├── main/                      # 设备驱动（BMI270, BMM150, CST9220, 磁吸检测）
│   ├── components/                # 屏幕渲染（exp_surface）与调试工具
│   ├── voice_components/          # 离线语音模块（exp_voice，集成 ESP-SR）
│   ├── vision_components/         # 摄像头手势识别模块（exp_vision，集成 ESP-DL）
│   └── partitions_voice.csv       # 16MB 完整分区规划表
├── docs/                          # 01~12 专项工程与审计设计文档
├── scripts/                       # 自动化脚本库（RF 静态审计、免按键下载烧录、日志工具）
├── release/                       # 官方发布二进制单镜像与视觉展示物料
└── evidence/                      # 自动化测试报告、UI 像素回归对比与 RF 审计日志
```

---

## 📄 开源许可与致谢

* **本项目源码**：遵循 [Apache License 2.0](LICENSE) 协议发布。
* **字体字形**：基于 Noto Sans CJK JP 衍生并嵌入，遵循 [SIL Open Font License 1.1](LICENSES/NotoSansCJK-OFL.txt)。
* **致谢与声明**：
  * 本项目深度依赖 [ESP-IDF](https://github.com/espressif/esp-idf)、[ESP-SR](https://github.com/espressif/esp-sr)、[ESP-DL](https://github.com/espressif/esp-dl) 等乐鑫开源生态组件。
  * `ESP32`、`ESP-MOSAICO`、`ESP-IDF` 等商标归乐鑫信息科技（Espressif Systems）所有。
  * `M5Stack` 商标归深圳市明栈信息科技有限公司所有。
