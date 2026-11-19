#!/usr/bin/env bash
# Build the E1 physics SH-2 bridge ELF and the model harness.
#   SH2ZAM_ROOT=... YABAUSE_SOURCE=... CROSS=sh2eb-elf- tools/physics_sh2/build.sh
# YABAUSE_SOURCE is a checkout of the commit SH2ZAM's sh2-model/run.sh pins.
# Output: tools/physics_sh2/build/{phys_bridge.elf,phys_bridge.sym,phys_harness}.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SH2ZAM_ROOT=${SH2ZAM_ROOT:-$ROOT/third_party/SH2ZAM}
S=$SH2ZAM_ROOT/saturn
CROSS=${CROSS:-sh2eb-elf-}
OUT=$ROOT/tools/physics_sh2/build
mkdir -p "$OUT"
"${CROSS}gcc" -std=c11 -O2 -m2 -mb -ffreestanding -nostdlib -nostartfiles -Wall -Wextra -Werror \
    -I"$S/include" -I"$S/src" -I"$ROOT/saturn" \
    "$ROOT/tools/physics_sh2/phys_bridge.c" "$ROOT/saturn/physics_native.c" \
    "$S/src/sh2zam_vehicle_dynamics.c" "$S/src/sh2zam_unit30.c" "$S/src/sh2zam_divu.c" \
    -T "$ROOT/tools/physics_sh2/bridge.ld" -Wl,--gc-sections -lgcc -o "$OUT/phys_bridge.elf"
"${CROSS}nm" "$OUT/phys_bridge.elf" > "$OUT/phys_bridge.sym"
"${CROSS}size" "$OUT/phys_bridge.elf" | tail -1
YAB=${YABAUSE_SOURCE:?set YABAUSE_SOURCE to the pinned Yabause checkout}
"${CC:-cc}" -O2 -fwrapv -fno-strict-aliasing -ffunction-sections -fdata-sections -Wl,--gc-sections -DHAVE_STDINT_H -I"$YAB/yabause/src" -I"$S/benchmarks/sh2-model" \
    "$ROOT/tools/physics_sh2/phys_harness.c" -lm -o "$OUT/phys_harness"
echo "$OUT"
