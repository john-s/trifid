#!/bin/sh
# SPDX-License-Identifier: MIT
# Trifid's tests, all on the computer (no FM-1, no JieLi toolchain):
#   dsp        the DSP modules natively (tests/dsp/dsp_test.c): ignition, limiter, Starve, Grit 0, numerics, pitch,
#              the echo's Freeze
#   host       the whole app in fm1-base's host simulator (tests/scenarios/*.txt), screenshots in app/build/scenarios/
#   web        the browser emulator builds (emcc; skipped without Emscripten)
#   ram        the static RAM report against the pool budget (tools/ram_report.py; clang), into docs/ram.md
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
OUT=app/build/tests
FAIL=0
mkdir -p "$OUT" app/build/scenarios
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
DSP="app/dsp/plant.c app/dsp/rot.c app/dsp/echo.c app/dsp/out.c app/dsp/engine.c"
CF="-O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Iapp/dsp -Ibase/include"
run dsp sh -c "$CC $CF -o $OUT/dsp_test tests/dsp/dsp_test.c $DSP -lm && $OUT/dsp_test"
run render sh -c "$CC $CF -Wno-unused-parameter -o $OUT/render tests/dsp/render.c $DSP -lm && $CC $CF -o $OUT/bench tests/dsp/bench.c $DSP -lm && $OUT/bench"
run host-build sh build.sh --host
for s in tests/scenarios/*.txt; do
    run "host-$(basename "$s" .txt)" app/build/host/trifid_host "$s" app/build/scenarios
done
if command -v emcc >/dev/null 2>&1; then
    run web-build sh build.sh --web
else
    echo "  skip web-build (no emcc: Emscripten)"
fi
if command -v clang >/dev/null 2>&1; then
    run ram "$PY" tools/ram_report.py --md docs/ram.md
else
    echo "  skip ram (no clang)"
fi
grep -h "engine:" "$OUT/render.log" 2>/dev/null | sed 's/^/       /'
[ $FAIL = 0 ] && echo "all passed" || echo "FAILED"
exit $FAIL
