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
MACHINE=${FZX_SOURCE_MACHINE:-0}
COUNTDOWN=${FZX_SOURCE_COUNTDOWN:-0}
MACHINE_MENU=${FZX_SOURCE_MACHINE_MENU:-0}
[ "$MACHINE_MENU" = 0 ] || [ "$MACHINE_MENU" = 1 ] || { echo "invalid FZX_SOURCE_MACHINE_MENU" >&2; exit 1; }
[ "$COUNTDOWN" = 0 ] || [ "$COUNTDOWN:$CAMERA" = 1:1 ] || { echo "countdown requires source camera" >&2; exit 1; }
[ "$MACHINE" = 0 ] || [ "$MACHINE:$CAMERA" = 1:1 ] || { echo "machine requires source camera" >&2; exit 1; }
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
if [ "$MACHINE_MENU" = 1 ]; then STUB="${STUB%/stub}-machine-menu/stub"; fi
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
if [ "${FZX_SOURCE_FINISH_MENU:-0}" = 1 ]; then
    [ "${FZX_SOURCE_RECORDS:-0}" = 1 ] || { echo 'finish menu requires records' >&2; exit 1; }
    BRIDGES="$BRIDGES finish_menu_bridge"
fi
if [ "${FZX_SOURCE_RECORDS:-0}" = 1 ]; then
    [ "${FZX_SOURCE_RESULTS:-0}" = 1 ] || { echo 'records requires results' >&2; exit 1; }
    BRIDGES="$BRIDGES records_bridge"
fi
if [ "${FZX_SOURCE_RESULTS:-0}" = 1 ]; then
    [ "$COUNTDOWN:$MACHINE_MENU:$CAMERA" = 1:1:1 ] || { echo "results requires countdown, selection and camera" >&2; exit 1; }
    CFLAGS="$CFLAGS -I$W/include -DFZX_RESULTS_SOURCE_ABI=1"
    BRIDGES="$BRIDGES results_bridge"
fi
if [ "$MACHINE_MENU" = 1 ]; then CFLAGS="$CFLAGS -DFZX_SOURCE_MACHINE_MENU=1 -DFZX_RACE_GFX_EXTERNAL=1"; fi
if [ "$COUNTDOWN" = 1 ]; then BRIDGES="$BRIDGES countdown_bridge"; fi
if [ "$PLAN" = 1 ]; then CFLAGS="-I$D/src/game $CFLAGS"; fi
if [ "$CAMERA" = 1 ]; then
    CFLAGS="$CFLAGS -DFZX_SOURCE_CAMERA=1 -I$ASSETS"
    SOURCES="$SOURCES src/game/camera.c"
    BRIDGES="$BRIDGES camera_bridge"
fi
if [ "$MACHINE" = 1 ]; then CFLAGS="$CFLAGS -DFZX_SOURCE_MACHINE=1"; fi
# FZX_REF_MACHINE_FIXED=1: fixed-point ref_machine presentation tail (default 0 keeps the bridge code).
REF_MACHINE_FIXED=${FZX_REF_MACHINE_FIXED:-0}
[ "$REF_MACHINE_FIXED" = 0 ] || [ "$REF_MACHINE_FIXED:$MACHINE" = 1:1 ] || { echo "FZX_REF_MACHINE_FIXED requires source machine" >&2; exit 1; }
if [ "$REF_MACHINE_FIXED" = 1 ]; then CFLAGS="$CFLAGS -DFZX_REF_MACHINE_FIXED=1"; fi
# FZX_DEFER_PROJECTION=1 (quick win 1, default 0): ref/sim_overrides.h defers the
# per-tick camera matrix build to its first read (ref/sim_bridge.c). The image must
# pass the same value to make (saturn/Makefile checks the closure's symbols).
DEFER=${FZX_DEFER_PROJECTION:-0}
[ "$DEFER" = 0 ] || [ "$DEFER:$CAMERA" = 1:1 ] || { echo "deferred projection requires source camera" >&2; exit 1; }
[ "${FZX_SIM_CAMERA:-0}" = 0 ] || { echo "the fixed-point camera is not integrated (claude/sim-native)" >&2; exit 1; }
if [ "$DEFER" = 1 ]; then
    CFLAGS="$CFLAGS -DFZX_DEFER_PROJECTION=1 -include $W/ref/sim_overrides.h"
    BRIDGES="$BRIDGES sim_bridge"
fi
# Publication fixes (default 0; the image's make must use the same values).
CATCHUP=${FZX_BACKDROP_CATCHUP:-0}; CATCHUP_VERIFY=${FZX_BACKDROP_CATCHUP_VERIFY:-0}
[ "$CATCHUP" = 0 ] || [ "$CATCHUP:$SKY" = 1:1 ] || { echo "backdrop catch-up requires the source sky" >&2; exit 1; }
[ "$CATCHUP_VERIFY" = 0 ] || [ "$CATCHUP" = 1 ] || { echo "catch-up verify requires catch-up" >&2; exit 1; }
if [ "$CATCHUP" = 1 ]; then CFLAGS="$CFLAGS -DFZX_BACKDROP_CATCHUP=1"; fi
if [ "$CATCHUP_VERIFY" = 1 ]; then CFLAGS="$CFLAGS -DFZX_BACKDROP_CATCHUP_VERIFY=1"; fi
PLAN_FIXED=${FZX_ROAD_PLAN_FIXED:-0}
[ "$PLAN_FIXED" = 0 ] || [ "$PLAN_FIXED:$PLAN" = 1:1 ] || { echo "fixed road plan requires the source road planner" >&2; exit 1; }
if [ "$PLAN_FIXED" = 1 ]; then CFLAGS="$CFLAGS -DFZX_ROAD_PLAN_FIXED=1"; fi
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
if [ "${FZX_SOURCE_RESULTS:-0}" = 1 ]; then
    bash "$W/tools/check_receipt.sh" "$W/generated/results-source/results_source.receipt"
    $CC $CFLAGS -c "$W/generated/results-source/results_matrix_bridge.c" -o "$OBJ/results_matrix_bridge.o"
    objs="$objs $OBJ/results_matrix_bridge.o"
    # Keep original state out of Yaul's presentation compilation: yaul.specs
    # unconditionally enables fast-math, including reciprocal multiplication.
    RESULTS_CFLAGS="-m2 -mb -O2 -std=c11 -ffreestanding -fno-builtin -fsigned-char -fno-fast-math
        -fno-unsafe-math-optimizations -ffp-contract=off -ffunction-sections -fdata-sections"
    $CC $RESULTS_CFLAGS -I"$W/include" -I"$W/generated/results-source" -c "$W/generated/results-source/results_source.c" -o "$OBJ/results_source.o"
    objs="$objs $OBJ/results_source.o"
fi
if [ "${FZX_SOURCE_RECORDS:-0}" = 1 ]; then
    bash "$W/tools/check_receipt.sh" "$W/generated/records-source/records_source.receipt"
    bash "$W/tools/check_receipt.sh" "$W/generated/records-draw-source/records_draw_source.receipt"
    $CC $CFLAGS -c "$W/generated/records-draw-source/records_matrix_bridge.c" -o "$OBJ/records_matrix_bridge.o"
    $CC $RESULTS_CFLAGS -I"$W/include" -I"$W/generated/records-source" -c "$W/generated/records-source/records_source.c" -o "$OBJ/records_source.o"
    $CC $RESULTS_CFLAGS -I"$W/include" -I"$W/generated/records-draw-source" -I"$W/generated/records_ui" -c "$W/generated/records-draw-source/records_draw_source.c" -o "$OBJ/records_draw_source.o"
    objs="$objs $OBJ/records_matrix_bridge.o $OBJ/records_source.o $OBJ/records_draw_source.o"
fi
if [ "${FZX_SOURCE_FINISH_MENU:-0}" = 1 ]; then
    bash "$W/tools/check_receipt.sh" "$W/generated/finish-menu-source/finish_menu_source.receipt"
    bash "$W/tools/check_receipt.sh" "$W/generated/finish-menu-draw-source/finish_menu_draw_source.receipt"
    $CC $RESULTS_CFLAGS -I"$W/include" -I"$W/generated/finish-menu-source" -c "$W/generated/finish-menu-source/finish_menu_source.c" -o "$OBJ/finish_menu_source.o"
    $CC $RESULTS_CFLAGS -I"$W/include" -c "$W/generated/finish-menu-draw-source/finish_menu_draw_source.c" -o "$OBJ/finish_menu_draw_source.o"
    objs="$objs $OBJ/finish_menu_source.o $OBJ/finish_menu_draw_source.o"
fi
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
