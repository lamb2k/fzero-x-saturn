#!/usr/bin/env bash
# Build the native E1 physics HOST driver (fzx-phys) against an SH2ZAM checkout.
#   SH2ZAM_ROOT=../SH2ZAM [TUBES=1] tools/physics_host/build.sh [CC] [OPT]
# Output: tools/physics_host/build/<cc><opt>/fzx-phys (git-ignored build folder); with TUBES=1
# (pipes and cylinders, SH2ZAM vehicle-dynamics-2) build/<cc><opt>-tubes/fzx-phys.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SH2ZAM_ROOT=${SH2ZAM_ROOT:-$ROOT/third_party/SH2ZAM}
S=$SH2ZAM_ROOT/saturn
TUBES=${TUBES:-0}
OUT=$ROOT/tools/physics_host/build/${CC}${OPT}
EXTRA=()
if [ "$TUBES" = 1 ]; then
    OUT=$OUT-tubes
    EXTRA=(-DFZX_PHYS_TUBES=1 "$S/src/sh2zam_vehicle_dynamics2.c")
fi
mkdir -p "$OUT"
"$CC" -std=c11 $OPT -Wall -Wextra -Werror -I"$S/include" -I"$S/src" -I"$ROOT/saturn" \
    "$ROOT/tools/physics_host/physics_main.c" "$ROOT/saturn/physics_native.c" \
    "$S/src/sh2zam_vehicle_dynamics.c" "$S/src/sh2zam_unit30.c" "$S/src/sh2zam_divu.c" "${EXTRA[@]}" \
    -o "$OUT/fzx-phys"
echo "$OUT/fzx-phys"
