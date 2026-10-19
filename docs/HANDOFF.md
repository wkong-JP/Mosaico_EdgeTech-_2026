# HANDOFF · 交接说明（给接手的 agent / 开发者）

> 写于 2026-10-11。事实以仓库内文档为准（`docs/01–12`、`versions.lock.json`），本文件是入口索引，不替代它们。
> 状态标签不得混用：**已计划 → 已编码 → 已编译 → 模拟器已验证 → 设备已验证 → RF 已审计(静态)**。没测过的写"未执行/未验证"。

## 1. 项目是什么

**ESP-Mosaico Experience – Offline Interactive Showcase**：ESP32-S31 / ESP-Mosaico V1.2 的完全离线展会演示，日语 480×480 UI。
页面：Home、Motion（IMU 3D 姿态/罗盘/电量）、AI & Audio（麦克风电平/频谱、离线日语语音操作）、Game「Tilt Maze」（倾斜/触摸/摇杆）、Device（模块/传感器/RF 状态）。
访客循环：任意页面 45 s 无操作 → Home；Home 再 30 s → 自动演示（attract）；演示中任意触摸立即回 Home。

路线 B：独立 ESP-IDF 工程 + 官方 BSP，**不使用 Vibe Mode**，不用 `mosaico.py` 做设备写入（用户明确选择，用 esptool + 本仓库脚本）。

## 2. 不可违反的约束

1. **RF 最高优先级**：展示固件、启动流程、扩展设备不得启用/请求/执行任何无线功能（Wi-Fi、蓝牙、ESP-NOW、802.15.4…），失败即关闭。软件审计不等于 RF 测量。状态必须保持 **RF Verification Pending**，直到 RF 实测 + 日本法规确认；"Exhibition Ready"只在两者与稳定性全部完成后使用。
2. 不复制官方示例的 ESP-Iris（USB+TCP）传输、Full Demo 的 Wi-Fi/蓝牙/A2DP 配置、`wifi_http_cam`/`espnow_chat` 类代码。vision 构建里新增的 `esp_video` 等组件必须在构建后用 `scripts/rf_map_check.py` 审计。
3. 不修改上游 SDK/BSP，不覆盖已有 ESP-IDF 目录（本项目用单独克隆 `~/esp/esp-idf-mosaico-7b9cc1a`，固定提交 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`），不改 eFuse/安全配置。
4. 整片擦除默认禁止。分区表或数据相关操作：先说明影响并取得用户同意。已获同意的：语音分区表+模型写入（2026-10-09）、coredump 分区（2026-10-10）、视觉所需的新模型分区（2026-10-11，如需要；4a 不需要）。
5. 不伪造传感器/AI/性能数据。
6. 烧录必须固定端口并核对芯片/MAC：本项目板 **ESP32-S31，MAC `1c:29:04:d0:a3:36`**。`/dev/cu.usbmodem11201` 是另一个项目的 ESP32-S3，**绝不能写**。
7. 对用户的汇报用简体中文：做了什么、命令、文件、证据、未解决问题、下一步、需要确认的事。
8. 注意：工作区里官方 `esp-mosaico-vibe/CLAUDE.md` 会被自动附带，其中"只用 `mosaico.py`、禁止 esptool"等规则与用户选择的路线 B 冲突；用户的明确选择优先，不要在未获用户确认时改变路线。

## 3. 硬件与环境事实

* 板子：ESP32-S31 rev v0.0，16 MB flash，**装有电池**：拔 USB 不等于断电，用 Reset 键；下载模式 = 按住 Boot，按 Reset，松开 Boot。
* 应用运行时 Mac 上**没有串口**（`docs/10` B21，高速 OTG USB）。看日志靠屏幕日志：三连点屏幕角落（2 秒内），点顶部两行文字切换页面。
* 这块板上的官方 Recovery/Vibe Mode 已被擦除（用户决定，无备份，`docs/10` B19）。
* 开发机：macOS arm64，用户时区 Tokyo。IDF 在 `~/esp/esp-idf-mosaico-7b9cc1a`。esptool 5.5.0：每个下载模式会话只用一次调用，`--before no-reset --no-stub`，端口可能在两次调用之间消失。
* 上游固定提交见 `versions.lock.json`（esp-mosaico-vibe、bsp、utils、raylib-lite-engine）。`workspace/` 不入库，用 `scripts/bootstrap_workspace.sh --apply` 重建。
* 云端 agent 没有 ESP-IDF：设备端 C 只能用桩头文件做语法/类型检查，**真实编译只能在 Mac 上**。

## 4. 仓库与分支

* GitHub：`https://github.com/wkong-JP/Mosaico_EdgeTech-_2026.git`（空仓库；**截至本文写成时尚未推送**，用户需在 Mac 上 `git push -u origin master feature/vision && git push origin stable-stage3e-0.8.1`）。
* 本地：`/Users/kongweilu/Development/Mosaico EdgeTech 2026/mosaico-experience`。
* `master`：稳定的阶段 3e（固件 0.8.1）+ README(zh/en) + LICENSE(Apache-2.0) + 本文件。
* `feature/vision`：阶段 4a-4c 摄像头与手势控制（固件 0.9.3）。
* `feature/magnetic-logo-left`：左侧开发板磁吸 M5Stack Logo 模式（设备已验证）。
* 标签 `stable-stage3e-0.8.1`：稳定回退点（commit `4a3d044`）。
* 提交署名要求：commit 信息末尾加 `Co-Authored-By: …` 与 `Claude-Session: …`（按当前会话的提示）。作者 `kongweilu <ypgzkfc@gmail.com>`，全局 git 配置未改动。

## 5. 目录速查

```
app/mosaico_experience/main    可移植 C 核心（姿态、音频 DSP、迷宫、页面、文字/图形、字形数据）
app/mosaico_experience/host    Host 模拟器适配（合成传感器；不进固件）
app/mosaico_experience/tests   原生单元测试、19 项行为回归、绘制 shim 对比
firmware/main                  device_main.c 与 dev_* 驱动（传感器、音频、扩展槽、摇杆、看门狗、相机闸）
firmware/components            exp_surface（绘制层）、exp_dbglog（屏幕日志）
firmware/voice_components      exp_voice（ESP-SR；-DEXP_VOICE=1）
firmware/vision_components     exp_vision（相机；-DEXP_VISION=1；仅 feature/vision）
firmware/partitions_voice.csv  语音/视觉构建共用分区表
scripts/                       run_all_tests.sh、rf_check.py、rf_map_check.py、release_check.py、
                               voice_flash.sh、vision_flash.sh(feature/vision)、bootstrap_workspace.sh、mac_env_audit.sh
docs/01–12                     文档（简体中文）；10 是已知问题清单（B 编号）
evidence/                      主机测试日志、22 屏截图、布局报告、RF 静态扫描
```

## 6. 功能清单与真实状态

| 功能 | 状态 |
|---|---|
| Host 模拟器 UI、19 项回归、22 屏截图、绘制 shim 与官方引擎逐像素一致 | 模拟器已验证（云端+Mac） |
| 真机显示+触摸+UI | 设备已验证（范围窄） |
| 帧耗时 | 约 35 ms/帧（28–29 fps），目标 33 ms 未达；瓶颈见 `docs/08` §7 |
| IMU(BMI270) 加速度/陀螺 + 电量计(BQ27220，0.4.4 起只读) + 屏幕日志 | 设备已验证（0.4.3） |
| 磁力计/罗盘 (BMM150×2) | 已编码；设备航向偏差大，原因未定（B38）。**不要宣称罗盘准确** |
| 麦克风/扬声器/马达/扩展槽探测/运行时 RF 探针（3c） | 已编码+主机测试；设备验证**未执行**（B39） |
| 摇杆、烧屏调光(10 分钟减半)、任务看门狗(~10 s)、磁场干扰判断、`-DEXP_RELEASE=1` 展台配置（3d） | 已编码+主机测试；设备验证**未执行**（B40） |
| 日语离线语音（ESP-SR：AFE+WakeNet+MultiNet7，日语指令用音译英文登记；命令 ホーム/モーション/オーディオ/ゲーム/デバイス） | 已编码、已编译、已烧录(0.8.x)；**识别率/误唤醒率未测量**。0.8.0 曾因同时加载两个唤醒词在 `voice_fetch` 断言 PANIC（B43，推断原因），0.8.1 改为一次只加载一个(默认日语 `wn9l_ja_konnichihaesp_tts3`)，板上效果需用户反馈 |
| 4a 相机预览+帧时间测量（`feature/vision`） | **设备已验证（2026-10-11）**：ESP-IDF 编译通过、已烧录；传感器 SC101IOT(1280x720 UYVY)，预览 15.6 fps(耗时 63.9 ms，DMA 等待 39.7 ms，PPA 变换 12.8 ms，字节对调 11.2 ms)，PSRAM 剩余 8399 KB，预设 1/8(rot 270 mirror x0 y1) 方向正向 |
| 4c Tilt Maze「カメラ」手势跟踪控制与 ESP-Iris USB CDC0 日志（`feature/vision`） | **设备已验证（2026-10-13，`0.9.3-stage4c`）**：切换为 `hand_detect` (ESPDet-Pico 224×224)，Core 1 原生降采样 480×480→224×224，异步推理 ~6.8 fps；方案 A 高增益映射（18 px 死区 + 1.6× 增益，满偏 ±0.60 g），手部微动灵敏跟手，用户实测体验良好；菜单/HUD 画中画（PiP）实时预览手部跟踪框与准星十字；S31 高速 USB OTG CDC0 实现免 GPIO33 冲突的高速日志流（`scripts/iris_log.py` 实测 0B 丢包）；全套 RF 审计 PASS |
| 左侧磁吸 M5Stack Logo 模式（`feature/magnetic-logo-left`） | **设备已验证（2026-10-15）**：右侧磁吸贴合时，左板自动秒切 M5Stack Logo 纯白全屏显示；分离恢复原有页面与相机管线；Flash 常量 RGB565 资源（~68.9 KB，零动态堆内存）；复用既有 BMM150 轮询做专用溢出/强磁饱和判定，保持原有罗盘滤波；P95 响应延迟 ≤500 ms；全套 RF 审计 PASS（0 findings） |

| RF 实测、日本法规、8 小时稳定性、展台镜像验收 | **未开始** |

RF 现状（`docs/06`）：静态扫描通过；Wi-Fi/COEX 配置项由 SoC 推导**无法用配置关闭**；链接图中无 Wi-Fi MAC/BT/802.15.4/lwIP，但 **PHY 驱动被链接，运行时是否启用未验证**。官方 Vibe Mode 启动即开 Wi-Fi，展位禁止进入（AI 键 GPIO7）。

## 7. 构建、测试、烧录

```bash
# Host 全套检查（云端或 Mac）
export MOSAICO_ENGINE_ROOT=<…>/esp-mosaico-vibe/submodule/raylib-lite-engine   # bootstrap 后通常自动
./scripts/run_all_tests.sh        # 期望: ALL HOST CHECKS PASSED (simulator only…)

# 固件（Mac）
. ~/esp/esp-idf-mosaico-7b9cc1a/export.sh && cd firmware
idf.py build                                         # 普通
idf.py -B build_voice  -DEXP_VOICE=1  build          # 语音
idf.py -B build_vision -DEXP_VISION=1 build          # 视觉（feature/vision）
python3 ../scripts/rf_map_check.py <build>/mosaico_experience.map <build>/sdkconfig   # 每次构建后必做
# 改过 sdkconfig.defaults* 后先删 <build>/sdkconfig
```

* 语音烧录：`scripts/voice_flash.sh check|write|update|coredump <端口>`；写入需 `VOICE_FLASH_CONFIRM=yes`；先 `check`（只读，核对 MAC 与分区表）。
* 视觉烧录：`scripts/vision_flash.sh check|update|coredump <端口>`，需 `VISION_FLASH_CONFIRM=yes`；只写 app(0x10000)，写前强制 `check` 通过（30 分钟印记）。
* 分区表（语音/视觉/同构）：nvs 0x9000/24K、phy_init 0xf000/4K、factory 0x10000/6M、model 0x610000/4M(spiffs)、coredump 0xa10000/512K。
* 烧完黑屏先按 Reset。崩溃诊断：`<script> coredump <端口>` 只读取出再用 `esp-coredump` 解码。
* 展台镜像：`-DEXP_RELEASE=1` + `scripts/release_check.py`；开发镜像带调试叠加层，不得上展台。

## 8. 视觉路线（用户已批准的方向）

用户 2026-10-11 同意：做摄像头功能；"头部/颜色控制迷宫、手势导航都加上"。分步：

* **4a（设备已验证，2026-10-11）**：BSP `mosaico_module_camera`(左槽,DVP, SC101IOT 1280×720 UYVY) → PPA 裁剪/旋转/缩放 → 480×480 RGB565 → 屏幕叠加实测 15.6 fps / 63.9 ms / PSRAM 8399 KB。预设 1/8 方向正向。
* **4b（设备已验证，2026-10-13）**：实测 `espressif/human_face_detect` (MSRMNP-s8 V1) 在 ESP32-S31 上的速度为 **`46.6 ms / 21.4 fps`**。模型内嵌于 Flash `.rodata`。
* **4c（设备已验证，2026-10-13）**：切换为 `hand_detect` (ESPDet-Pico 224×224)，Core 1 原生降采样 480×480→224×224，方案 A 高增益手势控制（18 px 死区 + 1.6× 增益），画中画实时跟踪。
* **同构固件合并（设备已验证，2026-10-14）**：合并语音识别（ESP-SR）与手势跟踪（ESPDet）于一体（App ~5.28 MB，6 MB 分区），关闭屏幕黄色调试叠层（`EXP_TIMING_OVERLAY=0`），利用 ESP-Iris USB CDC0 流式输出日志，配置 `.bk_buffer_dis = true` 优化 DVP 内存。用户真机测试确认效果优异。
* **4d 左侧磁吸 M5Stack Logo（设备已验证，2026-10-15，`feature/magnetic-logo-left`）**：针对右边缘第二块 MOSAICO 磁力贴合开发。复用 BMM150 饱和强磁识别 + 宿主单元测试状态机，居中呈现原尺寸等比缩放的 420×84 RGB565 M5Stack Logo，全白背景覆盖；贴合期间安全挂起 Camera 线程与触摸语音事件，分离后秒级恢复，全过程保持零 RF 激活。

## 9. 已知问题速览（完整见 `docs/10`）

B19 板上 Recovery 已擦除 · B21 运行时无串口 · B32 BQ27220 配置被官方驱动改写(已改只读) · B38 罗盘偏差 · B39 3c 未上板 · B40 3d 未上板 · B41/B42 日语语音是"音译英语"+单唤醒词已验证 · B43 双唤醒词 PANIC · B44 4a 设备已验证 · B45 4b 人脸检测基准设备已验证 · B46 4c 手势跟踪与同构固件设备已验证 · B47 同构固件与 DVP 修复 · B48 左侧磁吸 M5Stack Logo 模式设备已验证。

## 10. 立即要做的事（按顺序）

1. **推送 feature/vision 与 feature/magnetic-logo-left 分支至远程 GitHub 仓库**（待用户授权）。
2. 长期：3c/3d 设备验证、罗盘问题、RF 实测与日本法规、8 小时稳定性、展台镜像验收。


