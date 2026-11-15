# G1.3 SH-2 op microbenchmark image. Build from the repo root:
#   SATURN_DIR=tools/bench_sh2 tools/saturn_build.sh
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error YAUL_INSTALL_ROOT must point to the Yaul installation)
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH2ZAM_ROOT ?= ../../third_party/SH2ZAM

SH_PROGRAM := fzx-bench-sh2
SH_SRCS := main.c ../../ref/sat/soft_sqrtf.c ../../src/sh2float/fzx_float.c $(wildcard $(SH2ZAM_ROOT)/saturn/src/*.c)
SH_CFLAGS += -O2 -std=gnu11 -Wno-shadow -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1 \
             -fno-fast-math -ffp-contract=off -DFZX_FLOAT_SH2_DIVU

IP_VERSION := V0.100
IP_RELEASE_DATE := 20261115
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := FZX SH-2 op bench
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
