# ESP-Mosaico Experience – Offline Interactive Showcase

[English](README.en.md) · **简体中文**

面向 **ESP32‑S31 / ESP‑Mosaico V1.2** 的**完全离线**展会演示（日语 480×480 UI）。设备上不使用 Wi‑Fi、蓝牙、ESP‑NOW、IEEE 802.15.4 等任何无线功能。

演示由三部分组成：

| | 内容 |
|---|---|
| **A · モーション / HMI** | IMU 姿态（3D 模型）、罗盘、电量 |
| **B · オーディオ・AI** | 麦克风电平/频谱、离线日语语音操作（ESP‑SR） |
| **C · ゲーム** | 「傾き迷路」（Tilt Maze）：倾斜、触摸或摇杆控制 |

页面：Home · Motion · AI & Audio · Game · Device。

---

## ⚠️ 状态（请先读这一节）

* **RF（无线）状态：RF Verification Pending。** 没有任何 RF 实测，也没有日本法规结论。软件审计**不等于** RF 测量。
* **不是 "Exhibition Ready"。** 只有在 RF 实测 + 日本法规确认 + 稳定性测试全部完成后才会使用这个称呼。
* 本仓库里的"已验证"都带范围说明；**没有测过的项一律写"未执行 / 未验证"**，没有任何伪造的传感器、AI 或性能数据。
* 当前固件大量内容只在一块开发板上跑过，且这块板上原有的官方 Recovery 已被擦除（用户决定，无备份，见 `docs/07`、`docs/10` B19）。

状态标签的含义（文档里不混用）：**已计划 → 已编码 → 已编译 → 模拟器已验证 → 设备已验证 → RF 已审计（静态）**。

### 阶段进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| 0 | 环境 / 官方仓库 / RF 审计 | 完成 |
| 1 | 架构 | 完成 |
| 2 | Host 交互式 UI 预览 | 完成，**模拟器已验证**（合成传感器数据） |
| 3a | 真机固件：显示 + 触摸 + UI（路线 B） | **设备已验证**（范围很窄：显示、触摸有响应） |
| 性能 | 帧耗时 | 约 35 ms/帧（≈28–29 fps），目标 33 ms 未达到；瓶颈见 `docs/08` §7 |
| 3b‑1 | IMU + 电量计 + 屏幕日志 | **设备已验证**（0.4.3） |
| 3b‑2 | 磁力计 / 罗盘 | 已编码 + 模拟器测试；设备读数仍偏差大，原因未定（`docs/10` B38） |
| 3c | 麦克风、扬声器、马达、扩展槽探测、运行时 RF 探针 | 已编码 + 主机测试；设备验证**未执行**（`docs/10` B39） |
| 3d | 摇杆、烧屏调光、看门狗、磁场干扰判断、展台发布配置 | 已编码 + 主机测试；设备验证**未执行**（`docs/10` B40） |
| 3e | 日语离线语音操作（ESP‑SR） | **设备已验证**（单唤醒词 + 基础指令正常响应，`docs/11`） |
| 4a | 摄像头预览与帧时间测量（`-DEXP_VISION=1`） | **设备已验证**（15.6 fps，SC101IOT UYVY，`docs/12`） |
| 4b | ESPDet 人脸检测基准与实时框 | **设备已验证**（S31 推理 46.6 ms / 21.4 fps，`docs/12`） |
| 4c | Tilt Maze 手势跟踪控制、PiP 与 Iris USB CDC0 日志 | **设备已验证**（方案 A 高增益手势控制跟手灵敏，`docs/12`） |
| 合并 | 语音控制 + 视觉手势跟踪同构固件（6 MB App 分区） | **设备已验证**（ESP-SR + ESPDet 协同运行，屏幕无遮挡黄色日志，Iris USB CDC0 实时传输） |
| 4d | 左侧磁吸 M5Stack Logo 模式（`feature/magnetic-logo-left`） | **设备已验证**（右侧贴合秒切 M5Stack Logo，分离秒复原，零 RF，零堆分配） |
| 4–5 | 全面稳定性、长时间运行、RF 实测、展会运营 | 进行中 / 部分待实测 |

---

## 目录

```
app/mosaico_experience/
  main/    可移植 C 核心：姿态融合、音频 DSP、迷宫、页面、文字/图形层、字形数据
  host/    Host 模拟器适配（合成传感器、引擎模块）——不会链接进固件
  tools/   字体生成、Host 运行包装、截图/布局检查、本地预览服务器
  tests/   原生单元测试、行为回归（19 项）、绘制 shim 与官方引擎逐像素对比
firmware/  路线 B：独立 ESP-IDF 工程
  main/                device_main.c 与设备驱动（传感器、音频、扩展槽、摇杆、看门狗……）
  components/          exp_surface（绘制层）、exp_dbglog（屏幕日志）
  voice_components/    exp_voice（ESP-SR 日语语音，仅 -DEXP_VOICE=1）
  partitions_voice.csv 语音构建的分区表
docs/      01–12 文档（简体中文）
scripts/   run_all_tests.sh · run_preview.sh · rf_check.py · rf_map_check.py · check_build.sh · release_check.py
           mac_env_audit.sh（只读）· bootstrap_workspace.sh（默认 dry-run）· voice_flash.sh
evidence/  测试日志、截图（22 屏）、布局报告、RF 静态扫描结果
LICENSES/  字体许可证（Noto Sans CJK, SIL OFL 1.1）
versions.lock.json  上游提交与已知版本约束
```

上游 SDK / BSP / 引擎**不在**本仓库内（`workspace/` 被忽略），由 `scripts/bootstrap_workspace.sh` 按 `versions.lock.json` 里固定的提交克隆；本项目从不修改它们。

### 架构一句话

可移植 C 核心（`app/mosaico_experience/main`）同时被两条路线使用：**Host 模拟器**（官方 raylib‑lite‑engine，用于预览与回归）和**真机固件**（路线 B：自己的 ESP‑IDF 工程 + 官方 BSP，不使用 Vibe Mode）。设备专有逻辑放在 `firmware/`，其中可单独测试的纯逻辑（`dev_*_core.h`）有主机单元测试。详见 `docs/03`。

---

## 快速开始：Host 预览（不需要硬件）

依赖：Python ≥ 3.10、Pillow、C 编译器（gcc 或 clang）、git。

```bash
./scripts/mac_env_audit.sh                  # 只读：看这台 Mac 缺什么
./scripts/bootstrap_workspace.sh            # dry-run：显示将克隆的固定提交
./scripts/bootstrap_workspace.sh --apply    # 克隆官方 esp-mosaico-vibe（含子模块）到 ./workspace
export MOSAICO_ENGINE_ROOT=<…>/esp-mosaico-vibe/submodule/raylib-lite-engine   # bootstrap 之后通常自动找到
./scripts/run_all_tests.sh                  # 全部 Host 检查（编译、单元测试、行为回归、截图、RF 静态扫描）
./scripts/run_preview.sh                    # 本机浏览器交互预览（127.0.0.1）
```

预览窗口右上角的 `SIM` 标签表示传感器数据**全部是合成的**。

---

## 固件构建与烧录（摘要，详见 `docs/07`、`docs/11`）

要求：ESP‑IDF 固定在提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`（v6.2‑dev），目标 `esp32s31`。`git -C <idf> rev-parse HEAD` 核对后再用；请为本项目单独克隆一份，**不要**切换或改动已有的其它 ESP‑IDF 目录（`docs/01`、`docs/07`）。

```bash
. ~/esp/esp-idf-mosaico-7b9cc1a/export.sh
cd firmware
idf.py build                                              # 普通开发固件
idf.py -B build_voice -DEXP_VOICE=1 build                 # 语音固件（独立目录、独立 sdkconfig）
python3 ../scripts/rf_map_check.py build/mosaico_experience.map sdkconfig   # 链接内容的 RF 审计（每次构建后都要做）
```

* 展台镜像：加 `-DEXP_RELEASE=1`，版本串以 `-rel` 结尾，再跑 `scripts/release_check.py`。开发镜像带调试叠加层和屏幕日志，**不能上展台**。
* **烧录前必须**：进入下载模式（按住 Boot，按 Reset，松开 Boot）、**写死端口并核对芯片/MAC**。开发机上同时插着别的项目的 ESP32‑S3 板，写错端口会破坏另一个项目。
* **绝不整片擦除**（除非用户明确要求）；不动 eFuse / 安全配置 / bootloader（语音首次烧录那次例外，见 `docs/11` §4.1）。
* 语音固件需要自定义分区表（`partitions_voice.csv`：nvs / phy_init / factory 5 MB / model 4 MB / coredump 512 KB）。烧录脚本 `scripts/voice_flash.sh` 分 `check`（只读）→ `write`/`update`/`coredump`，写入需要 `VOICE_FLASH_CONFIRM=yes`。
* 板子带电池：拔 USB **不等于**断电，要按 Reset。应用运行时没有串口（`docs/10` B21），日志看屏幕日志（三连点屏幕角落）。

---

## 访客体验循环（模拟器已验证）

| 条件 | 行为 |
|---|---|
| 任意页面无操作 45 秒 | 返回首页 |
| 首页再停留 30 秒 | 进入自动演示（attract）巡回：动作 → 音频 → 游戏演示 → 首页 → 循环 |
| 演示中任意触摸 | 立即结束演示回首页（这次触摸不触发按钮） |
| 演示中的声音 / 震动 | 一律静音、不震动 |
| 游戏进行中 | 不因闲置被强制返回首页 |

展台人员须知见 `docs/09`。**在板上验证之前，不要向观众宣称设备能听懂日语，也不要宣称罗盘准确，更不要宣称"无线已关闭 / 已合规"。**

---

## RF 政策（最高优先级）

展示固件、启动流程及扩展设备**不得启用、请求或执行任何无线功能**（Wi‑Fi、蓝牙、ESP‑NOW、IEEE 802.15.4 及其他），要求失败即关闭（fail‑closed）。

* 已有：源码静态扫描（`scripts/rf_check.py`）、构建链接图审计（`scripts/rf_map_check.py`）——都是**静态**检查，范围见 `docs/06`。
* 已知事实：Wi‑Fi / 共存符号由 SoC 推导，**不能靠配置关掉**；PHY 驱动被链接进镜像，运行时是否会被启用**未验证**（`docs/06` §7）。因此配置本身不是证据。
* 已知冲突：官方 Vibe Mode 固件启动即启用 Wi‑Fi。展位上**禁止**进入 Vibe Mode（AI 键 GPIO7）。
* 不照搬官方示例里的 ESP‑Iris（USB + TCP）传输，不照搬 Full Demo 的 Wi‑Fi / 蓝牙 / A2DP 配置。
* 未完成：RF 实测、日本法规确认。

---

## 测试与证据

`./scripts/run_all_tests.sh` 的最后一次完整运行输出 `ALL HOST CHECKS PASSED (simulator only: no device, no RF measurement)`。

涵盖：字体数据可复现性、gcc/clang `-Werror` 编译与原生单元测试（ASan/UBSan）、19 项行为回归、绘制 shim 与官方引擎逐像素对比、22 屏截图与布局检查、RF 静态扫描。原始日志、截图、报告都在 `evidence/`，说明在 `docs/08`。

**这些都是模拟器/主机结果，不代表设备行为。** 设备端 C 代码在云端环境只能用桩头文件做语法检查，真实编译在带 ESP‑IDF 的 Mac 上进行。

---

## 视觉与语音同构固件（阶段 4a–4c，开发分支 `feature/vision`）
 
在当前分支上实现了**离线日语语音控制（ESP-SR）+ 摄像头手势跟踪（ESPDet）同构固件**（`-DEXP_VOICE=1 -DEXP_VISION=1`）：
* **分区表结构**：`factory` 分区扩充至 6.0 MB（`0x600000`），`model` 语音模型分区设于 `0x610000`（4.0 MB），`coredump` 设于 `0xa10000`（512 KB），总占用 10.56 MB / 16 MB Flash（余量 5.44 MB）。
* **视觉流水线**：官方 BSP 摄像头模块（左槽，SC101IOT，DVP）→ PPA 硬件变换（720×720→480×480 RGB565）→ Core 1 异步手部检测（`hand_detect` ESPDet-Pico 224×224）。
* **迷宫手势控制**：Tilt Maze「カメラ」模式，采用方案 A 高增益映射（18 px 死区 + 1.6× 增益），微动灵敏跟手，菜单与 HUD 画中画（PiP）实时反馈手势姿态与跟踪框。
* **干净显示与免冲突日志**：完全移除屏幕顶部的黄色调试叠层（`EXP_TIMING_OVERLAY=0`），利用 ESP32-S31 高速 USB OTG UTMI PHY 建立 ESP-Iris USB CDC0 日志通道（`scripts/iris_log.py`），实时诊断且不影响屏幕展示。
* **DVP 内存优化**：在 `esp_video_dvp_device.c` 中显式指定 `.bk_buffer_dis = true`，省去驱动冗余的 1.84 MB PSRAM backup buffer，彻底解决高并发下的 DVP 分配阻塞。

回退到纯语音版本：`git checkout stable-stage3e-0.8.1`。

---

## 左侧磁吸 M5Stack Logo 模式（阶段 4d，开发分支 `feature/magnetic-logo-left`）

在现有语音与视觉同构固件基础上，增加了**右侧磁吸贴合 M5Stack Logo 显示与分离平滑恢复功能**：
* **图像资源**：通过 `scripts/gen_m5stack_logo.py` 将 `M5stack.png` 缩放生成 420×84 RGB565 数组（`firmware/main/assets/m5stack_logo.c/.h`），存放于 Flash `.rodata`（约 68.9 KB），480×480 AMOLED 全屏纯白居中无拉伸，**0 动态堆内存分配**。
* **双 BMM150 强磁检测**：直接复用 `dev_sensors.c` 现有的 100 ms 轮询，不创建多余任务；区分强磁饱和（`-32768` 或 `X >= 30000 µT`）与正常地磁读数，在不干扰罗盘地磁滤波的同时实现极速贴合判决。
* **状态机去抖**：`NORMAL -> ATTACH_PENDING (2 帧确认) -> LOGO_ACTIVE -> DETACH_PENDING (3 帧确认)`，实测贴合到呈现延迟 P95 ≤ 500 ms。
* **最高优先级渲染与无缝恢复**：Logo 模式下全屏刷白并覆盖 Logo，挂起 Camera 线程释放 PSRAM 带宽，屏蔽触摸与语音；分离时清空累积时间（`time_debt_s = 0`）平滑恢复原页面，无重绘闪烁、无补帧快进、不重启。
* **零 RF 维持**：全过程完全禁止任何无线网络栈，静态审计与链接符号审计均为 0 findings。

---

## 文档索引

| 文档 | 内容 |
|---|---|
| `docs/01_ENVIRONMENT_AUDIT.md` | 环境审计 |
| `docs/02_OFFICIAL_REPOSITORY_REVIEW.md` | 官方仓库评审 |
| `docs/03_DEMO_ARCHITECTURE.md` | 演示架构 |
| `docs/04_HARDWARE_MATRIX.md` | 硬件矩阵 |
| `docs/05_UI_SPECIFICATION.md` | UI 规格 |
| `docs/06_RF_SAFETY_AUDIT.md` | RF 安全审计 |
| `docs/07_BUILD_AND_FLASH.md` | 构建与烧录 |
| `docs/08_TEST_REPORT.md` | 测试报告 |
| `docs/09_EXHIBITION_OPERATION.md` | 展会运营说明 |
| `docs/10_KNOWN_ISSUES.md` | 已知问题（B 编号） |
| `docs/11_VOICE_SETUP.md` | 日语离线语音 |
| `docs/12_VISION_SETUP.md` | 摄像头（阶段 4a） |

文档为简体中文；UI 文案为日语。

---

## 约束

* 不修改上游 SDK / BSP，不覆盖现有 ESP‑IDF 目录，不改 eFuse 与安全配置。
* 涉及分区表或数据的操作，先说明影响并取得用户同意。
* 不伪造数据；不混淆"已编码 / 已编译 / 模拟器已验证 / 设备已验证 / RF 已审计"。

## 许可证与第三方

* 本项目源码：Apache License 2.0（`LICENSE`；源文件头为 `SPDX-License-Identifier: Apache-2.0`）。
* 字体：Noto Sans CJK JP，SIL Open Font License 1.1（`LICENSES/NotoSansCJK-OFL.txt`）；生成的字形数据 `exp_font_data.c` 由它派生。
* 上游（不随本仓库分发，各自有许可证）：[esp-mosaico-vibe](https://github.com/esp-mosaico/esp-mosaico-vibe)、[esp-mosaico-bsp](https://github.com/esp-mosaico/esp-mosaico-bsp)、[esp-mosaico-utils](https://github.com/esp-mosaico/esp-mosaico-utils)、[raylib-lite-engine](https://github.com/espressif2022/raylib-lite-engine)、ESP‑IDF、ESP‑SR / ESP‑DL。固定提交见 `versions.lock.json`。
* ESP‑Mosaico、ESP32、ESP‑IDF 等名称归其各自权利人所有；本项目不是 Espressif 的官方产品。
