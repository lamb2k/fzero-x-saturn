#!/usr/bin/env bash
# Runs inside the Yaul image. Cross-compiles the original race code (the reference closure:
# decomp sources, ref/bridge.c, ref/hooks.c, sqrtf, the float library and the stand-ins from
# ref/sat/gen_sat.py) for the master SH-2 into one relocatable object, OUT/ref_closure.o.
# Used by ref/sat/inner.sh (the probe) and tools/saturn_orig.sh (the game image).
# OUT/sat_data.c (the probe's fixture and script) is linked in when present.
set -euo pipefail
OUT=$1
W=$(cd "$(dirname "$0")/../.." && pwd)
D=$W/upstream/G-Diffuser/decomp
STUB=$W/ref/build/gcc-O0/stub
CAMERA=${FZX_SOURCE_CAMERA:-0}
PLAN=${FZX_ROAD_PLAN:-0}
SKY=${FZX_SOURCE_SKY:-0}
SKYLINE=${FZX_SOURCE_SKYLINE:-0}
[ "$SKYLINE" = 0 ] || [ "$SKYLINE:$CAMERA" = 1:1 ] || { echo "skyline requires source camera" >&2; exit 1; }
if [ "$SKYLINE" = 1 ]; then bash "$W/tools/check_receipt.sh" "$W/generated/skyline/skyline.receipt"; fi
[ "$SKY" = 0 ] || [ "$SKY:$CAMERA" = 1:1 ] || { echo "sky requires source camera" >&2; exit 1; }
if [ "$SKY" = 1 ]; then bash "$W/tools/check_receipt.sh" "$W/generated/sky/sky.receipt"; fi
[ "$PLAN" = 0 ] || [ "$PLAN:$CAMERA" = 1:1 ] || { echo "road planner requires source camera" >&2; exit 1; }
if [ "$PLAN" = 1 ]; then
    bash "$W/tools/check_receipt.sh" "$W/generated/road_plan/course.receipt"
fi
[ "$CAMERA" = 0 ] || [ "$CAMERA" = 1 ] || { echo "invalid FZX_SOURCE_CAMERA" >&2; exit 1; }
if [ "$CAMERA" = 1 ]; then
    ART=${REF_CAMERA_ART:-fixture}
    case "$ART" in fixture|retail) ;; *) echo "invalid REF_CAMERA_ART" >&2; exit 1;; esac
    STUB=$W/ref/build/gcc-O0-camera-$ART/stub
    ASSETS=$W/generated/camera_$ART
    bash "$W/tools/check_receipt.sh" "$ASSETS/viewports.receipt"
fi
CC="$YAUL_ARCH_SH_PREFIX-gcc"
# Same defines and float discipline as ref/build.sh; the SH-2 has no FPU, so float is libgcc
# soft-float (IEEE binary32, round to nearest). sqrtf is ref/sat/soft_sqrtf.c (Yaul has no
# libm), checked against SSE for all 2^32 inputs.
CFLAGS="-m2 -mb -O2 -fno-fast-math -fno-unsafe-math-optimizations -ffp-contract=off -fno-strict-aliasing
 -fno-builtin -ffunction-sections -fdata-sections -g -std=gnu11 -w
 -D_LANGUAGE_C -DLANGUAGE_C -DF3DEX_GBI_2 -DNDEBUG -DVERSION_US=1 -DBUILD_VERSION=VERSION_I -DASSET_VERSION=us
 -DASSET_REVISION=rev0 -DLEO_VERSION=LEO_VERSION_A -DMFS_VERSION=MFS_VERSION_A -DNON_MATCHING -DAVOID_UB
 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -I$D/include -I$D/include/libultra -I$D/include/libc -I$D -I$D/src
 -I$W/ref -I$STUB -include $W/ref/overrides.h -include $STUB/asset_syms.h"
SOURCES="src/game/racer.c src/game/course.c src/game/course_gadgets.c src/sys/math.c src/game/EA90.c src/game/common.c src/sys/controller.c src/overlays/ovl_i2/course_context.c src/overlays/ovl_i3/cpu.c src/game/game_context.c src/game/course_gadget_context.c"
BRIDGES="bridge hooks"
if [ "$PLAN" = 1 ]; then CFLAGS="-I$D/src/game $CFLAGS"; fi
if [ "$CAMERA" = 1 ]; then
    CFLAGS="$CFLAGS -DFZX_SOURCE_CAMERA=1 -I$ASSETS"
    SOURCES="$SOURCES src/game/camera.c"
    BRIDGES="$BRIDGES camera_bridge"
fi
OBJ=$OUT/obj
mkdir -p "$OBJ"
objs=""
for s in $SOURCES; do
    o=$OBJ/$(basename "$s" .c).o
    if [ "$PLAN:$s" = 1:src/game/course.c ]; then
        $CC $CFLAGS -c "$W/generated/road_plan/course.c" -o "$o"
    else
        $CC $CFLAGS -c "$D/$s" -o "$o"
    fi
    objs="$objs $o"
done
for s in $BRIDGES; do
    $CC $CFLAGS -c "$W/ref/$s.c" -o "$OBJ/ref_$s.o"
    objs="$objs $OBJ/ref_$s.o"
done
if [ "$SKY" = 1 ]; then
    $CC $CFLAGS -I"$D/src/overlays/ovl_i3" -c "$W/generated/sky/sky_state.c" -o "$OBJ/sky_state.o"
    objs="$objs $OBJ/sky_state.o"
fi
if [ "$SKYLINE" = 1 ]; then
    $CC $CFLAGS -I"$D/src/overlays/ovl_i3" -c "$W/generated/skyline/skyline_state.c" -o "$OBJ/skyline_state.o"
    objs="$objs $OBJ/skyline_state.o"
fi
$CC -m2 -mb -O2 -w -DFZX_SQRT_SH2_DIVU -c "$W/ref/sat/soft_sqrtf.c" -o "$OBJ/soft_sqrtf.o"
# FZX_FASTFLOAT=1: replace libgcc's generic soft-float with src/sh2float (bit-exact with SSE;
# tests/test_fzx_float.c). Default off: the knob-off image must keep its hash.
extra=""
if [ "${FZX_FASTFLOAT:-0}" = 1 ]; then
    $CC -m2 -mb -O2 -Wall -Wextra -DFZX_FLOAT_LIBGCC -DFZX_FLOAT_SH2_DIVU -c "$W/src/sh2float/fzx_float.c" -o "$OBJ/fzx_float.o"
    extra="$OBJ/fzx_float.o"
fi
$CC -m2 -mb -O0 -w -c "$OUT/sat_stubs.c" -o "$OBJ/sat_stubs.o"
if [ -f "$OUT/sat_data.c" ]; then
    $CC -m2 -mb -O2 -w -c "$OUT/sat_data.c" -o "$OBJ/sat_data.o"
    extra="$extra $OBJ/sat_data.o"
fi
$CC -m2 -mb -c "$OUT/sat_stubs_data.s" -o "$OBJ/sat_stubs_data.o"
"$YAUL_ARCH_SH_PREFIX-ld" -r -o "$OUT/ref_closure.o" $objs "$OBJ/soft_sqrtf.o" "$OBJ/sat_stubs.o" \
    "$OBJ/sat_stubs_data.o" $extra
