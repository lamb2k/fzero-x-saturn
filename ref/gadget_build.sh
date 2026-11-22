#!/usr/bin/env bash
# HOST build of the course gadget dumper (diagnostic, claude/gadgets).
#   ref/gadget_build.sh [CC] [OPT]
# Builds the reference closure (no camera) plus ref/gadget_bake.c (decomp side) and ref/gadget_main.c (host side).
# Output: ref/build/gadgets-<cc><opt>/fzx-gadgets.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O2}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=$ROOT/upstream/G-Diffuser/decomp
OUT=$ROOT/ref/build/gadgets-${CC}${OPT}
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
for s in bridge hooks gadget_bake; do o=$OUT/ref_$s.o; compile "$ROOT/ref/$s.c" "$o"; OBJS="$OBJS $o"; done
HOST="-m32 -fno-pic -O2 -g -std=gnu11 -Wall -Wextra -Werror -I$ROOT/ref"
$CC $HOST -c "$ROOT/ref/gadget_main.c" -o "$OUT/gadget_main.o"
OBJS="$OBJS $OUT/gadget_main.o"
python3 "$ROOT/ref/gen_stubs.py" "$OUT/stubs.c" "$OUT"/*.i -- $OBJS > "$OUT/stubs.log"
$CC -m32 -fno-pic -O0 -w -c "$OUT/stubs.c" -o "$OUT/stubs.o"
$CC -m32 -no-pie $OBJS "$OUT/stubs.o" -lm -o "$OUT/fzx-gadgets"
echo "built $OUT/fzx-gadgets"
