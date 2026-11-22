#!/usr/bin/env bash
# Build the attacks duel HOST runners (C-092 judge): fzx-duel-native (road), fzx-duel-native-tubes
# (FZX_PHYS_TUBES) and fzx-pair-probe.
#   SH2ZAM_ROOT=<SH2ZAM with body-contact-1> GEN=<generated dir> tools/attacks_host/duel_build.sh OUT_DIR [CC] [OPT]
# GEN must hold attacks/ (tools/attacks_tables.py; local only).
#
# The opponent's step. In the duel reference racer 1 has an id at or above gNumPlayers (the harness
# sets one human player after the VS set-up), so the original's per-tick spline search runs for it
# with the computer racers' bounds: 3 iterations and a squared tolerance of 5 (racer.c, the
# func_8009EBEC call in the racer update; the human player gets 100 and 1). The library step uses
# the player's bounds. The runner's racer 1 is therefore stepped by a private copy of the library
# step with the computer bounds, made here from the library source by one exact-line substitution
# (the build fails if the anchor line is not found exactly once). The library and the engine are
# not changed; racer 0 (the player) uses the library step as the engine does.
set -euo pipefail
OUT=${1:?OUT_DIR}
CC=${2:-gcc}
OPT=${3:--O2}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${SH2ZAM_ROOT:?SH2ZAM_ROOT}/saturn
G=${GEN:?GEN}
mkdir -p "$OUT"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

ANCHOR='        if(sh2z_vd_locate(b, k, target, 100, (int64_t)1 << 32) == 0) {'
OPPLOC='        if(sh2z_vd_locate(b, k, target, 3, (int64_t)5 << 32) == 0) {'
want_once() { # FILE LINE: the line occurs exactly once (fixed string, whole line)
    local n
    n=$(sed 's/\r$//' "$1" | grep -cxF -- "$2" || true)
    [ "$n" = 1 ] || { echo "duel_build: anchor found $n times in $1: $2" >&2; exit 1; }
}
want_once "$S/src/sh2zam_vehicle_dynamics.c" "$ANCHOR"
want_once "$S/src/sh2zam_vehicle_contact.c" 'void sh2z_vd_step_contact(sh2z_vd_body_t *b, const sh2z_vd_track_t *track, const sh2z_vd_vehicle_t *v,'
want_once "$S/src/sh2zam_vehicle_contact2.c" 'void sh2z_vd_step2_contact(sh2z_vd_body_t *b, const sh2z_vd_track2_t *track, const sh2z_vd_vehicle_t *v,'
mkdir -p "$TMP/opp"
sed 's/\r$//' "$S/src/sh2zam_vehicle_dynamics.c" | awk -v a="$ANCHOR" -v b="$OPPLOC" '$0 == a { print b; next } { print }' \
    > "$TMP/opp/sh2zam_vehicle_dynamics.c"
[ "$(grep -cxF -- "$OPPLOC" "$TMP/opp/sh2zam_vehicle_dynamics.c")" = 1 ] || { echo "duel_build: opponent search not placed" >&2; exit 1; }
sed -e 's/\r$//' -e 's/^void sh2z_vd_step_contact(/void fzx_duel_opp_step_contact(/' "$S/src/sh2zam_vehicle_contact.c" > "$TMP/opp/opp_contact.c"
sed -e 's/\r$//' -e 's/^void sh2z_vd_step2_contact(/void fzx_duel_opp_step2_contact(/' "$S/src/sh2zam_vehicle_contact2.c" > "$TMP/opp/opp_contact2.c"

CF="-std=gnu11 $OPT -Wall -Wextra -Werror -I$S/include -I$S/src -I$ROOT/saturn -I$G/attacks"
LIB="$ROOT/saturn/physics_native.c $S/src/sh2zam_vehicle_dynamics.c $S/src/sh2zam_vehicle_contact.c $S/src/sh2zam_body_contact.c
     $S/src/sh2zam_vehicle_dynamics2.c $S/src/sh2zam_vehicle_contact2.c $S/src/sh2zam_unit30.c $S/src/sh2zam_divu.c"
# shellcheck disable=SC2086
"$CC" $CF "$ROOT/tools/attacks_host/duel_native.c" $LIB "$TMP/opp/opp_contact.c" -o "$OUT/fzx-duel-native"
# shellcheck disable=SC2086
"$CC" $CF -DFZX_PHYS_TUBES=1 "$ROOT/tools/attacks_host/duel_native.c" $LIB "$TMP/opp/opp_contact2.c" -o "$OUT/fzx-duel-native-tubes"
# shellcheck disable=SC2086
"$CC" $CF "$ROOT/tools/attacks_host/pair_probe.c" "$S/src/sh2zam_body_contact.c" "$S/src/sh2zam_unit30.c" "$S/src/sh2zam_divu.c" -lm \
    -o "$OUT/fzx-pair-probe"
ls -la "$OUT"
