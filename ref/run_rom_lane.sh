#!/bin/sh
# ROM lane of the reference (cards G1.2/G1.6): Mute City 1 with retail Blue Falcon tuning.
# Needs FZX_TEST_ROM (a US rev0 ROM) and FZX_EVIDENCE (a folder outside git). Everything this
# writes is retail-derived and stays in $FZX_EVIDENCE/ref: course record, recorded inputs,
# traces. Only hashes and event counts may be copied into the repository.
# Checks: the four builds agree on every M trace; no run is fed by a stub of game logic.
set -eu
cd "$(dirname "$0")/.."
: "${FZX_TEST_ROM:?set FZX_TEST_ROM to a US rev0 ROM}"
: "${FZX_EVIDENCE:?set FZX_EVIDENCE to a folder outside the repository}"
OUT="$FZX_EVIDENCE/ref"
mkdir -p "$OUT"
python3 - "$FZX_TEST_ROM" "$OUT/mute-city-1.bin" <<'PY'
import sys
sys.path.insert(0, "tools")
import import_course as imp
record, _ = imp.extract_rom_course(imp.normalize_rom(open(sys.argv[1], "rb").read())[0], "mute-city-1")
imp.parse_record(record, "big")  # checksum and ranges
open(sys.argv[2], "wb").write(record)
PY
BUILDS="gcc:-O2 gcc:-O0 clang:-O2 clang:-O0"
for b in $BUILDS; do make -s -C ref CC="${b%%:*}" OPT="${b#*:}" >/dev/null; done
fail=0
printf 'trace\tticks\thash\tevents\n' > "$OUT/summary.tsv"
for src in ref/traces/rom/M*.txt; do
    m=$(basename "$src" .txt)
    # Resolve bot steering once (gcc -O2), then replay the plain inputs on every build.
    build/ref/gcc-O2/ref --course "$OUT/mute-city-1.bin" --rom "$FZX_TEST_ROM" --script "$src" \
        --trace "$OUT/$m-record.tsv" --record "$OUT/$m.txt" --bot 1,0.5,60 --stop-at-finish 1 \
        >/dev/null 2>"$OUT/$m-record.err" || { echo "ref: FAIL $m invalid: $(grep INVALID "$OUT/$m-record.err")"; fail=1; continue; }
    first=""
    for b in $BUILDS; do
        bin="${b%%:*}${b#*:}"
        got=$("build/ref/$bin/ref" --course "$OUT/mute-city-1.bin" --rom "$FZX_TEST_ROM" \
            --script "$OUT/$m.txt" --trace "$OUT/$m-$bin.tsv" --stop-at-finish 1 2>/dev/null | awk '{print $3" "$5}')
        if [ -z "$first" ]; then first=$got; elif [ "$got" != "$first" ]; then
            echo "ref: FAIL determinism $m: $bin $got vs gcc-O2 $first"; fail=1
        fi
    done
    events=$(python3 ref/trace_events.py "$OUT/$m-gcc-O2.tsv" | tr '\n' ' ')
    printf '%s\t%s\t%s\t%s\n' "$m" "${first% *}" "${first#* }" "$events" >> "$OUT/summary.tsv"
    echo "$m ${first} | $events"
done
[ "$fail" -eq 0 ] && echo "ref: ROM lane PASS (Mute City 1, retail tuning, 4 builds identical)"
exit "$fail"
