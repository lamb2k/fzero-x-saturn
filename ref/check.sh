#!/usr/bin/env bash
# G1.2 checks for the reference harness, on the synthetic course and the fixture machine.
#   ref/check.sh            build gcc/clang x -O0/-O2, run every input script, compare
#   ref/check.sh --update   rewrite ref/expected/fixture.hashes from the gcc -O0 run
# 1. Determinism: the four builds give the same trace hash for every script.
# 2. Regression: the hashes equal ref/expected/fixture.hashes.
# 3. Restart: a restarted race replays as a fresh one.
# 4. Control: one changed tuning constant (the energy-drain path, through a one-byte change
#    of the fixture machine) changes the hash of a script that exercises it, so the hash
#    is not blind to the physics.
set -euo pipefail
# These fixed regression hashes belong to the legacy synthetic race arm.
export FZX_SOURCE_MACHINE_MENU=0
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF=$ROOT/ref
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 "$REF/fixture_course.py" "$TMP/course.bin"

configs="gcc:-O0 gcc:-O2 clang:-O0 clang:-O2"
command -v clang >/dev/null || { echo "check: clang not found; gcc only" >&2; configs="gcc:-O0 gcc:-O2"; }
for c in $configs; do
    bash "$REF/build.sh" "${c%%:*}" "${c##*:}" > "$TMP/build-${c%%:*}${c##*:}.log" 2>&1 ||
        { tail -40 "$TMP/build-${c%%:*}${c##*:}.log"; exit 1; }
done

fail=0
: > "$TMP/hashes"
for script in "$REF"/inputs/T*.txt; do
    name=$(basename "$script" .txt)
    first=""
    for c in $configs; do
        h=$("$REF/build/${c%%:*}${c##*:}/fzx-ref" --course "$TMP/course.bin" --byte-order big --inputs "$script" | awk '{print $4}')
        if [ -z "$first" ]; then first=$h; echo "$name $h" >> "$TMP/hashes";
        elif [ "$h" != "$first" ]; then echo "DIVERGE $name ${c}: $h != $first"; fail=1; fi
    done
    echo "$name $first"
done

# Little-endian input of the same record must give the same simulation.
python3 - "$TMP/course.bin" "$TMP/course_le.bin" <<'P'
import struct, sys
b = bytearray(open(sys.argv[1], "rb").read())
def sw(off, n):  # reverse each n-byte field
    b[off:off + n] = b[off:off + n][::-1]
sw(4, 4)
for i in range(64):
    base = 0x20 + i * 20
    for k in range(3): sw(base + 4 * k, 4)
    sw(base + 12, 2); sw(base + 14, 2); sw(base + 16, 4)
    sw(0x520 + i * 2, 2)
open(sys.argv[2], "wb").write(b)
P
le=$("$REF/build/gcc-O0/fzx-ref" --course "$TMP/course_le.bin" --byte-order little --inputs "$REF/inputs/T02_full_throttle.txt" | awk '{print $4}')
be=$(awk '$1=="T02_full_throttle"{print $2}' "$TMP/hashes")
[ "$le" = "$be" ] && echo "byte order: same hash" || { echo "byte order: DIFFER $le != $be"; fail=1; }

# Restart: a race run, then put back on the grid (ref_race_restart), must replay a script
# exactly as a fresh race does (the Saturn image restarts this way, without Course_Init).
for pre in T10_random T06_boost; do
    for name in T02_full_throttle T05_slalom T08_side_attack; do
        h=$("$REF/build/gcc-O0/fzx-ref" --course "$TMP/course.bin" --byte-order big             --inputs "$REF/inputs/$name.txt" --prefix "$REF/inputs/$pre.txt" | awk '{print $4}')
        want=$(awk -v n="$name" '$1==n{print $2}' "$TMP/hashes")
        [ "$h" = "$want" ] || { echo "restart after $pre: $name DIFFERS $h != $want"; fail=1; }
    done
done
echo "restart: replays as a fresh race"

# Control: a different machine constant must move the hash.
python3 - "$TMP/rom.bin" <<'P'
import sys
boot = bytearray(((i * 151 + 77) & 0xFF) for i in range(0x1000))
boot[0:4] = b"\x80\x37\x12\x40"   # z64 magic, so the driver accepts it
open(sys.argv[1] + ".base", "wb").write(boot)
boot[0x50D] ^= 0x10  # word 0x50C bit 20: inside the (>> 0x13) & 0xFF field Racer_InitMachineStats reads
open(sys.argv[1], "wb").write(boot)
P
c0=$("$REF/build/gcc-O0/fzx-ref" --course "$TMP/course.bin" --byte-order big --inputs "$REF/inputs/T02_full_throttle.txt" --rom "$TMP/rom.bin.base" | awk '{print $4}')
c1=$("$REF/build/gcc-O0/fzx-ref" --course "$TMP/course.bin" --byte-order big --inputs "$REF/inputs/T02_full_throttle.txt" --rom "$TMP/rom.bin" | awk '{print $4}')
[ "$c0" != "$c1" ] && echo "control: hash moved ($c0 -> $c1)" || { echo "control: FAILED, the hash did not move"; fail=1; }

if [ "${1:-}" = "--update" ]; then
    mkdir -p "$REF/expected"
    cp "$TMP/hashes" "$REF/expected/fixture.hashes"
    echo "updated ref/expected/fixture.hashes"
elif ! diff -u "$REF/expected/fixture.hashes" "$TMP/hashes"; then
    echo "regression: hashes differ from ref/expected/fixture.hashes"; fail=1
else
    echo "regression: hashes match ref/expected/fixture.hashes"
fi
[ $fail -eq 0 ] && echo "ref/check.sh: PASS" || { echo "ref/check.sh: FAIL"; exit 1; }
