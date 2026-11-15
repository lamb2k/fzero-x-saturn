# Build with the same Yaul SDK/toolchain as SH2ZAM's native cross workflow.
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error YAUL_INSTALL_ROOT must point to the Yaul installation)
endif

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH2ZAM_ROOT ?= ../third_party/SH2ZAM
FZX_IMPORTED ?= 0

ifeq ($(wildcard $(SH2ZAM_ROOT)/saturn/include/sh2zam/sh2zam.h),)
$(error SH2ZAM not found at $(SH2ZAM_ROOT); initialize third_party/SH2ZAM or set SH2ZAM_ROOT)
endif

ifeq ($(FZX_IMPORTED),1)
ifeq ($(wildcard ../generated/course.h),)
$(error FZX_IMPORTED=1 requires ../generated/course.h from the course importer)
endif
# Refuse a stale or hand-edited header (the importer writes the receipt).
FZX_RECEIPT_RESULT := $(shell sh ../tools/check_receipt.sh ../generated/course.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_RECEIPT_RESULT)),)
$(error Generated course is not current: $(FZX_RECEIPT_RESULT))
endif
SH_CFLAGS += -DFZX_USE_IMPORTED_COURSE=1
else ifneq ($(FZX_IMPORTED),0)
$(error FZX_IMPORTED must be 0 for the demo or 1 for the imported course)
endif

# Simulation selector, shown on the HUD. Only the prototype driver exists;
# `fixed` (the ported racer physics) arrives with Gate 3 of docs/POC_PLAN.md.
FZX_SIM ?= proto
ifeq ($(FZX_SIM),proto)
SH_CFLAGS += -DFZX_SIM=FZX_SIM_PROTO
else
$(error FZX_SIM must be proto; the fixed simulation does not exist yet)
endif

SH_PROGRAM := fzero-x-saturn
SH_SRCS := main.c ../src/fzx.c $(wildcard $(SH2ZAM_ROOT)/saturn/src/*.c)
SH_CFLAGS += -O2 -std=c11 -Wall -Wextra -I../include \
             -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1

IP_VERSION := V0.100
IP_RELEASE_DATE := 20261113
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := F-Zero X Saturn port foundation
IP_MASTER_STACK_ADDR := 0x06004000
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk
