#!/usr/bin/env bash
# Build the native E1 physics HOST driver (fzx-phys) against an SH2ZAM checkout.
#   SH2ZAM_ROOT=../SH2ZAM tools/physics_host/build.sh [CC] [OPT]
# Output: tools/physics_host/build/<cc><opt>/fzx-phys (git-ignored build folder).
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SH2ZAM_ROOT=${SH2ZAM_ROOT:-$ROOT/third_party/SH2ZAM}
S=$SH2ZAM_ROOT/saturn
OUT=$ROOT/tools/physics_host/build/${CC}${OPT}
mkdir -p "$OUT"
"$CC" -std=c11 $OPT -Wall -Wextra -Werror -I"$S/include" -I"$S/src" -I"$ROOT/saturn" \
    "$ROOT/tools/physics_host/physics_main.c" "$ROOT/saturn/physics_native.c" \
    "$S/src/sh2zam_vehicle_dynamics.c" "$S/src/sh2zam_unit30.c" "$S/src/sh2zam_divu.c" \
    -o "$OUT/fzx-phys"
echo "$OUT/fzx-phys"
