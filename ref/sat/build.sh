#!/usr/bin/env bash
# Build the reference-on-SH-2 probe image for one fixture script.
#   ref/sat/build.sh [T02_full_throttle]     -> ref/sat/build/<script>/fzx-ref-sat.iso
# 1. Host: measure how much of each data stand-in the reference uses, size them, and
#    prove a relink with those sizes keeps every hash in expected/fixture.hashes.
# 2. Yaul chroot (tools/saturn_build.sh's image): cross-compile the same decomp sources,
#    ref/bridge.c and ref/hooks.c for SH-2 (soft-float binary32), link with main_sat.c.
# Needs root for the chroot (WSL default). Synthetic fixture only: no ROM is read.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
REF=$ROOT/ref
SCRIPT=${1:-T02_full_throttle}
IMG=${YAUL_IMAGE:-$HOME/yaul-image}
HOST=$REF/build/gcc-O0
OUT=$REF/sat/build/$SCRIPT
[ -f "$REF/inputs/$SCRIPT.txt" ] || { echo "no script $SCRIPT" >&2; exit 1; }
[ -x "$HOST/fzx-ref" ] || "$REF/build.sh" gcc -O0 >/dev/null
mkdir -p "$OUT"

# 1a. Measure: the full-size host reference reports the highest non-zero byte of every data
#     stand-in over T01 to T10. Each gets twice that (64 bytes minimum).
SMALL=$REF/sat/build/host-small
mkdir -p "$SMALL"
python3 "$REF/fixture_course.py" "$SMALL/course.bin"
rm -f "$SMALL/extents.txt"
while read -r name _; do
    REF_STUB_REPORT="$SMALL/extents.txt" "$HOST/fzx-ref" --course "$SMALL/course.bin" --byte-order big \
        --inputs "$REF/inputs/$name.txt" >/dev/null
done < "$REF/expected/fixture.hashes"
awk '{ if ($2 > m[$1]) m[$1] = $2; seen[$1] = 1 }
     END { for (k in seen) { s = 2 * m[k]; if (s < 64) s = 64; printf "%s 0x%X\n", k, int((s + 63) / 64) * 64 } }' \
    "$SMALL/extents.txt" | sort > "$SMALL/sizes.txt"
# 1b. Prove: the host reference relinked with exactly those sizes keeps every fixture hash.
objs=$(ls "$HOST"/*.o | grep -v '/stubs.o$')
REF_STUB_SIZES="$SMALL/sizes.txt" python3 "$REF/gen_stubs.py" "$SMALL/stubs.c" "$HOST"/*.i -- $objs >/dev/null
gcc -m32 -fno-pic -O0 -w -c "$SMALL/stubs.c" -o "$SMALL/stubs.o"
gcc -m32 -no-pie $objs "$SMALL/stubs.o" -lm -o "$SMALL/fzx-ref"
while read -r name want; do
    got=$("$SMALL/fzx-ref" --course "$SMALL/course.bin" --byte-order big --inputs "$REF/inputs/$name.txt" | awk '{print $4}')
    [ "$got" = "$want" ] || { echo "sized stubs change $name: $got != $want" >&2; exit 1; }
done < "$REF/expected/fixture.hashes"
echo "host: measured stand-in sizes keep all fixture hashes ($(awk '{s += strtonum($2)} END {print s}' "$SMALL/sizes.txt") bytes)"
want=$(awk -v n="$SCRIPT" '$1 == n {print $2}' "$REF/expected/fixture.hashes")

# 2. Data, then the SH-2 build in the Yaul image.
python3 "$REF/sat/gen_sat.py" "$OUT" "$REF/inputs/$SCRIPT.txt" "$SMALL/stubs.c"
mkdir -p "$IMG/work" "$IMG/proc" "$IMG/dev"
mountpoint -q "$IMG/work" || mount --bind "$ROOT" "$IMG/work"
mountpoint -q "$IMG/proc" || mount -t proc proc "$IMG/proc"
mountpoint -q "$IMG/dev" || mount --bind /dev "$IMG/dev"
chroot "$IMG" /bin/bash -c "source /yaul-env.sh && export PATH=\"\$YAUL_INSTALL_ROOT/bin:\$PATH\" && \
    bash /work/ref/sat/inner.sh /work/ref/sat/build/$SCRIPT"
echo "$want" > "$OUT/expected_hash.txt"
echo "built $OUT/fzx-ref-sat.iso (host hash $want)"
