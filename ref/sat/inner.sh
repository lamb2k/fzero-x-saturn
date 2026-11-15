#!/usr/bin/env bash
# Runs inside the Yaul image (ref/sat/build.sh). Builds the reference closure for the master
# SH-2 (ref/sat/closure.sh) and links the probe image. Arg: the per-script build folder (/work/...).
set -euo pipefail
OUT=$1
W=/work
bash "$W/ref/sat/closure.sh" "$OUT"
# Yaul does not track SAT_CLOSURE as a link input: clear the outputs so the image relinks.
rm -rf "$OUT/yaul" "$OUT"/fzx-ref-sat.*
make -C "$W/ref/sat" SAT_OUT="$OUT" SAT_CLOSURE="$OUT/ref_closure.o"
