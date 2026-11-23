#!/usr/bin/env bash
# Build the attacks HOST driver (fzx-atk): the engine's field, attacks and physics sources on HOST.
#   SH2ZAM_ROOT=<SH2ZAM checkout at the engine pin> GEN=<the course's generated root> [ATK_GEN=<generated root with attacks/>]
#   [TUBES=1 for a tube course] [TAG=-name] tools/attacks_host/build.sh [CC] [OPT] [EXTRA CFLAGS...]
# Output: tools/attacks_host/build/<cc><opt><TAG>/fzx-atk (git-ignored). GEN must hold rivals/ (local only); attacks/ comes from
# ATK_GEN (default GEN). The field is built as the image builds it by default: line-field-4 surface, line-field-5 behaviour,
# class rows off, the E2 level-of-detail bands; the pit recharge (line-field-15) on when the library has it, as the image's default (RECHARGE=0/1 forces).
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
shift 2 || true
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${SH2ZAM_ROOT:?SH2ZAM_ROOT}/saturn
G=${GEN:-$ROOT/generated}
AG=${ATK_GEN:-$G}
TUBES=${TUBES:-0}
OUT=$ROOT/tools/attacks_host/build/${CC}${OPT}${TAG:-}
mkdir -p "$OUT"
DEFS="-DE1_FIELD=29 -DE2_PACK=1 -DE2_FASTPOSE=1 -DE2_LOD=1 -DE2_LOD_NEAR=1000,3000,8000 -DENG_ATTACKS=1 -DFZX_ATK_HOST_TESTS=1"
DEFS="$DEFS -DENG_RIVAL_SURFACE=1 -DENG_RIVAL_BEHAVE=1 -DENG_RIVAL_CLASS=0 -DENG_RIVAL_RECHARGE=${RECHARGE:-$(grep -q line-field-15 "$S/include/sh2zam/line_field_experimental.h" && echo 1 || echo 0)} -DFZX_PHYS_TUBES=$TUBES"
EXTRA=()
if [ "$TUBES" = 1 ]; then
    EXTRA=("$S/src/sh2zam_vehicle_dynamics2.c" "$S/src/sh2zam_vehicle_contact2.c")
fi
"$CC" -std=gnu11 $OPT -Wall -Wextra -Werror -Wno-type-limits $DEFS "$@" -I"$ROOT/tools/attacks_host/include" -I"$S/include" -I"$S/src" \
    -I"$ROOT/saturn" -I"$ROOT/saturn/engine" -I"$G/rivals" -I"$AG/attacks" \
    "$ROOT/tools/attacks_host/attacks_main.c" "$ROOT/saturn/physics_native.c" "$ROOT/saturn/engine/engine_field.c" \
    "$ROOT/saturn/engine/engine_attacks.c" \
    "$S/src/sh2zam_vehicle_dynamics.c" "$S/src/sh2zam_vehicle_contact.c" "$S/src/sh2zam_body_contact.c" \
    "$S/src/sh2zam_line_follower.c" "$S/src/sh2zam_line_field.c" "$S/src/sh2zam_unit30.c" "$S/src/sh2zam_divu.c" \
    "$S/src/sh2zam_trig12.c" "${EXTRA[@]}" \
    -o "$OUT/fzx-atk"
echo "$OUT/fzx-atk"
