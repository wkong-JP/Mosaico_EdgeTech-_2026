#!/usr/bin/env bash
# Everything that can be verified without hardware. Results are written to evidence/.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/app/mosaico_experience"
EV="$ROOT/evidence"
mkdir -p "$EV/screenshots"
rc=0
step() { echo; echo "=== $1"; }
step "font data is reproducible from exp_strings.h"
cp "$APP/main/exp_font_data.c" /tmp/exp_font_before.c
fontout="$(python3 "$APP/tools/gen_font.py" 2>&1)"; fontrc=$?
if [ $fontrc -ne 0 ] && echo "$fontout" | grep -q "not found"; then
  echo "[skip] Noto Sans CJK not installed here; committed exp_font_data.c is used as is (reproducibility check not run)"
  cp /tmp/exp_font_before.c "$APP/main/exp_font_data.c"
elif [ $fontrc -eq 0 ] && cmp -s /tmp/exp_font_before.c "$APP/main/exp_font_data.c"; then echo "font data up to date"
else echo "font data was stale and has been regenerated: re-run"; rc=1; fi
step "compile (gcc/clang -Werror) + native unit tests"
"$ROOT/scripts/check_build.sh" 2>&1 | tee "$EV/native_tests.log" || rc=1
step "Route B drawing shim == official engine (pixel equivalence)"
python3 "$APP/tests/surface_equivalence.py" | tail -4 || rc=1
step "Route B fast blend == original /255 formula (exhaustive, plain + byte-swapped builds)"
for swapflag in "" "-DEXP_SURFACE_SWAP_BYTES=1"; do
  cc -std=c11 -O2 -Wall -Wextra -Werror $swapflag -I "$ROOT/firmware/components/exp_surface/include" \
     "$APP/tests/blend_exact.c" -o /tmp/blend_exact_bin && /tmp/blend_exact_bin || rc=1
done
step "screen-log ring buffer (portable part of exp_dbglog; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror \
   -I "$ROOT/firmware/components/exp_dbglog/include" "$APP/tests/dbglog_ring_test.c" -o /tmp/dbglog_ring_bin \
   && /tmp/dbglog_ring_bin || rc=1
step "device sensor helpers (axis map, gyro mean; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror -lm \
   "$APP/tests/dev_sensors_core_test.c" -o /tmp/dev_sensors_core_bin -lm && /tmp/dev_sensors_core_bin || rc=1
step "magnetic edge classifier and dock state machine (right dock, debounce; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror -I "$ROOT/firmware/main" \
   "$APP/tests/dev_magnet_core_test.c" -o /tmp/dev_magnet_core_bin && /tmp/dev_magnet_core_bin || rc=1
step "device I/O helpers (mic decimation, UI cues, motor limiter, slot classes, crash guard; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror -I "$ROOT/firmware/main" -I "$APP/main" \
   "$APP/tests/dev_io_core_test.c" -o /tmp/dev_io_core_bin -lm && /tmp/dev_io_core_bin || rc=1
step "offline voice helpers (48->16 kHz low-pass, slot choice, speaker mask, command table, status machine; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror -I "$ROOT/firmware/main" -I "$APP/main" \
   "$APP/tests/dev_voice_core_test.c" -o /tmp/dev_voice_core_bin -lm && /tmp/dev_voice_core_bin || rc=1
step "camera preview helpers (crop geometry, byte swap, orientation presets, fps, slot gate; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror -I "$ROOT/firmware/main" -I "$APP/main" \
   "$APP/tests/dev_camera_core_test.c" -o /tmp/dev_camera_core_bin -lm && /tmp/dev_camera_core_bin || rc=1
step "face detect helpers (bounding box clamp and center; gcc with ASan/UBSan)"
cc -std=c11 -O1 -g -fsanitize=address,undefined -Wall -Wextra -Wpedantic -Werror -I "$ROOT/firmware/vision_components/exp_vision/include" \
   "$APP/tests/dev_face_core_test.c" -o /tmp/dev_face_core_bin && /tmp/dev_face_core_bin || rc=1
step "static RF source scan"
python3 "$ROOT/scripts/rf_check.py" | tee "$EV/rf_static_scan.json" || rc=1
step "Host behavioural regression"
python3 "$APP/tests/ui_regression.py" --json "$EV/ui_regression.json" | tail -25 || rc=1
step "screenshots + layout recorder (overlap / overflow / touch size / missing glyphs)"
python3 "$APP/tools/capture_screens.py" "$EV/screenshots" || rc=1
echo; [ $rc -eq 0 ] && echo "ALL HOST CHECKS PASSED (simulator only: no device, no RF measurement)" || echo "SOME CHECKS FAILED"
exit $rc
