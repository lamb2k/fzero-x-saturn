# Build with the same Yaul SDK/toolchain as SH2ZAM's native cross workflow.
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error YAUL_INSTALL_ROOT must point to the Yaul installation)
endif

# Yaul captures map/output paths while reading build.pre.mk. Set these first;
# otherwise a fresh original-race build depends on a prior prototype build dir.
SH_PROGRAM := fzero-x-saturn
FZX_SIM ?= proto
FZX_SOURCE_TITLE ?= 0
FZX_SOURCE_MENU ?= 0
FZX_SOURCE_COURSE_MENU ?= 0
FZX_SOURCE_MACHINE_MENU ?= 0
FZX_SOURCE_RESULTS ?= 0
FZX_SOURCE_RECORDS ?= 0
FZX_SOURCE_FINISH_MENU ?= 0
FZX_SOURCE_COUNTDOWN ?= 0
FZX_NATIVE_CORE_EMPTY ?= 0
ifeq ($(FZX_NATIVE_CORE_EMPTY),1)
ifneq ($(FZX_SIM):$(FZX_SOURCE_ROAD_TILES):$(FZX_SOURCE_MACHINE),orig:1:1)
$(error FZX_NATIVE_CORE_EMPTY requires original simulation, road tiles and machine)
endif
else ifneq ($(FZX_NATIVE_CORE_EMPTY),0)
$(error FZX_NATIVE_CORE_EMPTY must be 0 or 1)
endif
FZX_SOURCE_HUD ?= 0
FZX_HUD_CD ?= 0
FZX_SOURCE_CAMERA ?= 0
FZX_SOURCE_MACHINE ?= 0
FZX_MACHINE_REBAKE ?= 0
FZX_VEHICLE_PROFILE ?= 0
FZX_VEHICLE_SUBPROFILE ?= 0
FZX_VEHICLE_FAST ?= 0
FZX_VEHICLE_NATIVE ?= 0
ifneq ($(filter $(FZX_VEHICLE_NATIVE),0 1),$(FZX_VEHICLE_NATIVE))
$(error FZX_VEHICLE_NATIVE must be 0 or 1)
endif
ifneq ($(filter $(FZX_VEHICLE_FAST),0 1),$(FZX_VEHICLE_FAST))
$(error FZX_VEHICLE_FAST must be 0 or 1)
endif
FZX_VEHICLE_NATIVE_LOW_TABLES ?= 1
ifneq ($(filter $(FZX_VEHICLE_NATIVE_LOW_TABLES),0 1),$(FZX_VEHICLE_NATIVE_LOW_TABLES))
$(error FZX_VEHICLE_NATIVE_LOW_TABLES must be 0 or 1)
endif
ifneq ($(filter $(FZX_VEHICLE_SUBPROFILE),0 1),$(FZX_VEHICLE_SUBPROFILE))
$(error FZX_VEHICLE_SUBPROFILE must be 0 or 1)
endif
ifneq ($(filter $(FZX_MACHINE_REBAKE),0 1),$(FZX_MACHINE_REBAKE))
$(error FZX_MACHINE_REBAKE must be 0 or 1)
endif
ifeq ($(FZX_MACHINE_REBAKE),1)
ifneq ($(FZX_SOURCE_MACHINE),1)
$(error Vehicle rebake requires source machine)
endif
endif
FZX_SOURCE_SKY ?= 0
FZX_SOURCE_SKYLINE ?= 0
FZX_SOURCE_PLANES ?= 0
FZX_SOURCE_FLOOR_RBG ?= 0
FZX_PLANE_CLOUD_ONLY ?= 0
FZX_PLANES_DIR := ../generated/planes
ifeq ($(FZX_PLANE_CLOUD_ONLY),1)
ifneq ($(FZX_SOURCE_FLOOR_RBG):$(FZX_SOURCE_PLANES),1:1)
$(error FZX_PLANE_CLOUD_ONLY requires original planes and RBG floor)
endif
FZX_PLANES_DIR := ../generated/planes_cloud
else ifneq ($(FZX_PLANE_CLOUD_ONLY),0)
$(error FZX_PLANE_CLOUD_ONLY must be 0 or 1)
endif
FZX_FRAME_PROFILE ?= 0
ifeq ($(FZX_FRAME_PROFILE),1)
ifneq ($(FZX_SIM):$(FZX_SOURCE_FLOOR_RBG):$(FZX_SOURCE_MACHINE),orig:1:1)
$(error FZX_FRAME_PROFILE requires original race, RBG floor and original machine)
endif
FZX_FRAME_PROFILE_SRCS := frame_profile.c
else ifneq ($(FZX_FRAME_PROFILE),0)
$(error FZX_FRAME_PROFILE must be 0 or 1)
endif
FZX_SOURCE_ROAD_TILES ?= 0
FZX_SOURCE_ROAD_STREAM ?= 0
ifeq ($(FZX_SOURCE_ROAD_STREAM),1)
ifneq ($(FZX_SOURCE_ROAD_TILES),1)
$(error C3 stream requires the native road scene and legacy tile cache/profile disabled)
endif
else ifneq ($(FZX_SOURCE_ROAD_STREAM),0)
$(error FZX_SOURCE_ROAD_STREAM must be 0 or 1)
endif
FZX_ROAD_TILE_PROJECT_CULL ?= 0
FZX_ROAD_TILE_EDGE_REUSE ?= 0
FZX_ROAD_TILE_PROFILE ?= 0
FZX_ROAD_TILE_VERTEX_CACHE ?= 0
ifneq ($(filter $(FZX_ROAD_TILE_VERTEX_CACHE),0 1),$(FZX_ROAD_TILE_VERTEX_CACHE))
$(error FZX_ROAD_TILE_VERTEX_CACHE must be 0 or 1)
endif
ifeq ($(FZX_ROAD_TILE_VERTEX_CACHE),1)
ifneq ($(FZX_SOURCE_ROAD_TILES),1)
$(error FZX_ROAD_TILE_VERTEX_CACHE requires FZX_SOURCE_ROAD_TILES=1)
endif
endif
ifneq ($(filter $(FZX_ROAD_TILE_PROFILE),0 1),$(FZX_ROAD_TILE_PROFILE))
$(error FZX_ROAD_TILE_PROFILE must be 0 or 1)
endif
ifeq ($(FZX_ROAD_TILE_PROFILE),1)
ifneq ($(FZX_SOURCE_ROAD_TILES),1)
$(error FZX_ROAD_TILE_PROFILE requires FZX_SOURCE_ROAD_TILES=1)
endif
endif
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
ifeq ($(FZX_SOURCE_MACHINE_MENU),1)
# Keep the larger disc directory and compiled arm out of the legacy build.
SH_OUTPUT_DIR := $(SH_OUTPUT_DIR)-machine-menu
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
endif
ifeq ($(FZX_SOURCE_RESULTS),1)
SH_OUTPUT_DIR := $(SH_OUTPUT_DIR)-results
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
endif
ifeq ($(FZX_SOURCE_RECORDS),1)
SH_OUTPUT_DIR := $(SH_OUTPUT_DIR)-records
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
endif
ifeq ($(FZX_SOURCE_FINISH_MENU),1)
SH_OUTPUT_DIR := $(SH_OUTPUT_DIR)-finishmenu
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
endif
ifeq ($(FZX_SOURCE_ROAD_STREAM),1)
SH_OUTPUT_DIR := $(SH_OUTPUT_DIR)-stream
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
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

ifeq ($(FZX_SOURCE_MACHINE_MENU),1)
ifneq ($(FZX_SOURCE_COURSE_MENU):$(FZX_SOURCE_CAMERA):$(FZX_SOURCE_MACHINE),1:1:1)
$(error FZX_SOURCE_MACHINE_MENU requires original course menu, camera and machine)
endif
FZX_MACHINE_MENU_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/machine_menu/machine_menu.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/machine-menu-source/machine_menu_source.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/machine_menu_models/machine_models.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/machine_menu_camera/machine_menu_camera.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_MACHINE_MENU_RECEIPT)),)
$(error Original machine menu is not current: $(FZX_MACHINE_MENU_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_MACHINE_MENU=1 -fsigned-char -ffp-contract=off -I../generated/machine_menu -I../generated/machine-menu-source -I../generated/machine_menu_models -I../generated/machine_menu_camera
FZX_MACHINE_MENU_SRCS := race_workspace_native.c machine_menu_native.c machine_menu_models_native.c ../src/fzx_machine_menu_model.c ../generated/machine-menu-source/machine_menu_source.c
else ifneq ($(FZX_SOURCE_MACHINE_MENU),0)
$(error FZX_SOURCE_MACHINE_MENU must be 0 or 1)
endif

ifeq ($(FZX_SOURCE_COURSE_MENU),1)
ifneq ($(FZX_SOURCE_MENU),1)
$(error FZX_SOURCE_COURSE_MENU requires original title and main menu)
endif
FZX_COURSE_MENU_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/course_menu/course_menu.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/course-menu-source/course_menu_source.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/course_menu_preview/course_preview.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_COURSE_MENU_RECEIPT)),)
$(error Original course menu is not current: $(FZX_COURSE_MENU_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_COURSE_MENU=1 -fsigned-char -ffp-contract=off -I../generated/course_menu -I../generated/course-menu-source -I../generated/course_menu_preview
FZX_COURSE_MENU_SRCS := course_menu_native.c course_preview_native.c ../src/fzx_course_preview.c ../generated/course-menu-source/course_menu_source.c
else ifneq ($(FZX_SOURCE_COURSE_MENU),0)
$(error FZX_SOURCE_COURSE_MENU must be 0 or 1)
endif

ifeq ($(FZX_SOURCE_MENU),1)
ifneq ($(FZX_SOURCE_TITLE),1)
$(error FZX_SOURCE_MENU requires original title)
endif
FZX_MENU_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/menu/menu.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/menu_state/menu_source.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_MENU_RECEIPT)),)
$(error Original menu is not current: $(FZX_MENU_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_MENU=1 -I../generated/menu -I../generated/menu_state
FZX_MENU_SRCS := menu_native.c ../generated/menu_state/menu_source.c
else ifneq ($(FZX_SOURCE_MENU),0)
$(error FZX_SOURCE_MENU must be 0 or 1)
endif

ifeq ($(FZX_SOURCE_TITLE),1)
ifneq ($(FZX_SIM):$(FZX_ROM_BOOT),orig:1)
$(error FZX_SOURCE_TITLE requires the original race and local source content)
endif
FZX_TITLE_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/title/title.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/title_state/title_source.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_TITLE_RECEIPT)),)
$(error Original title is not current: $(FZX_TITLE_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_TITLE=1 -I../generated/title -I../generated/title_state
FZX_TITLE_SRCS := title_native.c ../generated/title_state/title_source.c
else ifneq ($(FZX_SOURCE_TITLE),0)
$(error FZX_SOURCE_TITLE must be 0 or 1)
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
ifneq ($(filter 1,$(FZX_SKYLINE_CD) $(FZX_HUD_CD) $(FZX_SOURCE_PLANES) $(FZX_SOURCE_TITLE)),)
FZX_SKY_SRCS += texture_cd.c
ifneq ($(FZX_SOURCE_ROAD),1)
FZX_SKY_SRCS += road_cd.c
endif
endif
ifeq ($(FZX_SOURCE_PLANES),1)
ifneq ($(FZX_SOURCE_SKY):$(FZX_SOURCE_SKYLINE):$(FZX_SKYLINE_CD):$(FZX_HUD_CD),1:1:1:1)
$(error FZX_SOURCE_PLANES=1 requires source sky/skyline and both CD asset arms)
endif
FZX_PLANES_RECEIPT := $(shell sh ../tools/check_receipt.sh $(FZX_PLANES_DIR)/planes.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_PLANES_RECEIPT)),)
$(error Source plane pack is not current: $(FZX_PLANES_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_PLANES=1 -I$(FZX_PLANES_DIR)
ifeq ($(FZX_PLANE_CLOUD_ONLY),1)
SH_CFLAGS += -DFZX_PLANE_CLOUD_ONLY=1
endif
FZX_SKY_SRCS += ../src/fzx_planes.c planes_native.c
else ifneq ($(FZX_SOURCE_PLANES),0)
$(error FZX_SOURCE_PLANES must be 0 or 1)
endif
ifeq ($(FZX_SOURCE_FLOOR_RBG),1)
ifneq ($(FZX_SOURCE_PLANES):$(FZX_SOURCE_ROAD_TILES),1:1)
$(error FZX_SOURCE_FLOOR_RBG=1 requires original planes and native road tiles)
endif
FZX_FLOOR_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/floor_rbg/floor_rbg.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_FLOOR_RECEIPT)),)
$(error Source floor rotation bank is not current: $(FZX_FLOOR_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_FLOOR_RBG=1 -I../generated/floor_rbg
FZX_SKY_SRCS += ../src/fzx_floor_rbg.c floor_rbg_native.c
else ifneq ($(FZX_SOURCE_FLOOR_RBG),0)
$(error FZX_SOURCE_FLOOR_RBG must be 0 or 1)
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
ifeq ($(FZX_SOURCE_ROAD_STREAM),1)
ifneq ($(FZX_ROAD_TILE_VERTEX_CACHE):$(FZX_ROAD_TILE_PROFILE),0:0)
$(error C3 stream excludes legacy tile cache/profile)
endif
FZX_ROAD_STREAM_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/road_bake/road_bake.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_ROAD_STREAM_RECEIPT)),)
$(error C3 road bake is stale: $(FZX_ROAD_STREAM_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_ROAD_TILES=1 -DFZX_SOURCE_ROAD_STREAM=1 -DFZX_INPUT_LOG_LOW_RAM=1 -I../generated/road_bake
FZX_ROAD_SRCS += ../src/fzx_road_stream.c ../src/fzx_road_stream_draw.c ../src/fzx_view16.c ../src/fzx_view16_center.c road_stream_native.c road_stream_commands.c
else
FZX_ROAD_TILES_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/road_tiles/road_tiles.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_ROAD_TILES_RECEIPT)),)
$(error Native road tile assets are stale: $(FZX_ROAD_TILES_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_ROAD_TILES=1 -DFZX_INPUT_LOG_LOW_RAM=1 -I../generated/road_tiles
SH_CFLAGS += -DFZX_ROAD_TILE_PROJECT_CULL=$(FZX_ROAD_TILE_PROJECT_CULL) -DFZX_ROAD_TILE_EDGE_REUSE=$(FZX_ROAD_TILE_EDGE_REUSE)
SH_CFLAGS += -DFZX_ROAD_TILE_PROFILE=$(FZX_ROAD_TILE_PROFILE) -DFZX_ROAD_TILE_VERTEX_CACHE=$(FZX_ROAD_TILE_VERTEX_CACHE)
ifeq ($(FZX_ROAD_TILE_PROFILE),1)
FZX_ROAD_SRCS += ../src/fzx_road_profile.c
endif
FZX_ROAD_SRCS += ../src/fzx_road_tiles.c road_tiles_native.c
endif
else ifneq ($(FZX_SOURCE_ROAD_TILES),0)
$(error FZX_SOURCE_ROAD_TILES must be 0 or 1)
endif
ifeq ($(FZX_SOURCE_MACHINE),1)
ifneq ($(FZX_SOURCE_CAMERA):$(FZX_SOURCE_ROAD_TILES),1:1)
$(error Source machine requires source camera and native road tiles)
endif
ifneq ($(FZX_MACHINE_REBAKE),1)
FZX_MACHINE_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/machine/machine.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_MACHINE_RECEIPT)),)
$(error Source machine assets are stale: $(FZX_MACHINE_RECEIPT))
endif
endif
SH_CFLAGS += -DFZX_SOURCE_MACHINE=1 -I../generated/machine
FZX_MACHINE_SRCS := machine_native.c ../src/fzx_machine_draw.c ../src/fzx_machine.c ../src/fzx_material.c
ifeq ($(FZX_MACHINE_REBAKE),1)
FZX_VEHICLE_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/machine_rebake/machine_rebake.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_VEHICLE_RECEIPT)),)
$(error Prepared vehicle assets are stale: $(FZX_VEHICLE_RECEIPT))
endif
SH_CFLAGS += -DFZX_MACHINE_REBAKE=1 -DFZX_VEHICLE_PROFILE=$(FZX_VEHICLE_PROFILE) -I../generated/machine_rebake
ifeq ($(FZX_VEHICLE_SUBPROFILE),1)
SH_CFLAGS += -DFZX_VEHICLE_SUBPROFILE=1
endif
ifeq ($(FZX_VEHICLE_FAST),1)
SH_CFLAGS += -DFZX_VEHICLE_FAST=1
endif
ifeq ($(FZX_VEHICLE_NATIVE),1)
SH_CFLAGS += -DFZX_VEHICLE_NATIVE=1 -DFZX_VEHICLE_FAST=1 -DFZX_VEHICLE_NATIVE_LOW_TABLES=$(FZX_VEHICLE_NATIVE_LOW_TABLES)
endif
FZX_MACHINE_SRCS := vehicle_native.c ../src/fzx_vehicle.c ../src/fzx_vehicle_draw.c ../src/fzx_view16.c
endif
else ifneq ($(FZX_SOURCE_MACHINE),0)
$(error FZX_SOURCE_MACHINE must be 0 or 1)
endif
ifeq ($(FZX_FRAME_PROFILE),1)
SH_CFLAGS += -DFZX_FRAME_PROFILE=1
endif
# Default-off C3 per-phase diagnostic; requires the stream and frame profile.
FZX_ROAD_STREAM_PHASES ?= 0
ifeq ($(FZX_ROAD_STREAM_PHASES),1)
ifneq ($(FZX_FRAME_PROFILE):$(FZX_SOURCE_ROAD_STREAM),1:1)
$(error FZX_ROAD_STREAM_PHASES requires FZX_FRAME_PROFILE=1 and FZX_SOURCE_ROAD_STREAM=1)
endif
SH_CFLAGS += -DFZX_ROAD_STREAM_PHASES=1
else ifneq ($(FZX_ROAD_STREAM_PHASES),0)
$(error FZX_ROAD_STREAM_PHASES must be 0 or 1)
endif
# Default-off C3 native road layout (boot-built chunk runs, 8-byte quads).
FZX_ROAD_STREAM_NATIVE_LAYOUT ?= 0
ifeq ($(FZX_ROAD_STREAM_NATIVE_LAYOUT),1)
ifneq ($(FZX_SOURCE_ROAD_STREAM),1)
$(error FZX_ROAD_STREAM_NATIVE_LAYOUT requires FZX_SOURCE_ROAD_STREAM=1)
endif
SH_CFLAGS += -DFZX_ROAD_STREAM_NATIVE_LAYOUT=1
# The pack path (gather/remap) is not linked under the native layout.
SH_CFLAGS += -DFZX_RS_PACK_PATH=0
else ifneq ($(FZX_ROAD_STREAM_NATIVE_LAYOUT),0)
$(error FZX_ROAD_STREAM_NATIVE_LAYOUT must be 0 or 1)
endif
# Default-off C3 direct VDP1 emit (templates patched in place, no packets).
FZX_ROAD_STREAM_EMIT ?= 0
ifeq ($(FZX_ROAD_STREAM_EMIT),1)
ifneq ($(FZX_SOURCE_ROAD_STREAM),1)
$(error FZX_ROAD_STREAM_EMIT requires FZX_SOURCE_ROAD_STREAM=1)
endif
SH_CFLAGS += -DFZX_ROAD_STREAM_EMIT=1
# The packet path is not linked under direct emit.
SH_CFLAGS += -DFZX_RS_PACKET_PATH=0
else ifneq ($(FZX_ROAD_STREAM_EMIT),0)
$(error FZX_ROAD_STREAM_EMIT must be 0 or 1)
endif
# Default-off C3 stepping/clip scratch in high work RAM.
FZX_ROAD_STREAM_HOT_HIGH ?= 0
ifeq ($(FZX_ROAD_STREAM_HOT_HIGH),1)
ifneq ($(FZX_SOURCE_ROAD_STREAM),1)
$(error FZX_ROAD_STREAM_HOT_HIGH requires FZX_SOURCE_ROAD_STREAM=1)
endif
SH_CFLAGS += -DFZX_ROAD_STREAM_HOT_HIGH=1
else ifneq ($(FZX_ROAD_STREAM_HOT_HIGH),0)
$(error FZX_ROAD_STREAM_HOT_HIGH must be 0 or 1)
endif
ifeq ($(FZX_SOURCE_COUNTDOWN),1)
ifneq ($(FZX_SIM):$(FZX_SOURCE_MACHINE):$(FZX_SOURCE_CAMERA):$(FZX_ROM_BOOT):$(FZX_PLANE_CLOUD_ONLY):$(FZX_NATIVE_CORE_EMPTY),orig:1:1:1:1:1)
$(error Original countdown requires original race/camera/machine, compact clouds and native core bypass)
endif
FZX_COUNTDOWN_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/countdown_pack/countdown_pack.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_COUNTDOWN_RECEIPT)),)
$(error Original countdown pack is stale: $(FZX_COUNTDOWN_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_COUNTDOWN=1 -I../generated/countdown_pack
FZX_COUNTDOWN_SRCS := countdown_scene.c countdown_draw_native.c ../src/fzx_countdown.c ../src/fzx_countdown_pack.c ../src/fzx_countdown_draw.c
ifneq ($(FZX_MACHINE_REBAKE),1)
FZX_COUNTDOWN_SRCS += ../src/fzx_view16.c
endif
else ifneq ($(FZX_SOURCE_COUNTDOWN),0)
$(error FZX_SOURCE_COUNTDOWN must be 0 or 1)
endif
ifeq ($(FZX_SOURCE_RESULTS),1)
ifneq ($(FZX_SOURCE_COUNTDOWN):$(FZX_SOURCE_MACHINE_MENU):$(FZX_SOURCE_HUD),1:1:1)
$(error FZX_SOURCE_RESULTS requires original countdown, machine selection and HUD)
endif
FZX_RESULTS_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/results_ui/results_ui.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/results-source/results_source.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/finish/pack/finish_pack.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_RESULTS_RECEIPT)),)
$(error Original results packs are not current: $(FZX_RESULTS_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_RESULTS=1 -fsigned-char -ffp-contract=off -I../generated/results_ui -I../generated/results-source -I../generated/finish/pack -Wno-unused-function
FZX_RESULTS_SRCS := results_scene.c results_native.c ../src/fzx_results_draw.c ../src/fzx_finish_draw.c ../src/fzx_finish_pack.c
else ifneq ($(FZX_SOURCE_RESULTS),0)
$(error FZX_SOURCE_RESULTS must be 0 or 1)
endif

ifeq ($(FZX_SOURCE_RECORDS),1)
ifneq ($(FZX_SOURCE_RESULTS),1)
$(error FZX_SOURCE_RECORDS requires original results)
endif
FZX_RECORDS_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/records_ui/records_ui.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/records-source/records_source.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/records-draw-source/records_draw_source.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/records_models/records_models.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_RECORDS_RECEIPT)),)
$(error Original records packs are not current: $(FZX_RECORDS_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_RECORDS=1 -I../generated/records_ui -I../generated/records_models
FZX_RECORDS_SRCS := records_scene.c records_native.c ../src/fzx_records_draw.c
else ifneq ($(FZX_SOURCE_RECORDS),0)
$(error FZX_SOURCE_RECORDS must be 0 or 1)
endif

ifeq ($(FZX_SOURCE_FINISH_MENU),1)
ifneq ($(FZX_SOURCE_RECORDS):$(FZX_SOURCE_FLOOR_RBG),1:1)
$(error FZX_SOURCE_FINISH_MENU requires original records and floor presentation callback)
endif
FZX_FINISH_MENU_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/finish_menu_ui/finish_menu_ui.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/finish-menu-source/finish_menu_source.receipt 2>&1 && sh ../tools/check_receipt.sh ../generated/finish-menu-draw-source/finish_menu_draw_source.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_FINISH_MENU_RECEIPT)),)
$(error Original finish-menu assets/source are not current: $(FZX_FINISH_MENU_RECEIPT))
endif
SH_CFLAGS += -DFZX_SOURCE_FINISH_MENU=1 -I../generated/finish_menu_ui
FZX_FINISH_MENU_SRCS := finish_menu_native.c
else ifneq ($(FZX_SOURCE_FINISH_MENU),0)
$(error FZX_SOURCE_FINISH_MENU must be 0 or 1)
endif

# SH2ZAM units linked into the image. The library's hand-assembled kernels and
# tables cannot be garbage-collected, so every compiled unit costs high work RAM
# whether or not it is called. Compile only the units this build calls: the base
# set is the 7de29ac (view-center-1) set; each lane adds its units on one line,
# gated by the knob whose code calls them. Sorted, so link order matches the
# former wildcard for the base set.
FZX_SH2ZAM_UNITS := sh2zam.c sh2zam_angles.c sh2zam_batch_sh2.c sh2zam_box2.c \
    sh2zam_cell16.c sh2zam_divu.c sh2zam_geometry.c sh2zam_m68k.c sh2zam_prepared.c \
    sh2zam_reuse.c sh2zam_rotation_batch.c sh2zam_shade.c sh2zam_spatial.c \
    sh2zam_transform.c sh2zam_trig.c sh2zam_view.c sh2zam_view_center.c \
    sh2zam_view_guard_src.c sh2zam_view_guard_src_center.c sh2zam_view_round.c
# C3 road stream (view-merge-2 tier planner, called by src/fzx_road_stream_draw.c).
FZX_SH2ZAM_UNITS += $(if $(filter 1,$(FZX_SOURCE_ROAD_STREAM)),sh2zam_view_merge_tier.c)
# C3 knobs that adopt shade-2 / view-camera add here, e.g.:
#   FZX_SH2ZAM_UNITS += $(if $(filter 1,$(FZX_ROAD_STREAM_<KNOB>)),sh2zam_shade_fused.c)
# C4 native vehicle arm (vehicle-kernels-1; pose-1 adds sh2zam_pose.c once tagged).
FZX_SH2ZAM_UNITS += $(if $(filter 1,$(FZX_VEHICLE_NATIVE)),sh2zam_vehicle.c sh2zam_view_indexed.c)
# Simulation knobs that adopt trig12-1 / fixed-unit-1 add here (sh2zam_trig12.c, sh2zam_unit30.c).
FZX_SH2ZAM_SRCS := $(addprefix $(SH2ZAM_ROOT)/saturn/src/,$(sort $(FZX_SH2ZAM_UNITS)))
FZX_SH2ZAM_MISSING := $(filter-out $(wildcard $(FZX_SH2ZAM_SRCS)),$(FZX_SH2ZAM_SRCS))
ifneq ($(FZX_SH2ZAM_MISSING),)
$(error SH2ZAM units missing at this pin: $(FZX_SH2ZAM_MISSING))
endif

# FZX_MENU_TABLES_LOW=1 (default 0): the four menu image tables move from high
# work RAM to reserved low work RAM, loaded once at boot from MENUTAB.PAK.
# tools/menu_tables_assets.py generates the pack and identity header before
# make (no host tools run here); make refuses a missing or stale receipt
# before compiling or staging anything. The stand-ins move above the
# reservation: regenerate sat_stubs_data.s with FZX_MENU_TABLES_LOW=1.
FZX_MENU_TABLES_LOW ?= 0
FZX_MENU_TABLES_SRCS :=
ifeq ($(FZX_MENU_TABLES_LOW),1)
ifneq ($(FZX_SOURCE_MACHINE_MENU)$(FZX_SOURCE_COURSE_MENU)$(FZX_SOURCE_RECORDS)$(FZX_SOURCE_FINISH_MENU),1111)
$(error FZX_MENU_TABLES_LOW requires the machine-menu, course-menu, records and finish-menu sources)
endif
FZX_MENU_TABLES_RECEIPT := $(shell sh ../tools/check_receipt.sh ../generated/menu_tables/menu_tables.receipt 2>&1; echo "rc=$$?")
ifeq ($(findstring rc=0,$(FZX_MENU_TABLES_RECEIPT)),)
$(error Menu tables are missing or stale; run tools/menu_tables_assets.py: $(FZX_MENU_TABLES_RECEIPT))
endif
SH_CFLAGS += -DFZX_MENU_TABLES_LOW=1 -I../generated/menu_tables
FZX_MENU_TABLES_SRCS := menu_tables_low.c
else ifneq ($(FZX_MENU_TABLES_LOW),0)
$(error FZX_MENU_TABLES_LOW must be 0 or 1)
endif

SH_SRCS := $(FZX_FINISH_MENU_SRCS) $(FZX_RECORDS_SRCS) $(FZX_RESULTS_SRCS) $(FZX_MACHINE_MENU_SRCS) $(FZX_COURSE_MENU_SRCS) $(FZX_MENU_SRCS) $(FZX_TITLE_SRCS) $(FZX_COUNTDOWN_SRCS) $(FZX_FRAME_PROFILE_SRCS) main.c ../src/fzx.c $(FZX_ORIG_SRCS) $(FZX_HUD_SRCS) $(FZX_CAMERA_SRCS) $(FZX_ROAD_SRCS) $(FZX_SKY_SRCS) $(FZX_MACHINE_SRCS) $(FZX_SH2ZAM_SRCS) $(FZX_MENU_TABLES_SRCS)
SH_CFLAGS += -DFZX_SH2_DIVU=1 -O2 -std=c11 -Wall -Wextra -I../include \
             -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1
# These tagged view units contain same-named local assembler macros. Keep their
# assembly in separate objects in both stream and reference builds.
# Yaul expands SH_CFLAGS inside eval, hence defer the source-name test once.
SH_CFLAGS += $$(if $$(filter %/sh2zam_view.c %/sh2zam_view_center.c,$$<),-fno-lto,)

ifeq ($(FZX_NATIVE_CORE_EMPTY),1)
SH_CFLAGS += -DFZX_NATIVE_CORE_EMPTY=1
endif

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
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: $(FZX_PLANES_DIR)/PLANES.PAK
pre-build-iso: stage-plane-pack
.PHONY: stage-plane-pack
stage-plane-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp $(FZX_PLANES_DIR)/PLANES.PAK $(IMAGE_DIRECTORY)/PLANES.PAK
endif

ifeq ($(FZX_SOURCE_FLOOR_RBG),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/floor_rbg/FLOORCHR.BIN ../generated/floor_rbg/FLOORMAP.BIN ../generated/floor_rbg/FLOORKOF.BIN
pre-build-iso: stage-floor-rbg
.PHONY: stage-floor-rbg
stage-floor-rbg:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/floor_rbg/FLOORCHR.BIN ../generated/floor_rbg/FLOORMAP.BIN ../generated/floor_rbg/FLOORKOF.BIN $(IMAGE_DIRECTORY)/
endif

ifeq ($(FZX_SOURCE_ROAD_TILES),1)
ifeq ($(FZX_SOURCE_ROAD_STREAM),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/road_bake/ROADLOD.PAK ../generated/road_bake/ROADLTX.PAK
pre-build-iso: stage-road-stream
.PHONY: stage-road-stream
stage-road-stream:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/road_bake/ROADLOD.PAK ../generated/road_bake/ROADLTX.PAK $(IMAGE_DIRECTORY)/
else
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/road_tiles/ROADTL.PAK ../generated/road_tiles/ROADTX.PAK
pre-build-iso: stage-road-tiles
.PHONY: stage-road-tiles
stage-road-tiles:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/road_tiles/ROADTL.PAK ../generated/road_tiles/ROADTX.PAK $(IMAGE_DIRECTORY)/
endif
endif

ifeq ($(FZX_SOURCE_MACHINE),1)
pre-build-iso: stage-machine-pack
.PHONY: stage-machine-pack
ifeq ($(FZX_MACHINE_REBAKE),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/machine_rebake/MACHRLD.PAK ../generated/machine_rebake/MACHRTX.PAK
stage-machine-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/machine_rebake/MACHRLD.PAK $(IMAGE_DIRECTORY)/MACHINE.PAK
	cp ../generated/machine_rebake/MACHRTX.PAK $(IMAGE_DIRECTORY)/MACHTX.PAK
else
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/machine/MACHINE.PAK ../generated/machine/MACHTX.PAK
stage-machine-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/machine/MACHINE.PAK ../generated/machine/MACHTX.PAK $(IMAGE_DIRECTORY)/
endif
endif

ifeq ($(FZX_SOURCE_COUNTDOWN),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/countdown_pack/COUNTDN.PAK ../generated/countdown_pack/COUNTDTX.PAK
pre-build-iso: stage-countdown-pack
.PHONY: stage-countdown-pack
stage-countdown-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/countdown_pack/COUNTDN.PAK ../generated/countdown_pack/COUNTDTX.PAK $(IMAGE_DIRECTORY)/
endif

ifeq ($(FZX_SOURCE_TITLE),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/title/TITLE.PAK
pre-build-iso: stage-title-pack
.PHONY: stage-title-pack
stage-title-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/title/TITLE.PAK $(IMAGE_DIRECTORY)/TITLE.PAK
endif

ifeq ($(FZX_SOURCE_MENU),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/menu/MENU.PAK
pre-build-iso: stage-menu-pack
.PHONY: stage-menu-pack
stage-menu-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/menu/MENU.PAK $(IMAGE_DIRECTORY)/MENU.PAK
endif

ifeq ($(FZX_SOURCE_COURSE_MENU),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/course_menu/COURSEUI.PAK ../generated/course_menu_preview/CPREVIEW.PAK
pre-build-iso: stage-course-menu-pack
.PHONY: stage-course-menu-pack
stage-course-menu-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/course_menu/COURSEUI.PAK ../generated/course_menu_preview/CPREVIEW.PAK $(IMAGE_DIRECTORY)/
endif

ifeq ($(FZX_SOURCE_MACHINE_MENU),1)
FZX_MACHINE_MENU_PACKS := $(wildcard ../generated/machine_menu/*.PAK) $(wildcard ../generated/machine_menu_models/*.PAK)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: $(FZX_MACHINE_MENU_PACKS)
pre-build-iso: stage-machine-menu-pack
.PHONY: stage-machine-menu-pack
stage-machine-menu-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp $(FZX_MACHINE_MENU_PACKS) $(IMAGE_DIRECTORY)/
endif

ifeq ($(FZX_SOURCE_RESULTS),1)
FZX_RESULTS_PACKS := ../generated/results_ui/RESULTUI.PAK ../generated/results_ui/RESTINT.PAK ../generated/finish/pack/FINISH.PAK ../generated/finish/pack/FINISHTX.PAK
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: $(FZX_RESULTS_PACKS)
pre-build-iso: stage-results-pack
.PHONY: stage-results-pack
stage-results-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp $(FZX_RESULTS_PACKS) $(IMAGE_DIRECTORY)/
endif

ifeq ($(FZX_SOURCE_RECORDS),1)
FZX_RECORDS_PACKS := ../generated/records_ui/RECORDUI.PAK ../generated/records_models/RMODELS.PAK
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: $(FZX_RECORDS_PACKS)
pre-build-iso: stage-records-pack
.PHONY: stage-records-pack
stage-records-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp $(FZX_RECORDS_PACKS) $(IMAGE_DIRECTORY)/
endif

ifeq ($(FZX_SOURCE_FINISH_MENU),1)
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/finish_menu_ui/FINMENU.PAK
pre-build-iso: stage-finish-menu-pack
.PHONY: stage-finish-menu-pack
stage-finish-menu-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/finish_menu_ui/FINMENU.PAK $(IMAGE_DIRECTORY)/
endif

ifeq ($(FZX_MENU_TABLES_LOW),1)
# The identity header is a normal prerequisite of everything that reads it.
FZX_MENU_TABLES_USERS := main.c menu_tables_low.c machine_menu_native.c course_menu_native.c finish_menu_native.c records_native.c
$(foreach SRC,$(FZX_MENU_TABLES_USERS),$(eval $(call macro-convert-build-path,$(addsuffix .o,$(basename $(SRC)))): ../generated/menu_tables/menu_tables_identity.h))
$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: ../generated/menu_tables/MENUTAB.PAK
pre-build-iso: stage-menu-tables-pack
.PHONY: stage-menu-tables-pack
stage-menu-tables-pack:
	mkdir -p $(IMAGE_DIRECTORY)
	cp ../generated/menu_tables/MENUTAB.PAK $(IMAGE_DIRECTORY)/MENUTAB.PAK
else
# OFF: a staging directory reused from an ON build must not ship its pack.
pre-build-iso: unstage-menu-tables-pack
.PHONY: unstage-menu-tables-pack
unstage-menu-tables-pack:
	rm -f $(IMAGE_DIRECTORY)/MENUTAB.PAK
endif
