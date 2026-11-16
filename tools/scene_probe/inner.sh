#!/usr/bin/env bash
# Runs in the already-mounted Yaul image; build expected.h with prepare.py first.
set -euo pipefail
source /yaul-env.sh
export PATH="$YAUL_INSTALL_ROOT/bin:$PATH"
W=/work
D=$W/upstream/G-Diffuser/decomp
OUT=$W/tools/scene_probe/build
STUB=$W/ref/build/gcc-O2-camera-fixture-backdrop/stub
CC=$YAUL_ARCH_SH_PREFIX-gcc
bash "$W/tools/check_receipt.sh" "$OUT/expected.receipt"
bash "$W/tools/check_receipt.sh" "$W/generated/backdrop_state/background.receipt"
CFLAGS="-m2 -mb -O2 -fno-fast-math -fno-unsafe-math-optimizations -ffp-contract=off -fno-strict-aliasing
 -fno-builtin -ffunction-sections -fdata-sections -g -std=gnu11 -w
 -D_LANGUAGE_C -DLANGUAGE_C -DF3DEX_GBI_2 -DNDEBUG -DVERSION_US=1 -DBUILD_VERSION=VERSION_I -DASSET_VERSION=us
 -DASSET_REVISION=rev0 -DLEO_VERSION=LEO_VERSION_A -DMFS_VERSION=MFS_VERSION_A -DNON_MATCHING -DAVOID_UB
 -D_MIPS_SZLONG=32 -D_MIPS_SZINT=32 -I$D/include -I$D/include/libultra -I$D/include/libc -I$D -I$D/src
 -I$D/src/overlays/ovl_i3 -I$W/ref -I$STUB -include $W/ref/overrides.h -include $STUB/asset_syms.h
 -D_aCloudTex_WIDTH=64 -D_aCloudTex_HEIGHT=32 -D_D_F2207C8_WIDTH=8 -D_D_F2207C8_HEIGHT=8"
$CC $CFLAGS -c "$W/generated/backdrop_state/background.c" -o "$OUT/background.o"
$CC $CFLAGS -c "$D/src/sys/math.c" -o "$OUT/math.o"
$CC $CFLAGS -c "$W/ref/scene_probe.c" -o "$OUT/probe.o"
$CC $CFLAGS -c "$W/tools/scene_probe/state.c" -o "$OUT/state.o"
$CC -m2 -mb -O2 -w -DFZX_SQRT_SH2_DIVU -c "$W/ref/sat/soft_sqrtf.c" -o "$OUT/sqrt.o"
$CC -m2 -mb -O2 -Wall -Wextra -DFZX_FLOAT_LIBGCC -DFZX_FLOAT_SH2_DIVU -c "$W/src/sh2float/fzx_float.c" -o "$OUT/float.o"
"$YAUL_ARCH_SH_PREFIX-ld" -r -o "$OUT/scene.o" "$OUT/background.o" "$OUT/math.o" "$OUT/probe.o" "$OUT/state.o" "$OUT/sqrt.o" "$OUT/float.o"
make -B -C "$W/tools/scene_probe" FZX_SCENE_CONTROL=${FZX_SCENE_CONTROL:-0}
