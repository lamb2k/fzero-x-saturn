# Build with the same Yaul SDK/toolchain as SH2ZAM's native cross workflow.
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error YAUL_INSTALL_ROOT must point to the Yaul installation)
endif

# Yaul captures map/output paths while reading build.pre.mk. Set these first;
# otherwise a fresh original-race build depends on a prior prototype build dir.
SH_PROGRAM := fzero-x-saturn
FZX_SIM ?= proto
FZX_SOURCE_HUD ?= 0
FZX_SOURCE_CAMERA ?= 0
FZX_SOURCE_ROAD ?= 0
ifeq ($(FZX_SIM),orig)
ifeq ($(FZX_SOURCE_HUD),1)
SH_BUILD_DIR := build-orig-hud/yaul
SH_OUTPUT_DIR := build-orig-hud
else
SH_BUILD_DIR := build-orig/yaul
SH_OUTPUT_DIR := build-orig
endif
endif
ifeq ($(FZX_SOURCE_CAMERA),1)
SH_BUILD_DIR := build-orig-camera/yaul
SH_OUTPUT_DIR := build-orig-camera
endif
ifeq ($(FZX_SOURCE_ROAD),1)
SH_BUILD_DIR := build-orig-road/yaul
SH_OUTPUT_DIR := build-orig-road
IMAGE_DIRECTORY := build-orig-road/cd
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

# Simulation selector, shown on the HUD. proto: the prototype driver.
# orig: the original race code drives the craft (build it with tools/saturn_orig.sh,
# which makes FZX_CLOSURE from the decompilation and imports the course).
ifeq ($(FZX_SIM),proto)
SH_CFLAGS += -DFZX_SIM=FZX_SIM_PROTO
else ifeq ($(FZX_SIM),orig)
ifneq ($(FZX_IMPORTED),1)
$(error FZX_SIM=orig needs FZX_IMPORTED=1: the original code and the renderer share the course)
endif
ifeq ($(wildcard $(FZX_CLOSURE)),)
$(error FZX_SIM=orig needs FZX_CLOSURE (ref_closure.o); use tools/saturn_orig.sh)
endif
SH_CFLAGS += -DFZX_SIM=FZX_SIM_ORIG -I../ref
SH_LDFLAGS += $(FZX_CLOSURE)
FZX_ORIG_SRCS := ../generated/course_record.c
# FZX_ROM_BOOT=1: the machine constants from the owner's ROM (generated/rom_boot.c, local only);
# 0: the CI fixture pattern (ref/hooks.c). saturn_orig.sh rebuilds main.o when this changes.
FZX_ROM_BOOT ?= 0
ifeq ($(FZX_ROM_BOOT),1)
FZX_ORIG_SRCS += ../generated/rom_boot.c
SH_CFLAGS += -DFZX_ROM_BOOT=1
endif
else
$(error FZX_SIM must be proto or orig)
endif

ifeq ($(FZX_SOURCE_CAMERA),1)
ifneq ($(FZX_SIM),orig)
$(error FZX_SOURCE_CAMERA=1 requires the original simulation)
endif
SH_CFLAGS += -DFZX_SOURCE_CAMERA=1
FZX_CAMERA_SRCS := ../src/fzx_view.c
else ifneq ($(FZX_SOURCE_CAMERA),0)
$(error FZX_SOURCE_CAMERA must be 0 or 1)
endif

# Partial native HUD integration; source scene/audio gates remain open.
ifeq ($(FZX_SOURCE_HUD),1)
ifneq ($(FZX_SIM),orig)
$(error FZX_SOURCE_HUD=1 requires the original race simulation)
endif
ifneq ($(FZX_ROM_BOOT),1)
$(error FZX_SOURCE_HUD=1 requires the local retail machine constants)
endif
FZX_HUD_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/source_art/source_art.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_HUD_RECEIPT)),)
$(error Source HUD is not current: $(FZX_HUD_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_HUD=1 -I../generated/source_art
FZX_HUD_SRCS := ../src/fzx_hud.c hud_native.c
else ifneq ($(FZX_SOURCE_HUD),0)
$(error FZX_SOURCE_HUD must be 0 or 1)
endif
ifeq ($(FZX_SOURCE_ROAD),1)
ifneq ($(FZX_SIM):$(FZX_SOURCE_CAMERA):$(FZX_ROM_BOOT),orig:1:1)
$(error FZX_SOURCE_ROAD=1 requires original simulation, source camera and local retail content)
endif
FZX_ROAD_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/road/road.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/road_pack/road_pack.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_ROAD_RECEIPT)),)
$(error Source road is not current: $(FZX_ROAD_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_ROAD=1 -I../generated/road_pack
FZX_ROAD_SRCS := road_native.c road_cd.c ../src/fzx_road_pack.c
else ifneq ($(FZX_SOURCE_ROAD),0)
$(error FZX_SOURCE_ROAD must be 0 or 1)
endif
SH_SRCS := main.c ../src/fzx.c $(FZX_ORIG_SRCS) $(FZX_HUD_SRCS) $(FZX_CAMERA_SRCS) $(FZX_ROAD_SRCS) $(wildcard $(SH2ZAM_ROOT)/saturn/src/*.c)
SH_CFLAGS += -DFZX_SH2_DIVU=1 -O2 -std=c11 -Wall -Wextra -I../include \
             -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1

IP_VERSION := V0.100
IP_RELEASE_DATE := 20261113
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := F-Zero X Saturn port foundation
ifeq ($(FZX_SIM),orig)
# The original race code's frames are deep: stack at the top of high work RAM, as the probe.
IP_MASTER_STACK_ADDR := 0x06100000
else
IP_MASTER_STACK_ADDR := 0x06004000
endif
IP_SLAVE_STACK_ADDR := 0x06001E00
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

ifeq ($(FZX_SOURCE_ROAD),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/road_pack/ROAD.PAK
pre-build-iso:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/road_pack/ROAD.PAK $(IMAGE_DIRECTORY)/ROAD.PAK
endif
