#!/usr/bin/env bash
# G1.3 tick cost lane: count the float operations of every tick by kind and by function.
#   ref/cost.sh [OUT_DIR]        default: $FZX_EVIDENCE/cost (or ~/fzx-evidence/cost)
# Runs the reference (gcc -O0: no inlining, so each count lands on the decomp function that
# owns it; gcc -O2 as a cross-check of the total) under valgrind callgrind, one profile dump
# per ref_tick, on T01 to T10 and the synthetic course. Then ref/cost_model.py turns the
# dumps into the ranked table. Needs valgrind; HOST tier. Fixture-only: no ROM is read.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF=$ROOT/ref
OUT=${1:-${FZX_EVIDENCE:-$HOME/fzx-evidence}/cost}
command -v valgrind >/dev/null || { echo "ref/cost.sh: valgrind missing (apt-get install valgrind)" >&2; exit 1; }
mkdir -p "$OUT"
python3 "$REF/fixture_course.py" "$OUT/course.bin"
for opt in -O0 -O2; do
    [ -x "$REF/build/gcc$opt/fzx-ref" ] || "$REF/build.sh" gcc "$opt" >/dev/null
done
for opt in -O0 -O2; do
    bin=$REF/build/gcc$opt/fzx-ref
    objdump -d --no-show-raw-insn "$bin" > "$OUT/gcc$opt.dis"
    for script in "$REF"/inputs/T*.txt; do
        name=$(basename "$script" .txt)
        d=$OUT/gcc$opt/$name
        rm -rf "$d"; mkdir -p "$d"
        "$bin" --course "$OUT/course.bin" --byte-order big --inputs "$script" --trace "$d/trace.tsv" > "$d/run.txt"
        valgrind -q --tool=callgrind --dump-instr=yes --compress-strings=no --compress-pos=no \
            --dump-after=ref_tick --callgrind-out-file="$d/cg.%p" \
            "$bin" --course "$OUT/course.bin" --byte-order big --inputs "$script" > "$d/run_cg.txt"
        cmp -s "$d/run.txt" "$d/run_cg.txt" || { echo "ref/cost.sh: $name hash differs under valgrind" >&2; exit 1; }
        echo "gcc$opt $name $(cat "$d/run.txt")"
    done
done
python3 "$REF/cost_model.py" "$OUT" "$ROOT/docs/census/closure.tsv" \
    "$ROOT/third_party/SH2ZAM/saturn/benchmarks/math-model/results.json"
