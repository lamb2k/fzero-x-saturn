#!/usr/bin/env bash
# Runs inside the Yaul image (ref/sat/build.sh). Cross-compiles the reference closure for the
# master SH-2 and links the probe image. Arg: the per-script build folder (/work/...).
set -euo pipefail
OUT=$1
W=/work
D=$W/upstream/G-Diffuser/decomp
STUB=$W/ref/build/gcc-O0/stub
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
SOURCES="src/game/racer.c src/game/course.c src/game/course_gadgets.c src/sys/math.c src/game/EA90.c src/game/common.c src/sys/controller.c src/overlays/ovl_i2/course_context.c src/overlays/ovl_i3/cpu.c"
OBJ=$OUT/obj
mkdir -p "$OBJ"
objs=""
for s in $SOURCES; do
    o=$OBJ/$(basename "$s" .c).o
    $CC $CFLAGS -c "$D/$s" -o "$o"
    objs="$objs $o"
done
for s in bridge hooks; do
    $CC $CFLAGS -c "$W/ref/$s.c" -o "$OBJ/ref_$s.o"
    objs="$objs $OBJ/ref_$s.o"
done
$CC -m2 -mb -O2 -w -c "$W/ref/sat/soft_sqrtf.c" -o "$OBJ/soft_sqrtf.o"
# FZX_FASTFLOAT=1: replace libgcc's generic soft-float with src/sh2float (bit-exact with SSE;
# tests/test_fzx_float.c). Default off: the knob-off image must keep its hash.
extra=""
if [ "${FZX_FASTFLOAT:-0}" = 1 ]; then
    $CC -m2 -mb -O2 -Wall -Wextra -DFZX_FLOAT_LIBGCC -DFZX_FLOAT_SH2_DIVU -c "$W/src/sh2float/fzx_float.c" -o "$OBJ/fzx_float.o"
    extra="$OBJ/fzx_float.o"
fi
$CC -m2 -mb -O0 -w -c "$OUT/sat_stubs.c" -o "$OBJ/sat_stubs.o"
$CC -m2 -mb -O2 -w -c "$OUT/sat_data.c" -o "$OBJ/sat_data.o"
$CC -m2 -mb -c "$OUT/sat_stubs_data.s" -o "$OBJ/sat_stubs_data.o"
"$YAUL_ARCH_SH_PREFIX-ld" -r -o "$OUT/ref_closure.o" $objs "$OBJ/soft_sqrtf.o" "$OBJ/sat_stubs.o" \
    "$OBJ/sat_data.o" "$OBJ/sat_stubs_data.o" $extra
# Yaul does not track SAT_CLOSURE as a link input: clear the outputs so the image relinks.
rm -rf "$OUT/yaul" "$OUT"/fzx-ref-sat.*
make -C "$W/ref/sat" SAT_OUT="$OUT" SAT_CLOSURE="$OUT/ref_closure.o"
