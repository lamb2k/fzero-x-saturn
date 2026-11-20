# SCU DSP micro-bench image (DSP lane, 2026-11-19): see main.c. No game data.
#
#   make -C saturn/engine/bench_dsp SH2ZAM_ROOT=<checkout with dsp-1> [DSPB_CORRUPT=1]
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error Undefined YAUL_INSTALL_ROOT (install root directory))
endif

SH_PROGRAM := dsp-bench
DSPB_CORRUPT ?= 0
SH_OUTPUT_DIR := build$(if $(filter 1,$(DSPB_CORRUPT)),-corrupt,)
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
IMAGE_1ST_READ_BIN := A.BIN

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH2ZAM_ROOT ?=
ifeq ($(wildcard $(SH2ZAM_ROOT)/saturn/include/sh2zam/dsp_experimental.h),)
$(error Set SH2ZAM_ROOT to an SH2ZAM checkout with dsp-1)
endif

SH2ZAM_UNITS := sh2zam.c sh2zam_divu.c sh2zam_view.c sh2zam_dsp.c
SH_SRCS := main.c $(addprefix $(SH2ZAM_ROOT)/saturn/src/,$(SH2ZAM_UNITS))
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1 \
    -ffunction-sections -fdata-sections -DDSPB_CORRUPT=$(DSPB_CORRUPT)
SH_CFLAGS += $$(if $$(filter %/sh2zam_view.c,$$<),-fno-lto,)
SH_LDFLAGS += -Wl,--gc-sections

IP_VERSION := V0.001
IP_RELEASE_DATE := 20261119
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := DSP bench
IP_MASTER_STACK_ADDR := 0x06100000
IP_SLAVE_STACK_ADDR := 0x06004000
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
