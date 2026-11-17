#!/usr/bin/env bash
# Build the reference-on-SH-2 probe image for one fixture script.
#   ref/sat/build.sh [T02_full_throttle]     -> ref/sat/build/<script>/fzx-ref-sat.iso
# 1. Host: measure how much of each data stand-in the reference uses, size them, and
#    prove a relink with those sizes keeps every hash in expected/fixture.hashes.
# 2. Yaul chroot (tools/saturn_build.sh's image): cross-compile the same decomp sources,
#    ref/bridge.c and ref/hooks.c for SH-2 (soft-float binary32), link with main_sat.c.
# Needs root for the chroot (WSL default). Synthetic fixture only: no ROM is read.
set -euo pipefail
# This probe compares the legacy synthetic fixture hashes, not menu tuning.
export FZX_SOURCE_MACHINE_MENU=0
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
REF=$ROOT/ref
SCRIPT=${1:-T02_full_throttle}
IMG=${YAUL_IMAGE:-$HOME/yaul-image}
HOST=$REF/build/gcc-O0
OUT=$REF/sat/build/$SCRIPT
[ -f "$REF/inputs/$SCRIPT.txt" ] || { echo "no script $SCRIPT" >&2; exit 1; }
[ -x "$HOST/fzx-ref" ] || "$REF/build.sh" gcc -O0 >/dev/null
mkdir -p "$OUT"

# 1. Stand-in sizes measured and proved on the host (ref/sat/host_stubs.sh).
SMALL=$REF/sat/build/host-small
"$REF/sat/host_stubs.sh"
want=$(awk -v n="$SCRIPT" '$1 == n {print $2}' "$REF/expected/fixture.hashes")

# 2. Data, then the SH-2 build in the Yaul image.
python3 "$REF/sat/gen_sat.py" "$OUT" "$REF/inputs/$SCRIPT.txt" "$SMALL/stubs.c"
mkdir -p "$IMG/work" "$IMG/proc" "$IMG/dev"
mountpoint -q "$IMG/work" || mount --bind "$ROOT" "$IMG/work"
mountpoint -q "$IMG/proc" || mount -t proc proc "$IMG/proc"
mountpoint -q "$IMG/dev" || mount --bind /dev "$IMG/dev"
chroot "$IMG" /bin/bash -c "source /yaul-env.sh && export PATH=\"\$YAUL_INSTALL_ROOT/bin:\$PATH\" && \
    FZX_FASTFLOAT=${FZX_FASTFLOAT:-0} bash /work/ref/sat/inner.sh /work/ref/sat/build/$SCRIPT"
echo "$want" > "$OUT/expected_hash.txt"
echo "built $OUT/fzx-ref-sat.iso (host hash $want)"
