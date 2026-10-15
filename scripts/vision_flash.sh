#!/usr/bin/env bash
# Vision build (EXP_VISION) flashing helper for the Mac. Written in the cloud and NOT RUN YET.
#
#   bash scripts/vision_flash.sh check <port>     read-only: chip type, MAC and the partition table on the board; records a stamp
#                                                 when the table is the voice/vision layout (factory 5 MB at 0x10000, coredump)
#   bash scripts/vision_flash.sh update <port>    writes ONLY the app at 0x10000. Needs VISION_FLASH_CONFIRM=yes and a stamp from a
#                                                 successful `check` of the last 30 minutes. The board is reset at the end.
#   bash scripts/vision_flash.sh coredump <port>  read-only: reads the crash-dump partition (0x910000) into build_vision/coredump.bin
#                                                 and decodes it with esp-coredump against build_vision/mosaico_experience.elf
#
# The vision image uses the SAME partition table as the voice image, so nothing but the app is written: the partition table, the
# "model" partition (ESP-SR models) and the bootloader are never touched. To go back to speech control, flash the voice app with
# scripts/voice_flash.sh update. A battery keeps the chip powered, so unplugging USB is NOT a power cycle: use the Reset button.
# Never writes: bootloader, partition table, models, eFuse, security settings; never erases the whole flash.
# Run from firmware/ after:  idf.py -B build_vision -DEXP_VISION=1 build
set -euo pipefail

MODE="${1:-}"; PORT="${2:-}"
EXPECT_MAC="1c:29:04:d0:a3:36"
BUILD="${BUILD_DIR:-build_vision}"
OFF_PT=0x8000; OFF_APP=0x10000; OFF_CORE=0xa10000; CORE_SIZE=0x80000
APP_MAX=$((0x600000))
STAMP="$BUILD/.table_checked"

die() { echo "ERROR: $*" >&2; exit 1; }
[[ "$MODE" == "check" || "$MODE" == "update" || "$MODE" == "coredump" ]] || die "usage: $0 check|update|coredump <port>"
[[ -n "$PORT" ]] || die "give the serial port explicitly (never auto-detected)"
[[ "$PORT" == *11201* ]] && die "$PORT is the other project's ESP32-S3: refusing"
[[ -e "$PORT" ]] || die "$PORT does not exist (is the board in download mode: hold Boot, press Reset, release Boot?)"
command -v esptool >/dev/null || die "esptool not found (source the ESP-IDF export script first)"

ESP_BASE=(esptool --chip esp32s31 -p "$PORT" --before no-reset --no-stub)
ESP=("${ESP_BASE[@]}" --after no-reset)      # read-only modes keep the board in download mode

# Everything that must happen in download mode is done in ONE esptool invocation (the USB port can vanish between two runs on
# this board; esptool v5 has no plain command chaining; the connect banner prints the MAC).
if [[ "$MODE" == "check" ]]; then
  echo "== chip / MAC + partition table currently on the board (read-only, one connection)"
  mkdir -p "$BUILD"
  PT=$(mktemp -t pt_now)
  OUT="$("${ESP[@]}" read-flash $OFF_PT 0x1000 "$PT" 2>&1)" || { echo "$OUT" | tail -15; die "could not read the board (re-enter download mode and retry)"; }
  echo "$OUT" | grep -i "mac:" | sort -u
  echo "$OUT" | grep -qi "$EXPECT_MAC" || { rm -f "$PT"; die "MAC is not $EXPECT_MAC: wrong board, refusing"; }
  TBL="$(python3 "${IDF_PATH:?IDF_PATH not set}/components/partition_table/gen_esp32part.py" "$PT" || true)"
  rm -f "$PT"
  echo "$TBL"
  NORM="$(echo "$TBL" | tr -d ' ')"
  if echo "$NORM" | grep -q '^factory,app,factory,0x10000,6M' && echo "$NORM" | grep -q '^coredump,data,coredump,0xa10000,512K'; then
    date +%s > "$STAMP"
    echo "table OK (voice/vision layout): the vision app can be written with update. Stamp recorded (valid 30 minutes)."
  else
    rm -f "$STAMP"
    die "the table on the board is NOT the voice/vision layout: do not write. Flash the voice build first (scripts/voice_flash.sh write), which sets the table."
  fi
  echo "check finished; nothing was written"; exit 0
fi

if [[ "$MODE" == "coredump" ]]; then
  CORE="$BUILD/coredump.bin"
  echo "== reading the crash-dump partition ($CORE_SIZE bytes at $OFF_CORE, read-only)"
  OUT="$("${ESP[@]}" read-flash $OFF_CORE $CORE_SIZE "$CORE" 2>&1)" || { echo "$OUT" | tail -15; die "could not read the board"; }
  echo "$OUT" | grep -i "mac:" | sort -u
  echo "$OUT" | grep -qi "$EXPECT_MAC" || die "MAC is not $EXPECT_MAC: wrong board, refusing"
  command -v esp-coredump >/dev/null || die "esp-coredump not found (source the ESP-IDF export script first)"
  esp-coredump info_corefile --core "$CORE" --core-format raw "$BUILD/mosaico_experience.elf" || echo "(decoding failed: send this output and $CORE)"
  exit 0
fi

[[ "${VISION_FLASH_CONFIRM:-}" == "yes" ]] || die "update needs VISION_FLASH_CONFIRM=yes"
[[ -f "$BUILD/mosaico_experience.bin" ]] || die "$BUILD/mosaico_experience.bin missing: build the vision image first"
[[ -f "$STAMP" ]] && [[ -n "$(find "$STAMP" -mmin -30 2>/dev/null)" ]] || die "no successful 'check' in the last 30 minutes: run check first (it confirms the board and its partition table)"
sz() { stat -f%z "$1" 2>/dev/null || stat -c%s "$1"; }
(( $(sz "$BUILD/mosaico_experience.bin") <= APP_MAX )) || die "app does not fit the 6 MB factory partition"
# (no "grep -q" in a pipe: with pipefail it can fail on SIGPIPE although the string is there)
VER_HITS=$(LC_ALL=C grep -a -c -- "-vision-" "$BUILD/mosaico_experience.bin" || true)
(( VER_HITS > 0 )) || die "image is not a vision build (version string): refusing"

echo "== writing the app only at $OFF_APP (no whole-flash erase; partition table, models and bootloader untouched)."
# last command of the run: default "--after hard-reset" so the new image starts by itself
"${ESP_BASE[@]}" write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m $OFF_APP "$BUILD/mosaico_experience.bin"
rm -f "$STAMP"
echo "done. The board was reset. If the screen stays dark press the Reset button (unplugging USB does not power-cycle a board with a battery)."
