#!/usr/bin/env python3
import subprocess
import time
import os
import sys

print("=== MOSAICO 全量擦除并重新烧录服务 ===")
print("请在板子上操作以进入 ROM 下载模式：")
print("  >>> 按住 Boot 键 -> 按一下 Reset 键 -> 松开 Boot 键 <<<")
print("脚本检测到端口后，将自动执行：擦除全部现有数据/配置（NVS/PHY）+ 写入最新全量固件")

timeout = 300
start_time = time.time()

PYTHON_BIN = "/Users/kongweilu/.espressif/python_env/idf6.2_py3.14_env/bin/python"
IDF_PATH = "/Users/kongweilu/esp/esp-idf-mosaico-7b9cc1a"

while time.time() - start_time < timeout:
    port = None
    try:
        dev_list = os.listdir("/dev")
    except Exception:
        dev_list = []

    for p in dev_list:
        if p.startswith("cu.usbmodem"):
            candidate = f"/dev/{p}"
            if "usbserial" not in candidate and candidate != "/dev/cu.usbserial-1120":
                port = candidate
                break

    if port:
        print(f"[{time.strftime('%T')}] 检测到开发板端口: {port}，等待 1.5 秒串口稳定...")
        time.sleep(1.5)

        cmd = [
            PYTHON_BIN, "-m", "esptool",
            "--chip", "esp32s31",
            "-p", port,
            "-b", "921600",
            "--before", "no-reset",
            "--after", "hard-reset",
            "--no-stub",
            "write-flash",
            "--flash-mode", "dio",
            "--flash-size", "16MB",
            "--flash-freq", "80m",
            "0x2000", "firmware/build_vision/bootloader/bootloader.bin",
            "0x8000", "firmware/build_vision/partition_table/partition-table.bin",
            "0x9000", "firmware/build_vision/blank_nvs.bin",
            "0xf000", "firmware/build_vision/blank_phy.bin",
            "0x10000", "firmware/build_vision/mosaico_experience.bin",
            "0x610000", "firmware/build_vision/srmodels/srmodels.bin"
        ]

        print(f"[{time.strftime('%T')}] 开始烧录（921600 高速波特率，Bootloader + 分区表 + 清空NVS/PHY + App + 语音模型）...")
        env = os.environ.copy()
        env["IDF_PATH"] = IDF_PATH
        res = subprocess.run(cmd, env=env)
        if res.returncode == 0:
            print("\n========================================================")
            print("=== 恭喜！旧固件及数据已全部清除，最新全量固件烧录成功！ ===")
            print("========================================================\n")
            sys.exit(0)
        else:
            print(f"烧录返回错误码 {res.returncode}，若连接中断请重新进入下载模式，继续监听...")
            time.sleep(2)

    time.sleep(0.5)

print("超时未检测到进入 ROM 下载模式的开发板")
sys.exit(1)
