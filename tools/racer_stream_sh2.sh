#!/bin/sh
# Link SH2ZAM sprite-cache-1's cell decoder and the plain vlc-2 baseline for
# tools/sh2_cycle_model.py.
# Usage: tools/racer_stream_sh2.sh SH2ZAM_SATURN_DIR OUT_DIR [CC]
# Writes OUT_DIR/cell4z.bin (flat image at 0x06004000) and OUT_DIR/cell4z.map
# (global function addresses).
set -eu
src=${1:?SH2ZAM saturn directory}
out=${2:?output directory}
cc=${3:-sh2eb-elf-gcc}
mkdir -p "$out"
"$cc" -m2 -mb -O2 -ffreestanding -std=c11 -Wall -Wextra -Werror -DSH2Z_REQUIRE_SH2=1 -I"$src/include" \
    -nostdlib -Wl,-Ttext=0x06004000 -Wl,-e,_sh2z_cell4z_decode \
    "$src/src/sh2zam_sprite_cache.c" "$src/src/sh2zam_vlc2.c" "$(dirname "$0")/racer_stream_baseline.c" \
    -lgcc -o "$out/cell4z.elf"
"${cc%gcc}objcopy" -O binary "$out/cell4z.elf" "$out/cell4z.bin"
"${cc%gcc}nm" "$out/cell4z.elf" | awk '$2 == "T" { print $3, "0x" $1 }' > "$out/cell4z.map"
cat "$out/cell4z.map"
