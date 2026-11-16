#!/usr/bin/env bash
# Build the G1.2 reference harness: the original race code, compiled natively in binary32 float.
#   ref/build.sh [CC] [OPT]      e.g. ref/build.sh clang -O2
# Output: ref/build/<cc><opt>/fzx-ref. Needs a 32-bit capable gcc or clang (gcc-multilib).
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O0}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=$ROOT/upstream/G-Diffuser/decomp
OUT=$ROOT/ref/build/${CC}${OPT}
CAMERA=${FZX_SOURCE_CAMERA:-0}
ROAD=${FZX_ROAD_EXPORT:-0}
[ "$ROAD" = 0 ] || [ "$ROAD" = 1 ] || { echo "invalid FZX_ROAD_EXPORT" >&2; exit 1; }
[ "$ROAD:$CAMERA" != 1:1 ] || { echo "road export is an offline tool, not a camera race" >&2; exit 1; }
[ "$ROAD" = 0 ] || OUT=$OUT-road
[ "$CAMERA" = 0 ] || [ "$CAMERA" = 1 ] || { echo "invalid FZX_SOURCE_CAMERA" >&2; exit 1; }
if [ "$CAMERA" = 1 ]; then
    export REF_STUB_CALLS=1
    ART=${REF_CAMERA_ART:-fixture}
    case "$ART" in fixture|retail) ;; *) echo "invalid REF_CAMERA_ART" >&2; exit 1;; esac
    OUT=$OUT-camera-$ART
    ASSETS=$ROOT/generated/camera_$ART
    bash "$ROOT/tools/check_receipt.sh" "$ASSETS/viewports.receipt"
fi
STUB=$OUT/stub
[ -f "$D/src/game/racer.c" ] || { echo "decomp submodule missing: git submodule update --init upstream/G-Diffuser && git -C upstream/G-Diffuser submodule update --init decomp" >&2; exit 1; }
mkdir -p "$STUB"
: > "$STUB/asset_syms.h"

# Stock US rev0: PORT and EXPANSION_KIT stay undefined so the fork's hooks compile out.
# SSE float with no contraction: binary32 results, no x87 extended precision, no fused multiply-add.
# -fno-unsafe-math-optimizations is needed beyond -fno-fast-math: without it clang 18 -O1+ rewrites
# float code in course.c and the start position moves by one ulp (found by ref/check.sh).
CFLAGS="-m32 -msse2 -mfpmath=sse -fno-fast-math -fno-unsafe-math-optimizations -ffp-contract=off -fno-strict-aliasing -fno-pic -fno-builtin -g $OPT -std=gnu11 -w
 -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration
 -D_LANGUAGE_C -DLANGUAGE_C -DF3DEX_GBI_2 -DNDEBUG -DVERSION_US=1 -DBUILD_VERSION=VERSION_I -DASSET_VERSION=us
 -DASSET_REVISION=rev0 -DLEO_VERSION=LEO_VERSION_A -DMFS_VERSION=MFS_VERSION_A -DNON_MATCHING -DAVOID_UB
 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -I$D/include -I$D/include/libultra -I$D/include/libc -I$D -I$D/src
 -I$ROOT/ref -I$STUB -include $ROOT/ref/overrides.h -include $STUB/asset_syms.h"
SOURCES="src/game/racer.c src/game/course.c src/game/course_gadgets.c src/sys/math.c src/game/EA90.c src/game/common.c src/sys/controller.c src/overlays/ovl_i2/course_context.c src/overlays/ovl_i3/cpu.c src/game/game_context.c src/game/course_gadget_context.c"

compile() { # Generated asset headers and asset symbols get empty stand-ins until the file compiles.
    local src=$1 obj=$2
    for _ in $(seq 1 60); do
        if $CC $CFLAGS -c "$src" -o "$obj" 2> "$obj.err"; then
            $CC $CFLAGS -E "$src" -o "${obj%.o}.i" 2>/dev/null
            return 0
        fi
        miss=$(grep -oE "fatal error: [^:]+: No such file|fatal error: '[^']+' file not found" "$obj.err" | head -1 |
               sed -E "s/fatal error: //; s/: No such file//; s/ file not found//; s/'//g" || true)
        if [ -n "$miss" ]; then mkdir -p "$STUB/$(dirname "$miss")"; : > "$STUB/$miss"; continue; fi
        # gcc: "error: 'name' undeclared"; clang: "use of undeclared identifier 'name'" (quote bytes vary by locale).
        names=$(LC_ALL=C sed -nE -e "s/.*error: [^A-Za-z_]*([A-Za-z_][A-Za-z0-9_]*)[^A-Za-z0-9_ ]+ undeclared.*/\1/p" \
                -e "s/.*undeclared identifier [^A-Za-z_]*([A-Za-z_][A-Za-z0-9_]*).*/\1/p" "$obj.err" | sort -u || true)
        [ -z "$names" ] && break
        for n in $names; do grep -q "\b$n\[\]" "$STUB/asset_syms.h" || echo "extern unsigned char $n[];" >> "$STUB/asset_syms.h"; done
    done
    cat "$obj.err" >&2
    return 1
}

BRIDGES="bridge hooks"
if [ "$CAMERA" = 1 ]; then
    CFLAGS="$CFLAGS -DFZX_SOURCE_CAMERA=1 -I$ASSETS"
    SOURCES="$SOURCES src/game/camera.c"
    BRIDGES="$BRIDGES camera_bridge"
fi
OBJS=""
for s in $SOURCES; do
    o=$OUT/$(basename "$s" .c).o
    compile "$D/$s" "$o"
    OBJS="$OBJS $o"
done
if [ "$ROAD" = 1 ]; then
    python3 "$ROOT/tools/road_bindings.py" "$STUB/asset_syms.h" "$STUB/road_bindings.h" "$D/include/fzx_segmentA.h"
    BRIDGES="$BRIDGES road_bridge"
fi
for s in $BRIDGES; do  # Bridge side: decomp headers.
    o=$OUT/ref_$s.o
    compile "$ROOT/ref/$s.c" "$o"
    OBJS="$OBJS $o"
done
# Driver side: host headers only.
DRIVER=main
[ "$ROAD" = 0 ] || DRIVER=road_main
$CC -m32 -fno-pic -O2 -g -std=c11 -Wall -Wextra -Werror -I"$ROOT/ref" -DFZX_SOURCE_CAMERA=$CAMERA -c "$ROOT/ref/$DRIVER.c" -o "$OUT/ref_main.o"
OBJS="$OBJS $OUT/ref_main.o"
python3 "$ROOT/ref/gen_stubs.py" "$OUT/stubs.c" "$OUT"/*.i -- $OBJS
$CC -m32 -fno-pic -O0 -w -c "$OUT/stubs.c" -o "$OUT/stubs.o"
$CC -m32 -no-pie $OBJS "$OUT/stubs.o" -lm -o "$OUT/fzx-ref"
echo "built $OUT/fzx-ref"
