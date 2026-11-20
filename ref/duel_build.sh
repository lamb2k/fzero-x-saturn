#!/usr/bin/env bash
# Build the duel reference (attacks lane): the original race code with two pad-driven racers.
#   ref/duel_build.sh [CC] [OPT]
# Output: ref/build/<cc><opt>-duel/fzx-duel. As ref/handling_build.sh, from a private edited copy of
# ref/build.sh, which is never changed.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
sed -e "s|^ROOT=.*|ROOT=$ROOT|" \
    -e 's|^OUT=$ROOT/ref/build/${CC}${OPT}$|OUT=$ROOT/ref/build/${CC}${OPT}-duel|' \
    -e 's|^BRIDGES="bridge hooks"$|BRIDGES="bridge hooks handling_probe course_line duel_probe rivals_bake"|' \
    -e 's|^DRIVER=main$|DRIVER=duel_main|' \
    "$ROOT/ref/build.sh" > "$TMP/build.sh"
for want in "ROOT=$ROOT" 'OUT=$ROOT/ref/build/${CC}${OPT}-duel' 'BRIDGES="bridge hooks handling_probe course_line duel_probe rivals_bake"' 'DRIVER=duel_main'; do
    grep -qxF "$want" "$TMP/build.sh" || { echo "duel_build: ref/build.sh changed; cannot place: $want" >&2; exit 1; }
done
bash "$TMP/build.sh" "$CC" "$OPT"
