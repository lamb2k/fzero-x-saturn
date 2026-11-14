#!/bin/sh
# Reference corpus gate (card G1.2). Builds the float reference four ways (GCC and Clang,
# -O0 and -O2), runs T01..T10 on the synthetic fixture course with each, and requires:
#   1. all four builds give the same run hash for every trace (determinism);
#   2. the hashes equal ref/traces/expected.tsv (no silent drift);
#   3. control: one bit of fixture machine tuning changes T02's hash;
#   4. control (G1.1): racer.c compiled with PORT defined changes the hashes.
# Usage: ref/run_corpus.sh [--update]   (--update rewrites expected.tsv after a reviewed change)
# Needs the decomp submodule, gcc-multilib and clang. Outputs go to build/ref/.
set -eu
cd "$(dirname "$0")/.."
UPDATE=${1:-}
FX=build/ref/fx
mkdir -p "$FX"
python3 ref/make_fixture.py "$FX/fixture.bin" >/dev/null

BUILDS="gcc:-O2 gcc:-O0 clang:-O2 clang:-O0"
for b in $BUILDS; do
    make -s -C ref CC="${b%%:*}" OPT="${b#*:}" >/dev/null
done

run() { # build trace [extra args]
    bin=$1; trace=$2; shift 2
    "build/ref/$bin/ref" --course "$FX/fixture.bin" --script "ref/traces/$trace.txt" \
        --trace "$FX/$trace-$bin.tsv" "$@" 2>/dev/null | awk '{print $3" "$5}'
}

fail=0
out="$FX/actual.tsv"
printf 'trace\tticks\thash\n' > "$out"
for t in T01 T02 T03 T04 T05 T06 T07 T08 T09 T10; do
    first=""
    for b in $BUILDS; do
        bin="${b%%:*}${b#*:}"
        got=$(run "$bin" "$t")
        if [ -z "$first" ]; then first=$got; elif [ "$got" != "$first" ]; then
            echo "ref: FAIL determinism $t: $bin gives $got, gcc-O2 gives $first"; fail=1
        fi
    done
    printf '%s\t%s\t%s\n' "$t" "${first% *}" "${first#* }" >> "$out"
done

if [ "$UPDATE" = "--update" ]; then
    cp "$out" ref/traces/expected.tsv
    echo "ref: expected.tsv updated"
elif ! diff -u ref/traces/expected.tsv "$out"; then
    echo "ref: FAIL hashes differ from ref/traces/expected.tsv"; fail=1
fi

base=$(run gcc-O2 T02)
flip=$(run gcc-O2 T02 --rom-word 20202021)
if [ "$base" = "$flip" ]; then
    echo "ref: FAIL control: a tuning change did not change T02"; fail=1
fi

make -s -C ref PORT_RACER=1 >/dev/null
for t in T01 T02 T09; do
    if [ "$(run gcc-O2-portracer "$t")" = "$(run gcc-O2 "$t")" ]; then
        echo "ref: FAIL control: PORT in racer.c did not change $t"; fail=1
    fi
done

if [ "$fail" -ne 0 ]; then exit 1; fi
echo "ref: PASS 10 traces x 4 builds identical, expected hashes match, both controls fail as required"
