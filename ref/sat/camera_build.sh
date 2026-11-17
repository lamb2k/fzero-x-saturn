#!/usr/bin/env bash
# Run in Yaul's environment after tools/camera_probe_assets.py. Synthetic only.
# Compile the common closure once; only the input data differs between probes.
set -euo pipefail
# Synthetic camera receipts use the legacy fixture tuning and stub layout.
export FZX_SOURCE_MACHINE_MENU=0
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ $# -gt 0 ] || set -- camera_controls
BASE=$ROOT/ref/sat/build/camera
SHARED=$BASE/shared
mkdir -p "$SHARED"
first=$1
for script in "$@"; do
    case "$script" in T[0-9][0-9]_*|camera_controls) ;; *) echo "invalid camera script" >&2; exit 1;; esac
    bash "$ROOT/tools/check_receipt.sh" "$BASE/$script/camera.receipt"
    cmp "$BASE/$first/sat_stubs.c" "$BASE/$script/sat_stubs.c"
    cmp "$BASE/$first/sat_stubs_data.s" "$BASE/$script/sat_stubs_data.s"
done
cp "$BASE/$first/sat_stubs.c" "$SHARED/sat_stubs.c"
cp "$BASE/$first/sat_stubs_data.s" "$SHARED/sat_stubs_data.s"
FZX_SOURCE_CAMERA=1 REF_CAMERA_ART=fixture FZX_FASTFLOAT=1 bash "$ROOT/ref/sat/closure.sh" "$SHARED"
for script in "$@"; do
    OUT=$BASE/$script
    "$YAUL_ARCH_SH_PREFIX-gcc" -m2 -mb -O2 -w -c "$OUT/sat_data.c" -o "$OUT/sat_data.o"
    "$YAUL_ARCH_SH_PREFIX-ld" -r -o "$OUT/ref_closure.o" "$SHARED/ref_closure.o" "$OUT/sat_data.o"
    make -B -C "$ROOT/ref/sat" SAT_OUT="$OUT" SAT_CLOSURE="$OUT/ref_closure.o" FZX_SOURCE_CAMERA=1
done
