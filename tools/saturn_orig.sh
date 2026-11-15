#!/usr/bin/env bash
# Build the game image with FZX_SIM=orig: the original race code (the decompilation, compiled
# for the master SH-2 with the bit-exact float library) drives the craft; the renderer follows it.
#   tools/saturn_orig.sh [RECORD.bin --byte-order big|little]   -> saturn/build-orig/fzero-x-saturn.iso
# With no record, the course is the synthetic CI fixture (ref/fixture_course.py): no ROM read.
# 1. Host: stand-in sizes measured and proved (ref/sat/host_stubs.sh); the course imported
#    into generated/ (header for the renderer, record for the original code, receipt).
# 2. Yaul image: the closure (ref/sat/closure.sh), then saturn/ with FZX_SIM=orig.
# Needs root for the chroot (WSL default), like tools/saturn_build.sh.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${YAUL_IMAGE:-$HOME/yaul-image}
OUT=$ROOT/saturn/build-orig
[ -f "$IMG/yaul-env.sh" ] || { echo "no Yaul image at $IMG: run tools/fetch_yaul.py $IMG" >&2; exit 1; }

"$ROOT/ref/sat/host_stubs.sh"
mkdir -p "$OUT" "$ROOT/generated"
if [ $# -eq 0 ]; then
    python3 "$ROOT/ref/fixture_course.py" "$OUT/course.bin"
    set -- "$OUT/course.bin" --byte-order big
fi
python3 "$ROOT/tools/import_course.py" --record "$1" "${@:2}" \
    --output "$ROOT/generated/course.h" --record-output "$ROOT/generated/course_record.c" >/dev/null
python3 "$ROOT/ref/sat/gen_sat.py" --stubs "$OUT" "$ROOT/ref/sat/build/host-small/stubs.c"

mkdir -p "$IMG/work" "$IMG/proc" "$IMG/dev"
mountpoint -q "$IMG/work" || mount --bind "$ROOT" "$IMG/work"
mountpoint -q "$IMG/proc" || mount -t proc proc "$IMG/proc"
mountpoint -q "$IMG/dev" || mount --bind /dev "$IMG/dev"
# Yaul does not track the closure as a link input: drop the linked outputs so the image relinks.
chroot "$IMG" /bin/bash -c "source /yaul-env.sh && export PATH=\"\$YAUL_INSTALL_ROOT/bin:\$PATH\" && \
    FZX_FASTFLOAT=1 bash /work/ref/sat/closure.sh /work/saturn/build-orig && \
    rm -f /work/saturn/build-orig/yaul/fzero-x-saturn.elf /work/saturn/build-orig/fzero-x-saturn.* && \
    make -C /work/saturn FZX_SIM=orig FZX_IMPORTED=1 FZX_CLOSURE=/work/saturn/build-orig/ref_closure.o"
echo "built $ROOT/saturn/build-orig/fzero-x-saturn.iso (FZX_SIM=orig)"
