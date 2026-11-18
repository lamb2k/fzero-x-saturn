#!/usr/bin/env bash
# Simulation-lane HOST build: the reference closure with the source camera (as ref/build.sh with
# FZX_SOURCE_CAMERA=1), plus the sim lane's knobs and the differential-harness driver.
#   ref/sim_build.sh [CC] [OPT]          e.g. ref/sim_build.sh clang -O2
# Environment (all default 0 / fixture):
#   REF_CAMERA_ART=fixture|retail     camera viewport assets, as ref/build.sh
#   FZX_SOURCE_MACHINE_MENU=0|1       explicit machine/skin/engine selection (--select)
#   FZX_DEFER_PROJECTION=0|1          quick win 1 (ref/sim_overrides.h)
#   FZX_SIM_CAMERA=0|1                fixed-point camera linked (src/sim/fzx_sim_camera.c);
#                                     the harness chooses REF or FIX at run time
# Output: ref/build/sim-<cc><opt>-<art>[-mm][-defer][-cam]/fzx-sim (driver ref/sim_main.c).
# ref/build.sh stays the receipt-bound reference build; this script never writes its outputs.
set -euo pipefail
CC=${1:-gcc}
OPT=${2:--O0}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
D=$ROOT/upstream/G-Diffuser/decomp
ART=${REF_CAMERA_ART:-fixture}
MM=${FZX_SOURCE_MACHINE_MENU:-0}
DEFER=${FZX_DEFER_PROJECTION:-0}
SIMCAM=${FZX_SIM_CAMERA:-0}
case "$ART" in fixture|retail) ;; *) echo "invalid REF_CAMERA_ART" >&2; exit 1;; esac
for v in "$MM" "$DEFER" "$SIMCAM"; do case "$v" in 0|1) ;; *) echo "sim knobs are 0 or 1" >&2; exit 1;; esac; done
OUT=$ROOT/ref/build/sim-${CC}${OPT}-$ART
[ "$MM" = 0 ] || OUT=$OUT-mm
[ "$DEFER" = 0 ] || OUT=$OUT-defer
[ "$SIMCAM" = 0 ] || OUT=$OUT-cam
ASSETS=$ROOT/generated/camera_$ART
bash "$ROOT/tools/check_receipt.sh" "$ASSETS/viewports.receipt"
export REF_STUB_CALLS=1
STUB=$OUT/stub
[ -f "$D/src/game/racer.c" ] || { echo "decomp submodule missing" >&2; exit 1; }
mkdir -p "$STUB"
: > "$STUB/asset_syms.h"

# Same float discipline as ref/build.sh: SSE binary32, no contraction, no unsafe math.
CFLAGS="-m32 -msse2 -mfpmath=sse -fno-fast-math -fno-unsafe-math-optimizations -ffp-contract=off -fno-strict-aliasing -fno-pic -fno-builtin -g $OPT -std=gnu11 -w
 -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration
 -D_LANGUAGE_C -DLANGUAGE_C -DF3DEX_GBI_2 -DNDEBUG -DVERSION_US=1 -DBUILD_VERSION=VERSION_I -DASSET_VERSION=us
 -DASSET_REVISION=rev0 -DLEO_VERSION=LEO_VERSION_A -DMFS_VERSION=MFS_VERSION_A -DNON_MATCHING -DAVOID_UB
 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -I$D/include -I$D/include/libultra -I$D/include/libc -I$D -I$D/src
 -I$ROOT/ref -I$ROOT/src/sim -I$ROOT/third_party/SH2ZAM/saturn/include -I$STUB -include $ROOT/ref/overrides.h -include $STUB/asset_syms.h
 -DFZX_SOURCE_CAMERA=1 -I$ASSETS -DFZX_DEFER_PROJECTION=$DEFER -DFZX_SIM_CAMERA=$SIMCAM -include $ROOT/ref/sim_overrides.h"
[ "$MM" = 0 ] || CFLAGS="$CFLAGS -DFZX_SOURCE_MACHINE_MENU=1"
SOURCES="src/game/racer.c src/game/course.c src/game/course_gadgets.c src/sys/math.c src/game/EA90.c src/game/common.c src/sys/controller.c src/overlays/ovl_i2/course_context.c src/overlays/ovl_i3/cpu.c src/game/game_context.c src/game/course_gadget_context.c src/game/camera.c"

# Objects are reused when newer than their source and every lane header/build input
# (SIM_FRESH=1 forces a full rebuild). Decomp and receipt-bound sources never change here.
DEPS="$0 $ROOT/ref/overrides.h $ROOT/ref/ref.h $ROOT/ref/sim.h $ROOT/ref/sim_overrides.h $ROOT/src/sim/*.h"
current() { # obj src
    [ "${SIM_FRESH:-0}" = 0 ] && [ -f "$1" ] && [ -f "${1%.o}.i" ] || return 1
    local f; for f in $2 $DEPS; do [ "$1" -nt "$f" ] || return 1; done
}
compile() { # As ref/build.sh: generated asset headers and symbols get empty stand-ins.
    local src=$1 obj=$2
    current "$obj" "$src" && return 0
    for _ in $(seq 1 60); do
        if $CC $CFLAGS -c "$src" -o "$obj" 2> "$obj.err"; then
            $CC $CFLAGS -E "$src" -o "${obj%.o}.i" 2>/dev/null
            return 0
        fi
        miss=$(grep -oE "fatal error: [^:]+: No such file|fatal error: '[^']+' file not found" "$obj.err" | head -1 |
               sed -E "s/fatal error: //; s/: No such file//; s/ file not found//; s/'//g" || true)
        if [ -n "$miss" ]; then mkdir -p "$STUB/$(dirname "$miss")"; : > "$STUB/$miss"; continue; fi
        names=$(LC_ALL=C sed -nE -e "s/.*error: [^A-Za-z_]*([A-Za-z_][A-Za-z0-9_]*)[^A-Za-z0-9_ ]+ undeclared.*/\1/p" \
                -e "s/.*undeclared identifier [^A-Za-z_]*([A-Za-z_][A-Za-z0-9_]*).*/\1/p" "$obj.err" | sort -u || true)
        [ -z "$names" ] && break
        for n in $names; do grep -q "\b$n\[\]" "$STUB/asset_syms.h" || echo "extern unsigned char $n[];" >> "$STUB/asset_syms.h"; done
    done
    cat "$obj.err" >&2
    return 1
}

OBJS=""
for s in $SOURCES; do
    o=$OUT/$(basename "$s" .c).o
    compile "$D/$s" "$o"
    OBJS="$OBJS $o"
done
for s in bridge hooks camera_bridge; do
    o=$OUT/ref_$s.o
    compile "$ROOT/ref/$s.c" "$o"
    OBJS="$OBJS $o"
done
# The lane's own bridge gets no stand-in retries: an undeclared name is an error, not an asset.
o=$OUT/ref_sim_bridge.o
if ! current "$o" "$ROOT/ref/sim_bridge.c"; then
    $CC $CFLAGS -Werror=implicit-function-declaration -c "$ROOT/ref/sim_bridge.c" -o "$o"
    $CC $CFLAGS -E "$ROOT/ref/sim_bridge.c" -o "${o%.o}.i"
fi
OBJS="$OBJS $o"
# Fixed-point camera: portable integer C, the same source the SH-2 closure compiles.
if [ "$SIMCAM" = 1 ]; then
    for s in fzx_sim_camera fzx_sim_convert; do
        $CC -m32 -fno-pic $OPT -g -std=gnu11 -Wall -Wextra -Werror -fno-strict-aliasing \
            -DSH2Z_FORCE_PORTABLE=1 -I"$ROOT/src/sim" -I"$ROOT/third_party/SH2ZAM/saturn/include" \
            -c "$ROOT/src/sim/$s.c" -o "$OUT/$s.o"
        OBJS="$OBJS $OUT/$s.o"
    done
    for s in sh2zam_divu sh2zam_unit30 sh2zam_trig12; do
        $CC -m32 -fno-pic $OPT -g -std=gnu11 -w -DSH2Z_FORCE_PORTABLE=1 -I"$ROOT/third_party/SH2ZAM/saturn/include" \
            -c "$ROOT/third_party/SH2ZAM/saturn/src/$s.c" -o "$OUT/$s.o"
        OBJS="$OBJS $OUT/$s.o"
    done
fi
# Driver side: host headers only.
$CC -m32 -msse2 -mfpmath=sse -fno-fast-math -fno-unsafe-math-optimizations -ffp-contract=off -fno-builtin \
    -fno-pic -O2 -g -std=gnu11 -Wall -Wextra -Werror -I"$ROOT/ref" -I"$ROOT/src/sim" -DFZX_SOURCE_CAMERA=1 \
    -DFZX_SOURCE_MACHINE_MENU=$MM -DFZX_DEFER_PROJECTION=$DEFER -DFZX_SIM_CAMERA=$SIMCAM \
    -c "$ROOT/ref/sim_main.c" -o "$OUT/sim_main.o"
OBJS="$OBJS $OUT/sim_main.o"
python3 "$ROOT/ref/gen_stubs.py" "$OUT/stubs.c" "$OUT"/*.i -- $OBJS > "$OUT/stubs.log"
$CC -m32 -fno-pic -O0 -w -c "$OUT/stubs.c" -o "$OUT/stubs.o"
$CC -m32 -no-pie $OBJS "$OUT/stubs.o" -lm -o "$OUT/fzx-sim"
{ $CC --version | head -1; echo "closure cflags:" $CFLAGS; echo "knobs mm=$MM defer=$DEFER simcam=$SIMCAM opt=$OPT"; } > "$OUT/recipe.txt"
echo "built $OUT/fzx-sim"
