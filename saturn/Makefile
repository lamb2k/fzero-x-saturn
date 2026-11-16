# Build with the same Yaul SDK/toolchain as SH2ZAM's native cross workflow.
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error YAUL_INSTALL_ROOT must point to the Yaul installation)
endif

# Yaul captures map/output paths while reading build.pre.mk. Set these first;
# otherwise a fresh original-race build depends on a prior prototype build dir.
SH_PROGRAM := fzero-x-saturn
FZX_SIM ?= proto
FZX_SOURCE_HUD ?= 0
FZX_HUD_CD ?= 0
FZX_SOURCE_CAMERA ?= 0
FZX_SOURCE_SKY ?= 0
FZX_SOURCE_SKYLINE ?= 0
FZX_SOURCE_PLANES ?= 0
FZX_SOURCE_ROAD_TILES ?= 0
FZX_ROAD_TILE_PROJECT_CULL ?= 0
FZX_ROAD_TILE_EDGE_REUSE ?= 0
ifneq ($(filter $(FZX_ROAD_TILE_PROJECT_CULL),0 1),$(FZX_ROAD_TILE_PROJECT_CULL))
$(error FZX_ROAD_TILE_PROJECT_CULL must be 0 or 1)
endif
ifneq ($(filter $(FZX_ROAD_TILE_EDGE_REUSE),0 1),$(FZX_ROAD_TILE_EDGE_REUSE))
$(error FZX_ROAD_TILE_EDGE_REUSE must be 0 or 1)
endif
FZX_SKYLINE_CD ?= 0
FZX_SKY_SIMPLIFY ?= 0
ifneq ($(filter $(FZX_SKY_SIMPLIFY),0 1),$(FZX_SKY_SIMPLIFY))
$(error FZX_SKY_SIMPLIFY must be 0 or 1)
endif
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
ifeq ($(FZX_SOURCE_SKY),1)
SH_BUILD_DIR := build-orig-sky/yaul
SH_OUTPUT_DIR := build-orig-sky
IMAGE_DIRECTORY := build-orig-sky/cd
endif
ifeq ($(FZX_SOURCE_SKYLINE),1)
SH_BUILD_DIR := build-orig-skyline/yaul
SH_OUTPUT_DIR := build-orig-skyline
IMAGE_DIRECTORY := build-orig-skyline/cd
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

ifeq ($(FZX_SOURCE_SKY),1)
ifneq ($(FZX_SIM):$(FZX_SOURCE_CAMERA):$(FZX_ROM_BOOT),orig:1:1)
$(error FZX_SOURCE_SKY=1 requires original simulation, source camera and local retail content)
endif
FZX_SKY_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/sky/sky.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_SKY_RECEIPT)),)
$(error Source sky is not current: $(FZX_SKY_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_SKY=1 -I../generated/sky
SH_CFLAGS += -DFZX_SKY_SIMPLIFY=$(FZX_SKY_SIMPLIFY)
FZX_SKY_SRCS := ../src/fzx_sky.c sky_native.c
else ifneq ($(FZX_SOURCE_SKY),0)
$(error FZX_SOURCE_SKY must be 0 or 1)
endif

ifeq ($(FZX_SOURCE_SKYLINE),1)
ifneq ($(FZX_SIM):$(FZX_SOURCE_CAMERA):$(FZX_SOURCE_SKY):$(FZX_ROM_BOOT),orig:1:1:1)
$(error FZX_SOURCE_SKYLINE=1 requires original simulation, camera, sky and local retail content)
endif
FZX_SKYLINE_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/skyline/skyline.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_SKYLINE_RECEIPT)),)
$(error Source skyline is not current: $(FZX_SKYLINE_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_SKYLINE=1 -I../generated/skyline
FZX_SKY_SRCS += ../src/fzx_skyline.c skyline_native.c
else ifneq ($(FZX_SOURCE_SKYLINE),0)
$(error FZX_SOURCE_SKYLINE must be 0 or 1)
endif

ifeq ($(FZX_SKYLINE_CD),1)
ifneq ($(FZX_SOURCE_SKYLINE),1)
$(error FZX_SKYLINE_CD=1 requires FZX_SOURCE_SKYLINE=1)
endif
SH_CFLAGS += -DFZX_SKYLINE_CD=1
else ifneq ($(FZX_SKYLINE_CD),0)
$(error FZX_SKYLINE_CD must be 0 or 1)
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
ifeq ($(FZX_HUD_CD),1)
ifneq ($(FZX_SOURCE_HUD),1)
$(error FZX_HUD_CD=1 requires FZX_SOURCE_HUD=1)
endif
FZX_HUD_CD_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/hud_cd/hud_cd.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_HUD_CD_RECEIPT)),)
$(error Source HUD CD pack is not current: $(FZX_HUD_CD_RECEIPT))
endif
SH_CFLAGS += -DFZX_HUD_CD=1 -I../generated/hud_cd
else ifneq ($(FZX_HUD_CD),0)
$(error FZX_HUD_CD must be 0 or 1)
endif
ifneq ($(filter 1,$(FZX_SKYLINE_CD) $(FZX_HUD_CD) $(FZX_SOURCE_PLANES)),)
FZX_SKY_SRCS += texture_cd.c
ifneq ($(FZX_SOURCE_ROAD),1)
FZX_SKY_SRCS += road_cd.c
endif
endif
ifeq ($(FZX_SOURCE_PLANES),1)
ifneq ($(FZX_SOURCE_SKY):$(FZX_SOURCE_SKYLINE):$(FZX_SKYLINE_CD):$(FZX_HUD_CD),1:1:1:1)
$(error FZX_SOURCE_PLANES=1 requires source sky/skyline and both CD asset arms)
endif
FZX_PLANES_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/planes/planes.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_PLANES_RECEIPT)),)
$(error Source plane pack is not current: $(FZX_PLANES_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_PLANES=1 -I../generated/planes
FZX_SKY_SRCS += ../src/fzx_planes.c planes_native.c
else ifneq ($(FZX_SOURCE_PLANES),0)
$(error FZX_SOURCE_PLANES must be 0 or 1)
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
ifeq ($(FZX_SOURCE_ROAD_TILES),1)
ifneq ($(FZX_SOURCE_ROAD):$(FZX_SOURCE_SKY):$(FZX_SOURCE_SKYLINE):$(FZX_HUD_CD),1:1:1:1)
$(error Native road tiles require source road/sky/skyline and CD HUD)
endif
ifeq ($(FZX_SOURCE_ROAD_DRAW),1)
$(error Select native tiles or the software road experiment)
endif
FZX_ROAD_TILES_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/road_tiles/road_tiles.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_ROAD_TILES_RECEIPT)),)
$(error Native road tile assets are stale: $(FZX_ROAD_TILES_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_ROAD_TILES=1 -DFZX_INPUT_LOG_LOW_RAM=1 -I../generated/road_tiles
SH_CFLAGS += -DFZX_ROAD_TILE_PROJECT_CULL=$(FZX_ROAD_TILE_PROJECT_CULL) -DFZX_ROAD_TILE_EDGE_REUSE=$(FZX_ROAD_TILE_EDGE_REUSE)
FZX_ROAD_SRCS += ../src/fzx_road_tiles.c road_tiles_native.c
else ifneq ($(FZX_SOURCE_ROAD_TILES),0)
$(error FZX_SOURCE_ROAD_TILES must be 0 or 1)
endif
SH_SRCS := main.c ../src/fzx.c $(FZX_ORIG_SRCS) $(FZX_HUD_SRCS) $(FZX_CAMERA_SRCS) $(FZX_ROAD_SRCS) $(FZX_SKY_SRCS) $(wildcard $(SH2ZAM_ROOT)/saturn/src/*.c)
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
pre-build-iso: stage-road-pack
.PHONY: stage-road-pack
stage-road-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/road_pack/ROAD.PAK $(IMAGE_DIRECTORY)/ROAD.PAK
endif
ifeq ($(FZX_SKYLINE_CD),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/skyline/SKYLINE.PAK
pre-build-iso: stage-skyline-pack
.PHONY: stage-skyline-pack
stage-skyline-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/skyline/SKYLINE.PAK $(IMAGE_DIRECTORY)/SKYLINE.PAK
endif
ifeq ($(FZX_HUD_CD),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/hud_cd/HUD.PAK
pre-build-iso: stage-hud-pack
.PHONY: stage-hud-pack
stage-hud-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/hud_cd/HUD.PAK $(IMAGE_DIRECTORY)/HUD.PAK
endif
ifeq ($(FZX_SOURCE_PLANES),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/planes/PLANES.PAK
pre-build-iso: stage-plane-pack
.PHONY: stage-plane-pack
stage-plane-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/planes/PLANES.PAK $(IMAGE_DIRECTORY)/PLANES.PAK
endif

ifeq ($(FZX_SOURCE_ROAD_TILES),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/road_tiles/ROADTL.PAK ../generated/road_tiles/ROADTX.PAK
pre-build-iso: stage-road-tiles
.PHONY: stage-road-tiles
stage-road-tiles:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/road_tiles/ROADTL.PAK ../generated/road_tiles/ROADTX.PAK $(IMAGE_DIRECTORY)/
endif
