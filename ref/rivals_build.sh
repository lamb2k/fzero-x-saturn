#!/usr/bin/env bash
# HOST build of the rival line baker and model check (diagnostic, claude/rivals-native).
#   ref/rivals_build.sh [CC] [OPT] SH2ZAM_SATURN_DIR
# Builds the reference closure (no camera) plus ref/rivals_bake.c (decomp side) and ref/rivals_main.c
# (host side) with the SH2ZAM line follower. Output: ref/build/rivals-<cc><opt>/fzx-rivals.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
SZ=${3:?SH2ZAM saturn dir with the line follower}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=$ROOT/upstream/G-Diffuser/decomp
OUT=$ROOT/ref/build/rivals-${CC}${OPT}
STUB=$OUT/stub
[ -f "$D/src/game/racer.c" ] || { echo "decomp submodule missing" >&2; exit 1; }
mkdir -p "$STUB"
: > "$STUB/asset_syms.h"
CFLAGS="-m32 -msse2 -mfpmath=sse -fno-fast-math -fno-unsafe-math-optimizations -ffp-contract=off -fno-strict-aliasing -fno-pic -fno-builtin -g $OPT -std=gnu11 -w
 -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration
 -D_LANGUAGE_C -DLANGUAGE_C -DF3DEX_GBI_2 -DNDEBUG -DVERSION_US=1 -DBUILD_VERSION=VERSION_I -DASSET_VERSION=us
 -DASSET_REVISION=rev0 -DLEO_VERSION=LEO_VERSION_A -DMFS_VERSION=MFS_VERSION_A -DNON_MATCHING -DAVOID_UB
 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -I$D/include -I$D/include/libultra -I$D/include/libc -I$D -I$D/src
 -I$ROOT/ref -I$STUB -include $ROOT/ref/overrides.h -include $STUB/asset_syms.h"
SOURCES="src/game/racer.c src/game/course.c src/game/course_gadgets.c src/sys/math.c src/game/EA90.c src/game/common.c src/sys/controller.c src/overlays/ovl_i2/course_context.c src/overlays/ovl_i3/cpu.c src/game/game_context.c src/game/course_gadget_context.c"
compile() {
    local src=$1 obj=$2
    for _ in $(seq 1 60); do
        if $CC $CFLAGS -c "$src" -o "$obj" 2> "$obj.err"; then $CC $CFLAGS -E "$src" -o "${obj%.o}.i" 2>/dev/null; return 0; fi
        miss=$(grep -oE "fatal error: [^:]+: No such file|fatal error: '[^']+' file not found" "$obj.err" | head -1 |
               sed -E "s/fatal error: //; s/: No such file//; s/ file not found//; s/'//g" || true)
        if [ -n "$miss" ]; then mkdir -p "$STUB/$(dirname "$miss")"; : > "$STUB/$miss"; continue; fi
        names=$(LC_ALL=C sed -nE -e "s/.*error: [^A-Za-z_]*([A-Za-z_][A-Za-z0-9_]*)[^A-Za-z0-9_ ]+ undeclared.*/\1/p" \
                -e "s/.*undeclared identifier [^A-Za-z_]*([A-Za-z_][A-Za-z0-9_]*).*/\1/p" "$obj.err" | sort -u || true)
        [ -z "$names" ] && break
        for n in $names; do grep -q "\b$n\[\]" "$STUB/asset_syms.h" || echo "extern unsigned char $n[];" >> "$STUB/asset_syms.h"; done
    done
    cat "$obj.err" >&2; return 1
}
OBJS=""
for s in $SOURCES; do o=$OUT/$(basename "$s" .c).o; compile "$D/$s" "$o"; OBJS="$OBJS $o"; done
for s in bridge hooks rivals_bake course_line; do o=$OUT/ref_$s.o; compile "$ROOT/ref/$s.c" "$o"; OBJS="$OBJS $o"; done
HOST="-m32 -fno-pic -O2 -g -std=gnu11 -Wall -Wextra -Werror -I$ROOT/ref -I$SZ/include"
$CC $HOST -c "$ROOT/ref/rivals_main.c" -o "$OUT/rivals_main.o"
$CC $HOST -c "$SZ/src/sh2zam_line_follower.c" -o "$OUT/sh2zam_line_follower.o"
OBJS="$OBJS $OUT/rivals_main.o $OUT/sh2zam_line_follower.o"
python3 "$ROOT/ref/gen_stubs.py" "$OUT/stubs.c" "$OUT"/*.i -- $OBJS > "$OUT/stubs.log"
$CC -m32 -fno-pic -O0 -w -c "$OUT/stubs.c" -o "$OUT/stubs.o"
$CC -m32 -no-pie $OBJS "$OUT/stubs.o" -lm -o "$OUT/fzx-rivals"
echo "built $OUT/fzx-rivals"
# Reference Grand Prix (original code, 30 racers): same closure, its own driver.
GP_OBJS=$(echo "$OBJS" | sed "s# $OUT/rivals_main.o##; s# $OUT/sh2zam_line_follower.o##")
$CC -m32 -fno-pic -O2 -g -std=gnu11 -Wall -Wextra -Werror -I"$ROOT/ref" -c "$ROOT/ref/gp_ref_main.c" -o "$OUT/gp_ref_main.o"
mkdir -p "$OUT/gp"
python3 "$ROOT/ref/gen_stubs.py" "$OUT/gp/stubs.c" "$OUT"/*.i -- $GP_OBJS "$OUT/gp_ref_main.o" > "$OUT/gp/stubs.log"
$CC -m32 -fno-pic -O0 -w -c "$OUT/gp/stubs.c" -o "$OUT/gp/stubs.o"
$CC -m32 -no-pie $GP_OBJS "$OUT/gp_ref_main.o" "$OUT/gp/stubs.o" -lm -o "$OUT/fzx-gp-ref"
echo "built $OUT/fzx-gp-ref"
# Native field on HOST: SH2ZAM only, no original game code.
$CC -m32 -O2 -g -std=gnu11 -Wall -Wextra -Werror -I"$SZ/include" "$ROOT/ref/rivals_gp_main.c" "$SZ/src/sh2zam_line_follower.c" "$SZ/src/sh2zam_line_field.c" -lm -o "$OUT/fzx-rivals-gp"
echo "built $OUT/fzx-rivals-gp"
