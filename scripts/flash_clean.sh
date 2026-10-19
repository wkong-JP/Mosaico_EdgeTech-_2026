#!/usr/bin/env bash
set -euo pipefail

source ~/esp/esp-idf-mosaico-7b9cc1a/export.sh

echo "=== 准备从头完整烧录 MOSAICO 固件 (Bootloader + 分区表 + App + 语音模型) ==="
echo "请在板子上操作：按住 Boot 键 -> 按一下 Reset 键 -> 松开 Boot 键"

for i in $(seq 1 60); do
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n 1 || true)
  if [[ -n "$PORT" && -e "$PORT" ]]; then
    echo "[$(date +%T)] 正在连接 $PORT (尝试 $i/60)..."
    if python -m esptool --chip esp32s31 -p "$PORT" -b 921600 --before no-reset --after hard-reset --no-stub write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m \
        0x2000 firmware/build_vision/bootloader/bootloader.bin \
        0x8000 firmware/build_vision/partition_table/partition-table.bin \
        0x10000 firmware/build_vision/mosaico_experience.bin \
        0x610000 firmware/build_vision/srmodels/srmodels.bin; then
      echo "=== 恭喜！全量固件已从头完整烧录成功！ ==="
      exit 0
    fi
  else
    echo "[$(date +%T)] 等待 /dev/cu.usbmodem* 端口枚举..."
  fi
  sleep 1
done

echo "超时：未在 60 秒内检测到进入下载模式的 MOSAICO 开发板"
exit 1
