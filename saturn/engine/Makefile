# Engine image (docs/ENGINE_PLAN.md), milestone E0: track flythrough.
# A separate Saturn program with its own main loop. It links no original game
# code: no reference closure, no stand-ins, no race bridge. The legacy image is
# built by ../Makefile and is not touched by anything here.
#
#   make -C saturn/engine SH2ZAM_ROOT=<checkout of the engine pin> [FZX_VDP1_DBANK=0|1]
#
# The SH2ZAM pin is saturn/engine/sh2zam.pin (branch claude/e1-train: engine-train plus vehicle-dynamics-1 and sprite-cache-1).
# Assets come from ../../generated (owning generators, receipts checked here).
ifeq ($(strip $(YAUL_INSTALL_ROOT)),)
$(error Undefined YAUL_INSTALL_ROOT (install root directory))
endif

SH_PROGRAM := fzs-engine
FZX_VDP1_DBANK ?= 1
FZX_VDP1_DBANK_STALE ?= 0
# Road guard band: pieces reaching more than this many pixels past the view
# are clipped on the CPU instead of rasterised off screen by VDP1.
E0_GUARD_MARGIN ?= 256
# Diagnostic: per-phase totals of the track build (changes timing; never measured for the floor).
E0_PHASES ?= 0
# Whole-quad winding cull without a clip when no corner is behind the near plane.
E0_FAST_CULL ?= 1
# Coarse road tiers reach 2^N times nearer (0 = as legacy).
E0_LOD_SHIFT ?= 0
# 1: VBlank callbacks keep MACH/MACL (the BIOS dispatcher does not); 0 is the control.
E0_ISR_MAC_SAVE ?= 1
# Diagnostic: per-frame check for multiply registers changed under the VBlank interrupt.
E0_MAC_PROBE ?= 0
# E1 drive slice (engine_drive.c): native physics, chase camera, sprite player, HUD.
# Needs generated/physics (tools/engine_drive_assets.py, local only) and
# generated/racer_sprites (tools/racer_sprites.py), and an SH2ZAM pin with
# vehicle-dynamics-1 and rotation-sprites-1 (claude/e1-train).
FZX_E1_DRIVE ?= 0
# 1: the R01 test driver steers from boot (captures); 0: the pad (owner).
E1_DRIVER ?= 0
# Test driver boost period in ticks (0 = R01; 240 = R02).
E1_DRIVER_BOOST ?= 0
# Skip zero-area fan pieces of clipped road polygons (the tick-3000 seam).
E0_FAN_FIX ?= 1
# Floor/cloud fog bands: near/far 3D clip + 2D clip at the view, no diagnostic hash.
E0_RBG_LEAN ?= 1
ifeq ($(filter 0 1,$(FZX_VDP1_DBANK)),)
$(error FZX_VDP1_DBANK must be 0 or 1)
endif
SH_OUTPUT_DIR := build-$(if $(filter 1,$(FZX_E1_DRIVE)),e1-d$(E1_DRIVER)$(if $(filter-out 0,$(E1_DRIVER_BOOST)),-b$(E1_DRIVER_BOOST),)-,e0-)bank$(FZX_VDP1_DBANK)$(if $(filter 1,$(FZX_VDP1_DBANK_STALE)),-stale,)-g$(E0_GUARD_MARGIN)-c$(E0_FAST_CULL)-l$(E0_LOD_SHIFT)-f$(E0_FAN_FIX)-r$(E0_RBG_LEAN)$(if $(filter 0,$(E0_ISR_MAC_SAVE)),-nomac,)$(if $(filter 1,$(E0_MAC_PROBE)),-macprobe,)$(if $(filter 1,$(E0_PHASES)),-phases,)
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
IMAGE_1ST_READ_BIN := A.BIN

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH2ZAM_ROOT ?=
ifeq ($(wildcard $(SH2ZAM_ROOT)/saturn/include/sh2zam/segmesh_experimental.h),)
$(error Set SH2ZAM_ROOT to a checkout of the engine pin (saturn/engine/sh2zam.pin))
endif

G := ../../generated
E0_RECEIPTS := engine_path/engine_path.receipt road_pack/road_pack.receipt road_bake/road_bake.receipt \
               skyline/skyline.receipt sky/sky.receipt planes_cloud/planes.receipt floor_rbg/floor_rbg.receipt
E0_RECEIPT_CHECK := $(foreach R,$(E0_RECEIPTS),$(shell sh ../../tools/check_receipt.sh $(G)/$(R) >/dev/null 2>&1 || echo $(R)))
ifeq ($(FZX_E1_DRIVE),1)
E1_RECEIPT_CHECK := $(foreach R,physics/physics.receipt racer_sprites/racer_sprites.receipt,$(shell sh ../../tools/check_receipt.sh $(G)/$(R) >/dev/null 2>&1 || echo $(R)))
ifneq ($(strip $(E1_RECEIPT_CHECK)),)
$(error Missing or stale E1 assets; run their generators: $(E1_RECEIPT_CHECK))
endif
ifeq ($(wildcard $(SH2ZAM_ROOT)/saturn/include/sh2zam/vehicle_dynamics_experimental.h),)
$(error FZX_E1_DRIVE=1 needs an SH2ZAM pin with vehicle-dynamics-1 (claude/e1-train))
endif
endif
ifneq ($(strip $(E0_RECEIPT_CHECK)),)
$(error Missing or stale assets; run their generators: $(E0_RECEIPT_CHECK))
endif

SH2ZAM_UNITS := sh2zam.c sh2zam_angles.c sh2zam_batch_sh2.c sh2zam_box2.c sh2zam_cell16.c sh2zam_divu.c \
    sh2zam_geometry.c sh2zam_m68k.c sh2zam_prepared.c sh2zam_reuse.c sh2zam_rotation_batch.c sh2zam_shade.c \
    sh2zam_spatial.c sh2zam_transform.c sh2zam_trig.c sh2zam_view.c sh2zam_view_center.c \
    sh2zam_view_guard_src.c sh2zam_view_guard_src_center.c sh2zam_view_round.c \
    sh2zam_view_merge_tier.c sh2zam_shade_fused.c sh2zam_view_rails.c sh2zam_segmesh.c sh2zam_vdp1_banks.c     $(if $(filter 1,$(FZX_E1_DRIVE)),sh2zam_vehicle_dynamics.c sh2zam_unit30.c sh2zam_rotation_sprite.c sh2zam_sprite_cache.c sh2zam_vlc2.c,)

SH_SRCS := engine_main.c engine_scene.c engine_path.c \
    ../sky_native.c ../skyline_native.c ../planes_native.c ../floor_rbg_native.c ../clouds_vdp2_native.c \
    ../road_native.c ../road_cd.c ../road_stream_native.c ../road_stream_commands.c ../slave_road.c ../texture_cd.c \
    ../../src/fzx_view.c ../../src/fzx_view16.c ../../src/fzx_view16_center.c ../../src/fzx_sky.c ../../src/fzx_skyline.c ../../src/fzx_planes.c \
    ../../src/fzx_floor_rbg.c ../../src/fzx_clouds_rbg.c ../../src/fzx_rbg_fast.c \
    ../../src/fzx_road_pack.c ../../src/fzx_road_stream.c ../../src/fzx_road_stream_draw.c \
    $(G)/course_record.c     $(if $(filter 1,$(FZX_E1_DRIVE)),engine_drive.c ../physics_native.c,) \
    $(addprefix $(SH2ZAM_ROOT)/saturn/src/,$(sort $(SH2ZAM_UNITS)))

SH_CFLAGS += -DFZX_ENGINE=1 -DFZX_FRAME_PROFILE=1 -DFZX_VDP1_DBANK=$(FZX_VDP1_DBANK) \
    -DFZX_VDP1_DBANK_STALE=$(FZX_VDP1_DBANK_STALE) -DFZX_ROAD_STREAM_GUARD_MARGIN=$(E0_GUARD_MARGIN) \
    -DFZX_ROAD_STREAM_PHASES=$(E0_PHASES) -DFZX_SCENE_PHASES=$(E0_PHASES) -DFZX_ROAD_STREAM_FAST_CULL=$(E0_FAST_CULL) -DFZX_ROAD_STREAM_LOD_SHIFT=$(E0_LOD_SHIFT) -DFZX_ROAD_STREAM_FAN_FIX=$(E0_FAN_FIX) -DFZX_RBG_LEAN=$(E0_RBG_LEAN) \
    -DFZX_SOURCE_SKY=1 -I$(G)/sky -DFZX_SKY_SIMPLIFY=1 -DFZX_SOURCE_SKYLINE=1 -I$(G)/skyline -DFZX_SKYLINE_CD=1 \
    -DFZX_SOURCE_PLANES=1 -I$(G)/planes_cloud -DFZX_PLANE_CLOUD_ONLY=1 -DFZX_PLANES_DEGENERATE_UV=1 \
    -DFZX_SOURCE_FLOOR_RBG=1 -I$(G)/floor_rbg -DFZX_CLOUDS_VDP2=1 \
    -DFZX_SOURCE_ROAD=1 -I$(G)/road_pack -DFZX_SOURCE_ROAD_TILES=1 -DFZX_SOURCE_ROAD_STREAM=1 -I$(G)/road_bake \
    -DFZX_ROAD_STREAM_NATIVE_LAYOUT=1 -DFZX_RS_PACK_PATH=0 -DFZX_ROAD_STREAM_EMIT=1 -DFZX_RS_PACKET_PATH=0 \
    -DFZX_ROAD_STREAM_BUDGET=1 -DFZX_RBG_FAST=1 -DFZX_SLAVE_ROAD=1 -DFZX_ROAD_LOD=1 \
    -I$(G)/engine_path -DFZX_SH2_DIVU=1 -O2 -std=c11 -Wall -Wextra \
    -I. -I.. -I../../include -I../../ref -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1 \
    -ffunction-sections -fdata-sections     -DE0_ISR_MAC_SAVE=$(E0_ISR_MAC_SAVE) -DE0_MAC_PROBE=$(E0_MAC_PROBE) -DFZX_E1_DRIVE=$(FZX_E1_DRIVE) -DE1_DRIVER=$(E1_DRIVER) -DE1_DRIVER_BOOST=$(E1_DRIVER_BOOST)     $(if $(filter 1,$(FZX_E1_DRIVE)),-I$(G)/physics -I$(G)/racer_sprites,)
# As ../Makefile: these SH2ZAM units carry same-named assembler macros.
SH_CFLAGS += $$(if $$(filter %/sh2zam_view.c %/sh2zam_view_center.c,$$<),-fno-lto,)

# Low work RAM layout of this image: the path pack at 0x00200000 (0x8C000
# reserved), then the CD scratch (4,096 B), then a 12,288-word block the track
# workspace's loader expects ahead of it. The reused loaders find these through
# the two symbols below.
SH_LDFLAGS += -Wl,--defsym=_sat_stub_data_end=0x0028C000 -Wl,--defsym=_fzx_input_log=0x0028D000
# Unused functions and data are dropped at link time (SH2ZAM sections-1).
SH_LDFLAGS += -Wl,--gc-sections

IP_VERSION := V0.001
IP_RELEASE_DATE := 20261119
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := $(if $(filter 1,$(FZX_E1_DRIVE)),Engine E1 drive,Engine E0 track flythrough)
IP_MASTER_STACK_ADDR := 0x06100000
# Slave stack: the 8 KB IP.BIN area 0x06002000..0x06004000, guarded (../slave_road.c).
IP_SLAVE_STACK_ADDR := 0x06004000
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: $(G)/engine_path/ENGPATH.PAK
pre-build-iso: stage-e0-packs
.PHONY: stage-e0-packs
stage-e0-packs:
	mkdir -p "$(IMAGE_DIRECTORY)"
	cp $(G)/engine_path/ENGPATH.PAK $(G)/road_pack/ROAD.PAK $(G)/road_bake/ROADLOD.PAK $(G)/road_bake/ROADLTX.PAK \
	   $(G)/skyline/SKYLINE.PAK $(G)/planes_cloud/PLANES.PAK \
	   $(G)/floor_rbg/FLOORCHR.BIN $(G)/floor_rbg/FLOORMAP.BIN $(G)/floor_rbg/FLOORKOF.BIN "$(IMAGE_DIRECTORY)/"
ifeq ($(FZX_E1_DRIVE),1)
	cp $(G)/physics/PHYS.PAK $(G)/racer_sprites/RSPRITE.PAK "$(IMAGE_DIRECTORY)/"
endif
