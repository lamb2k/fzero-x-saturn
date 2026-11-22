#!/usr/bin/env bash
# Build the reference damage breakdown (attacks lane): the original GP with 30 racers, losses by source.
#   ref/duel_build.sh [CC] [OPT]
# Output: ref/build/<cc><opt>-damage/fzx-damage. As ref/handling_build.sh, from a private edited copy of
# ref/build.sh, which is never changed.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
sed -e "s|^ROOT=.*|ROOT=$ROOT|" \
    -e 's|^OUT=$ROOT/ref/build/${CC}${OPT}$|OUT=$ROOT/ref/build/${CC}${OPT}-damage|' \
    -e 's|^BRIDGES="bridge hooks"$|BRIDGES="bridge hooks handling_probe course_line rivals_bake damage_probe"|' \
    -e 's|^DRIVER=main$|DRIVER=gp_damage_main|' \
    "$ROOT/ref/build.sh" > "$TMP/build.sh"
for want in "ROOT=$ROOT" 'OUT=$ROOT/ref/build/${CC}${OPT}-damage' 'BRIDGES="bridge hooks handling_probe course_line rivals_bake damage_probe"' 'DRIVER=gp_damage_main'; do
    grep -qxF "$want" "$TMP/build.sh" || { echo "damage_build: ref/build.sh changed; cannot place: $want" >&2; exit 1; }
done
bash "$TMP/build.sh" "$CC" "$OPT"
