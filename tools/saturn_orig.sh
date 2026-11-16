#!/usr/bin/env bash
# Build the game image with FZX_SIM=orig: the original race code (the decompilation, compiled
# for the master SH-2 with the bit-exact float library) drives the craft; the renderer follows it.
#   tools/saturn_orig.sh [--rom ROM | RECORD.bin --byte-order big|little]
#       -> saturn/build-orig/fzero-x-saturn.iso
# With no argument, the course is the synthetic CI fixture (ref/fixture_course.py) and the machine
# constants are the CI fixture pattern: no ROM is read. --rom ROM uses Mute City 1 and the real
# machine constants from the owner's ROM.
# 1. Host: stand-in sizes measured and proved (ref/sat/host_stubs.sh); the course imported
#    into generated/ (header for the renderer, record for the original code, receipt).
# 2. Yaul image: the closure (ref/sat/closure.sh), then saturn/ with FZX_SIM=orig.
# Needs root for the chroot (WSL default), like tools/saturn_build.sh.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${YAUL_IMAGE:-$HOME/yaul-image}
OUT=$ROOT/saturn/build-orig
CAMERA=${FZX_SOURCE_CAMERA:-0}
STUB_DIR=$ROOT/ref/sat/build/host-small
ART=fixture
if [ "$CAMERA" = 1 ]; then
    if [ "${1:-}" = --rom ]; then ART=retail; export FZX_TEST_ROM="$2"; fi
    export FZX_SOURCE_CAMERA=1 REF_CAMERA_ART=$ART
    python3 "$ROOT/tools/camera_assets.py" --art "$ART"
    OUT=$ROOT/saturn/build-orig-camera
    STUB_DIR=$STUB_DIR-camera-$ART
fi
[ -f "$IMG/yaul-env.sh" ] || { echo "no Yaul image at $IMG: run tools/fetch_yaul.py $IMG" >&2; exit 1; }

mkdir -p "$OUT" "$ROOT/generated"
if [ "${1:-}" = --rom ]; then
    # Size the stand-ins on the ROM's course too (the record stays in the git-ignored build folder).
    python3 - "$ROOT/tools" "$2" "$OUT/course-rom.bin" <<'PY'
import sys
sys.path.insert(0, sys.argv[1])
import import_course
with open(sys.argv[2], "rb") as rom, open(sys.argv[3], "wb") as out:
    out.write(import_course.extract_rom_course(rom.read())[0])
PY
    REF_STUB_EXTRA_COURSE="$OUT/course-rom.bin" REF_STUB_ROM="$2" "$ROOT/ref/sat/host_stubs.sh"
else
    "$ROOT/ref/sat/host_stubs.sh"
fi
ROM_BOOT=0
rm -f "$ROOT/generated/rom_boot.c"
if [ "${1:-}" = --rom ]; then
    # The owner's ROM: Mute City 1 and the boot area (the real machine constants). Both land in
    # git-ignored generated/; nothing is printed.
    python3 "$ROOT/tools/import_course.py" --rom "$2" --output "$ROOT/generated/course.h" \
        --record-output "$ROOT/generated/course_record.c" --boot-output "$ROOT/generated/rom_boot.c" >/dev/null
    ROM_BOOT=1
else
    if [ $# -eq 0 ]; then
        python3 "$ROOT/ref/fixture_course.py" "$OUT/course.bin"
        set -- "$OUT/course.bin" --byte-order big
    fi
    python3 "$ROOT/tools/import_course.py" --record "$1" "${@:2}" \
        --output "$ROOT/generated/course.h" --record-output "$ROOT/generated/course_record.c" >/dev/null
fi
python3 "$ROOT/ref/sat/gen_sat.py" --stubs "$OUT" "$STUB_DIR/stubs.c"

mkdir -p "$IMG/work" "$IMG/proc" "$IMG/dev"
mountpoint -q "$IMG/work" || mount --bind "$ROOT" "$IMG/work"
mountpoint -q "$IMG/proc" || mount -t proc proc "$IMG/proc"
mountpoint -q "$IMG/dev" || mount --bind /dev "$IMG/dev"
if [ "$CAMERA" = 1 ]; then
    chroot "$IMG" /bin/bash -c "source /yaul-env.sh && export PATH=\"\$YAUL_INSTALL_ROOT/bin:\$PATH\" && \
        FZX_FASTFLOAT=1 FZX_SOURCE_CAMERA=1 REF_CAMERA_ART=$ART bash /work/ref/sat/closure.sh /work/saturn/build-orig-camera && \
        make -B -C /work/saturn FZX_SIM=orig FZX_IMPORTED=1 FZX_SOURCE_CAMERA=1 \
          FZX_CLOSURE=/work/saturn/build-orig-camera/ref_closure.o FZX_ROM_BOOT=$ROM_BOOT FZX_SOURCE_HUD=${FZX_SOURCE_HUD:-0}"
    echo "built source-camera original-race image"
    exit 0
fi
# Yaul does not track the closure as a link input: drop the linked outputs so the image relinks.
chroot "$IMG" /bin/bash -c "source /yaul-env.sh && export PATH=\"\$YAUL_INSTALL_ROOT/bin:\$PATH\" && \
    FZX_FASTFLOAT=1 bash /work/ref/sat/closure.sh /work/saturn/build-orig && \
    rm -f /work/saturn/build-orig/yaul/@work@saturn@main.o /work/saturn/build-orig/yaul/fzero-x-saturn.elf /work/saturn/build-orig/fzero-x-saturn.* && \
    make -C /work/saturn FZX_SIM=orig FZX_IMPORTED=1 FZX_CLOSURE=/work/saturn/build-orig/ref_closure.o FZX_ROM_BOOT=$ROM_BOOT"
echo "built $ROOT/saturn/build-orig/fzero-x-saturn.iso (FZX_SIM=orig)"
