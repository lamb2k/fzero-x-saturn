#!/usr/bin/env bash
# Build the attacks HOST driver (fzx-atk): the engine's field, attacks and physics sources on HOST.
#   SH2ZAM_ROOT=<SH2ZAM with body-contact-1> GEN=<generated dir> [TUBES=1] [RIVAL_SURFACE=0] tools/attacks_host/build.sh [CC] [OPT] [EXTRA CFLAGS...]
# Output: tools/attacks_host/build/<cc><opt>/fzx-atk (git-ignored). GEN must hold rivals/ and attacks/ (local only).
# As the image builds them: RIVAL_SURFACE (default 1) is ENG_RIVAL_SURFACE (the rivals meet the course's zones and
# tubes, with trig12); TUBES=1 is ENG_TUBES (vehicle-dynamics-2 for the v3 packs of Sand Ocean and Big Blue).
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
shift 2 || true
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${SH2ZAM_ROOT:?SH2ZAM_ROOT}/saturn
G=${GEN:-$ROOT/generated}
OUT=$ROOT/tools/attacks_host/build/${CC}${OPT}${TAG:-}
RS=${RIVAL_SURFACE:-1}
EXTRA=()
[ "$RS" = 1 ] && EXTRA+=("$S/src/sh2zam_trig12.c")
if [ "${TUBES:-0}" = 1 ]; then
    OUT=$OUT-tubes
    EXTRA+=(-DFZX_PHYS_TUBES=1 "$S/src/sh2zam_vehicle_dynamics2.c" "$S/src/sh2zam_vehicle_contact2.c")
fi
mkdir -p "$OUT"
DEFS="-DE1_FIELD=29 -DE2_PACK=1 -DE2_FASTPOSE=1 -DE2_LOD=1 -DE2_LOD_NEAR=1000,3000,8000 -DENG_ATTACKS=1 -DENG_RIVAL_SURFACE=$RS"
"$CC" -std=gnu11 $OPT -Wall -Wextra -Werror $DEFS "$@" -I"$ROOT/tools/attacks_host/include" -I"$S/include" -I"$S/src" \
    -I"$ROOT/saturn" -I"$ROOT/saturn/engine" -I"$G/rivals" -I"$G/attacks" \
    "$ROOT/tools/attacks_host/attacks_main.c" "$ROOT/saturn/physics_native.c" "$ROOT/saturn/engine/engine_field.c" \
    "$ROOT/saturn/engine/engine_attacks.c" \
    "$S/src/sh2zam_vehicle_dynamics.c" "$S/src/sh2zam_vehicle_contact.c" "$S/src/sh2zam_body_contact.c" \
    "$S/src/sh2zam_line_follower.c" "$S/src/sh2zam_line_field.c" "$S/src/sh2zam_unit30.c" "$S/src/sh2zam_divu.c" \
    "${EXTRA[@]}" -o "$OUT/fzx-atk"
echo "$OUT/fzx-atk"
