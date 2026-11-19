#!/usr/bin/env bash
# Build the E1 handling harness: the original race code with the handling probe and its own driver.
#   ref/handling_build.sh [CC] [OPT]      e.g. ref/handling_build.sh clang -O2
# Output: ref/build/<cc><opt>-handling/fzx-ref (add FZX_SOURCE_MACHINE_MENU=1 for --selection).
# ref/build.sh is run from a private, edited copy and is never changed: it and ref/main.c are
# inputs of asset receipts on the legacy line.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
sed -e "s|^ROOT=.*|ROOT=$ROOT|" \
    -e 's|^OUT=$ROOT/ref/build/${CC}${OPT}$|OUT=$ROOT/ref/build/${CC}${OPT}-handling|' \
    -e 's|^BRIDGES="bridge hooks"$|BRIDGES="bridge hooks handling_probe course_line"|' \
    -e 's|^DRIVER=main$|DRIVER=handling_main|' \
    "$ROOT/ref/build.sh" > "$TMP/build.sh"
for want in "ROOT=$ROOT" 'OUT=$ROOT/ref/build/${CC}${OPT}-handling' 'BRIDGES="bridge hooks handling_probe course_line"' 'DRIVER=handling_main'; do
    grep -qxF "$want" "$TMP/build.sh" || { echo "handling_build: ref/build.sh changed; cannot place: $want" >&2; exit 1; }
done
bash "$TMP/build.sh" "$CC" "$OPT"
