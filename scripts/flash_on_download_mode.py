#!/usr/bin/env python3
import subprocess
import time
import os
import sys

def get_usb_tree():
    try:
        return subprocess.check_output(["ioreg", "-p", "IOUSB", "-w0"]).decode('utf-8', errors='ignore')
    except Exception:
        return ""

print("=== MOSAICO 烧录监听服务已启动 ===")
print("请在板子上操作：按住 Boot 键 -> 按一下 Reset 键 -> 松开 Boot 键")

timeout = 600  # 10 minutes timeout
start_time = time.time()

while time.time() - start_time < timeout:
    tree = get_usb_tree()
    # When in ROM download mode, ESP-Iris is gone and a new USB device or USB JTAG unit is present on 01140000
    if "ESP-Iris" not in tree and ("01140000" in tree or "Espressif" in tree or "USB JTAG" in tree):
        print(f"[{time.strftime('%T')}] 检测到开发板已进入 ROM 下载模式！等待串口稳定...")
        time.sleep(1.2)
        port = None
        for p in os.listdir("/dev"):
            if p.startswith("cu.usbmodem"):
                port = f"/dev/{p}"
                break
        if port:
            print(f"[{time.strftime('%T')}] 正在以 921600 波特率向 {port} 烧录全部固件...")
            cmd = [
                "/Users/kongweilu/.espressif/python_env/idf6.2_py3.14_env/bin/python", "-m", "esptool",
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
                "0x10000", "firmware/build_vision/mosaico_experience.bin"
            ]
            env = os.environ.copy()
            env["IDF_PATH"] = "/Users/kongweilu/esp/esp-idf-mosaico-7b9cc1a"
            res = subprocess.run(cmd, env=env)
            if res.returncode == 0:
                print("=== 恭喜！Bootloader、分区表、App、语音模型库全部从头烧录成功！ ===")
                sys.exit(0)
            else:
                print(f"烧录返回错误码 {res.returncode}，继续监听...")
    time.sleep(0.5)

print("超时未检测到进入下载模式")
sys.exit(1)
