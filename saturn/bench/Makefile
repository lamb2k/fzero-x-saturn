# G1.3 control: SH-2 timing image for one converted function (see bench.c).
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error YAUL_INSTALL_ROOT must point to the Yaul installation)
endif
include $(YAUL_INSTALL_ROOT)/share/build.pre.mk
SH2ZAM_ROOT ?= ../../third_party/SH2ZAM
SH_PROGRAM := fzx-bench
SH_SRCS := bench.c $(wildcard $(SH2ZAM_ROOT)/saturn/src/*.c)
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -fno-fast-math -ffp-contract=off \
             -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1
IP_VERSION := V0.100
IP_RELEASE_DATE := 20261114
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := F-Zero X Saturn SH-2 bench
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0
include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
