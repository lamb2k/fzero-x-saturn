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
# Refuse stale or hand-edited generated output (card G0.3).
ifneq ($(shell sh ../tools/check_receipt.sh ../generated/course.h.receipt >/dev/null 2>&1 && echo ok),ok)
$(error ../generated/course.h is stale or hand-edited: its receipt does not match. Rerun tools/import_course.py; see tools/check_receipt.sh ../generated/course.h.receipt)
endif
SH_CFLAGS += -DFZX_USE_IMPORTED_COURSE=1
else ifneq ($(FZX_IMPORTED),0)
$(error FZX_IMPORTED must be 0 for the demo or 1 for the imported course)
endif

# Simulation selector (card G0.4): proto now; fixed after Gate 3.
FZX_SIM ?= proto
ifeq ($(FZX_SIM),proto)
SH_CFLAGS += -DFZX_SIM=0
else ifeq ($(FZX_SIM),fixed)
SH_CFLAGS += -DFZX_SIM=1
else
$(error FZX_SIM must be proto or fixed)
endif

# G1.4 probe knobs (see main.c); defaults are the original behaviour.
FZX_HUD_EVERY ?= 1
FZX_SYNC_WAIT ?= 2
FZX_VDP1_INTERVAL ?= -1
SH_CFLAGS += -DFZX_HUD_EVERY=$(FZX_HUD_EVERY) -DFZX_SYNC_WAIT=$(FZX_SYNC_WAIT) \
             -DFZX_VDP1_INTERVAL="($(FZX_VDP1_INTERVAL))"

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
