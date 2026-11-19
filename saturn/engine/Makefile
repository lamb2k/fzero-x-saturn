# Engine image (docs/ENGINE_PLAN.md), milestone E0: track flythrough.
# A separate Saturn program with its own main loop. It links no original game
# code: no reference closure, no stand-ins, no race bridge. The legacy image is
# built by ../Makefile and is not touched by anything here.
#
#   make -C saturn/engine SH2ZAM_ROOT=<checkout of the engine pin> [FZX_VDP1_DBANK=0|1]
#
# The SH2ZAM pin is saturn/engine/sh2zam.pin (branch claude/e2-field-train: claude/e1-train plus segmesh-2 and line-field-1).
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
# Coarse road tiers reach 2^N times nearer (0 = as legacy). Owner 2026-11-19: "go with shift 2".
E0_LOD_SHIFT ?= 2
# 1: VBlank callbacks keep MACH/MACL (the BIOS dispatcher does not); 0 is the control.
E0_ISR_MAC_SAVE ?= 1
# Diagnostic: per-frame check for multiply registers changed under the VBlank interrupt.
E0_MAC_PROBE ?= 0
# 1: wait for a due floor/cloud present before queueing the next bank; 0 is the control.
E0_PRESENT_WAIT ?= 1
# E1 drive slice (engine_drive.c): native physics, chase camera, sprite player, HUD.
# Needs generated/physics (tools/engine_drive_assets.py, local only) and
# generated/racer_sprites (tools/racer_sprites.py), and an SH2ZAM pin with
# vehicle-dynamics-1 and rotation-sprites-1 (claude/e1-train).
FZX_E1_DRIVE ?= 0
# 1: the R01 test driver steers from boot (captures); 0: the pad (owner).
E1_DRIVER ?= 0
# Test driver boost period in ticks (0 = R01; 240 = R02).
E1_DRIVER_BOOST ?= 0
# Close-up A/B (E1 only; generated/CLOSEUP-AB.md): 0 the sprite player (the control,
# unchanged); 1 the player as the 3D model (engine_closeup.c: the prepared vehicle
# pack, vehicle-kernels-1 and pose-1); 2 the high-quality near sprite set
# (generated/racer_sprites_hq: tools/racer_sprites.py --views tools/racer_sprites_hq.json).
E1_CLOSEUP ?= 0
# Arm A only: 1 builds the vehicle's commands while the slave builds the track (staged,
# copied after the shadow); 0 builds them after the track, as the sprite.
E1_CLOSEUP_EARLY ?= 0
# The 3D vehicle's generated folders (stills of other machines: machine_rebake_fNN / closeup_fNN).
E1_REBAKE ?= machine_rebake
E1_VEHICLE ?= closeup
# Close-up 3D-N (generated/CLOSEUP-3DN.md): N rivals (1..29) on the SH2ZAM line field, the
# nearest K cars within D drawn in 3D (K, D: run-time words fzx_field_k, fzx_field_d).
# Needs E1_CLOSEUP=1 E1_CLOSEUP_EARLY=1 and an SH2ZAM pin with line-field-1.
E1_FIELD ?= 0
# E2 field lane (generated/E2-FIELD.md), with E1_FIELD only; each 0 = the 3D-N field as it was.
# E2_PACK: the E2 rival line (RIVALS.PAK: CPU line laterals, speed caps, widths) and the reference
#   GP field config (generated/rivals: tools/rivals_assets.py, ref/rivals_main.c, tools/rivals_field_header.py).
# E2_FASTPOSE: rival poses from the stations' rows interpolated (no renormalisation, no square roots).
# E2_CULL: cheap reject before any sprite work (behind, past the draw distance, off screen);
#   shadows only for near cars.
# E2_LOD: the field's distance level of detail at E2_LOD_NEAR (units: full rate, 1/2, 1/4; else 1/8).
# E2_EARLY: every car (3D and sprites) is built before the slave's kick and the master's field
#   wait (staged), so after the VBlank the master only runs the prelude and one copy.
E2_PACK ?= 0
E2_FASTPOSE ?= 0
E2_CULL ?= 0
E2_LOD ?= 0
E2_LOD_NEAR ?= 1000,3000,8000
E2_EARLY ?= 0
# Diagnostic: per-frame master phase times (fzx_e2_prof); changes timing slightly, never a floor measurement.
E2_PROF ?= 0
# E2_FASTSPRITE (needs E2_CULL): rival sprite poses reuse the cull's camera-space terms; one division for the up vector.
E2_FASTSPRITE ?= 0
# E2_ROSTER (needs E2_PACK, E2_CULL, E2_FASTSPRITE, E2_EARLY): each rival drawn from its own machine's
#   near/far rotation-sprite sets in the private roster pack (generated/roster/ROSTER.PAK and
#   roster_identity.h from tools/roster_identity.py, which refuses a pack with the wrong SHA-256).
E2_ROSTER ?= 0
# Skip zero-area fan pieces of clipped road polygons (the tick-3000 seam).
E0_FAN_FIX ?= 1
# Floor/cloud fog bands: near/far 3D clip + 2D clip at the view, no diagnostic hash.
E0_RBG_LEAN ?= 1
# Baked cluster bounds (ROADCL.PAK, SH2ZAM segmesh-2): skip quads of culled facing classes.
E0_CLUSTERS ?= 0
# Track build tables (work, projected vertices, native layout) in high work RAM.
E0_WORK_HIGH ?= 1
# Visible chunk cap N (0 = off). Owner 2026-11-19: "Yes stable cap 26 should be the default" (was "cap 32").
E0_CHUNK_CAP ?= 26
# Road piece clip: near plane in 3D, then the guard rectangle in 2D.
E0_LEAN_CLIP ?= 1
# Exact fog early-out: pieces wholly before the first fog threshold reuse one shading result per image.
E0_FOG_EARLY ?= 1
# Hardware disc only: on-screen presented-frame counters, looping flythrough.
E0_HW_COUNTER ?= 0
# Diagnostics: VDP2 register/CRAM capture for the map; low-RAM sentinel overwrite control.
E0_VDP2_MAP ?= 0
E0_SENTINEL_CONTROL ?= 0
# Diagnostic: per-frame marked and drawn chunk maps (pop counting).
E0_POP_LOG ?= 0
# Stable chunk cap: rank by depth, keep the nearest E0_CHUNK_CAP with hysteresis.
E0_CAP_STABLE ?= 1
# Diagnostic (needs E0_PHASES=1): draw skipped quads anyway and count any that would draw.
E0_CLUSTER_AUDIT ?= 0
ifeq ($(filter 0 1,$(FZX_VDP1_DBANK)),)
$(error FZX_VDP1_DBANK must be 0 or 1)
endif
ifeq ($(filter 0 1 2,$(E1_CLOSEUP)),)
$(error E1_CLOSEUP must be 0, 1 or 2)
endif
ifneq ($(E1_CLOSEUP),0)
ifneq ($(FZX_E1_DRIVE),1)
$(error E1_CLOSEUP needs FZX_E1_DRIVE=1)
endif
endif
ifneq ($(E1_CLOSEUP_EARLY),0)
ifneq ($(E1_CLOSEUP_EARLY):$(E1_CLOSEUP),1:1)
$(error E1_CLOSEUP_EARLY=1 needs E1_CLOSEUP=1)
endif
endif
ifneq ($(E1_FIELD),0)
ifneq ($(E1_CLOSEUP):$(E1_CLOSEUP_EARLY),1:1)
$(error E1_FIELD needs E1_CLOSEUP=1 E1_CLOSEUP_EARLY=1)
endif
endif
# The racer sprite set the player is drawn from.
E1_SPRITES := $(if $(filter 2,$(E1_CLOSEUP)),racer_sprites_hq,racer_sprites)
comma := ,
SH_OUTPUT_DIR := build-$(if $(filter 1,$(FZX_E1_DRIVE)),e1-d$(E1_DRIVER)$(if $(filter-out 0,$(E1_DRIVER_BOOST)),-b$(E1_DRIVER_BOOST),)$(if $(filter-out 0,$(E1_CLOSEUP)),-ab$(E1_CLOSEUP)$(if $(filter 1,$(E1_CLOSEUP_EARLY)),e,)$(if $(filter-out 0,$(E1_FIELD)),-f$(E1_FIELD)$(if $(filter 1,$(E2_PACK)),p,)$(if $(filter 1,$(E2_FASTPOSE)),q,)$(if $(filter 1,$(E2_CULL)),c,)$(if $(filter 1,$(E2_LOD)),l$(subst $(comma),_,$(E2_LOD_NEAR)),)$(if $(filter 1,$(E2_EARLY)),e,)$(if $(filter 1,$(E2_FASTSPRITE)),f,)$(if $(filter 1,$(E2_ROSTER)),r,)$(if $(filter 1,$(E2_PROF)),P,),),)-,e0-)bank$(FZX_VDP1_DBANK)$(if $(filter 1,$(FZX_VDP1_DBANK_STALE)),-stale,)-g$(E0_GUARD_MARGIN)-c$(E0_FAST_CULL)-l$(E0_LOD_SHIFT)-f$(E0_FAN_FIX)-r$(E0_RBG_LEAN)-k$(E0_CLUSTERS)-w$(E0_WORK_HIGH)$(if $(filter-out 0,$(E0_CHUNK_CAP)),-cap$(E0_CHUNK_CAP),)-n$(E0_LEAN_CLIP)-e$(E0_FOG_EARLY)$(if $(filter 1,$(E0_HW_COUNTER)),-hw,)$(if $(filter 1,$(E0_VDP2_MAP)),-map,)$(if $(filter 1,$(E0_SENTINEL_CONTROL)),-sentinel,)$(if $(filter 1,$(E0_POP_LOG)),-pop,)$(if $(filter 1,$(E0_CAP_STABLE)),-stable,)$(if $(filter 1,$(E0_CLUSTER_AUDIT)),-audit,)$(if $(filter 1,$(E0_PHASES)),-phases,)
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
               skyline/skyline.receipt sky/sky.receipt planes_cloud/planes.receipt floor_rbg/floor_rbg.receipt \
               $(if $(filter 1,$(E0_CLUSTERS)),road_cluster/road_cluster.receipt,)
E0_RECEIPT_CHECK := $(foreach R,$(E0_RECEIPTS),$(shell sh ../../tools/check_receipt.sh $(G)/$(R) >/dev/null 2>&1 || echo $(R)))
ifeq ($(FZX_E1_DRIVE),1)
E1_RECEIPT_CHECK := $(foreach R,physics/physics.receipt $(E1_SPRITES)/racer_sprites.receipt $(if $(filter 1,$(E1_CLOSEUP)),$(E1_REBAKE)/machine_rebake.receipt $(E1_VEHICLE)/closeup.receipt,),$(shell sh ../../tools/check_receipt.sh $(G)/$(R) >/dev/null 2>&1 || echo $(R)))
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
    sh2zam_view_merge_tier.c sh2zam_shade_fused.c sh2zam_view_rails.c sh2zam_segmesh.c sh2zam_segmesh_cull.c sh2zam_vdp1_banks.c \
    $(if $(filter 1,$(FZX_E1_DRIVE)),sh2zam_vehicle_dynamics.c sh2zam_unit30.c sh2zam_rotation_sprite.c sh2zam_sprite_cache.c sh2zam_vlc2.c,) \
    $(if $(filter 1,$(E1_CLOSEUP)),sh2zam_vehicle.c sh2zam_view_indexed.c sh2zam_pose.c,) \
    $(if $(filter-out 0,$(E1_FIELD)),sh2zam_line_follower.c sh2zam_line_field.c,)

SH_SRCS := engine_main.c engine_scene.c engine_path.c \
    ../sky_native.c ../skyline_native.c ../planes_native.c ../floor_rbg_native.c ../clouds_vdp2_native.c \
    ../road_native.c ../road_cd.c ../road_stream_native.c ../road_stream_commands.c ../slave_road.c ../texture_cd.c \
    ../../src/fzx_view.c ../../src/fzx_view16.c ../../src/fzx_view16_center.c ../../src/fzx_sky.c ../../src/fzx_skyline.c ../../src/fzx_planes.c \
    ../../src/fzx_floor_rbg.c ../../src/fzx_clouds_rbg.c ../../src/fzx_rbg_fast.c \
    ../../src/fzx_road_pack.c ../../src/fzx_road_stream.c ../../src/fzx_road_stream_draw.c \
    $(G)/course_record.c \
    $(if $(filter 1,$(FZX_E1_DRIVE)),engine_drive.c ../physics_native.c,) \
    $(if $(filter 1,$(E1_CLOSEUP)),engine_closeup.c ../../src/fzx_vehicle.c ../../src/fzx_vehicle_draw.c,) \
    $(if $(filter-out 0,$(E1_FIELD)),engine_field.c,) \
    $(addprefix $(SH2ZAM_ROOT)/saturn/src/,$(sort $(SH2ZAM_UNITS)))

SH_CFLAGS += -DFZX_ENGINE=1 -DFZX_FRAME_PROFILE=1 -DFZX_VDP1_DBANK=$(FZX_VDP1_DBANK) \
    -DFZX_VDP1_DBANK_STALE=$(FZX_VDP1_DBANK_STALE) -DFZX_ROAD_STREAM_GUARD_MARGIN=$(E0_GUARD_MARGIN) \
    -DFZX_ROAD_STREAM_PHASES=$(E0_PHASES) -DFZX_SCENE_PHASES=$(E0_PHASES) -DFZX_ROAD_STREAM_FAST_CULL=$(E0_FAST_CULL) -DFZX_ROAD_STREAM_LOD_SHIFT=$(E0_LOD_SHIFT) -DFZX_ROAD_STREAM_FAN_FIX=$(E0_FAN_FIX) -DFZX_RBG_LEAN=$(E0_RBG_LEAN) \
    -DFZX_ROAD_STREAM_CLUSTERS=$(E0_CLUSTERS) -DFZX_ROAD_STREAM_CLUSTER_AUDIT=$(E0_CLUSTER_AUDIT) -I$(G)/road_cluster -DFZX_ROAD_STREAM_WORK_HIGH=$(E0_WORK_HIGH) -DE0_CHUNK_CAP=$(E0_CHUNK_CAP) -DFZX_ROAD_STREAM_LEAN_CLIP=$(E0_LEAN_CLIP) -DFZX_ROAD_STREAM_FOG_EARLY=$(E0_FOG_EARLY) -DE0_HW_COUNTER=$(E0_HW_COUNTER) -DE0_VDP2_MAP=$(E0_VDP2_MAP) -DE0_SENTINEL_CONTROL=$(E0_SENTINEL_CONTROL) -DE0_POP_LOG=$(E0_POP_LOG) -DE0_CAP_STABLE=$(E0_CAP_STABLE) \
    -DFZX_SOURCE_SKY=1 -I$(G)/sky -DFZX_SKY_SIMPLIFY=1 -DFZX_SOURCE_SKYLINE=1 -I$(G)/skyline -DFZX_SKYLINE_CD=1 \
    -DFZX_SOURCE_PLANES=1 -I$(G)/planes_cloud -DFZX_PLANE_CLOUD_ONLY=1 -DFZX_PLANES_DEGENERATE_UV=1 \
    -DFZX_SOURCE_FLOOR_RBG=1 -I$(G)/floor_rbg -DFZX_CLOUDS_VDP2=1 \
    -DFZX_SOURCE_ROAD=1 -I$(G)/road_pack -DFZX_SOURCE_ROAD_TILES=1 -DFZX_SOURCE_ROAD_STREAM=1 -I$(G)/road_bake \
    -DFZX_ROAD_STREAM_NATIVE_LAYOUT=1 -DFZX_RS_PACK_PATH=0 -DFZX_ROAD_STREAM_EMIT=1 -DFZX_RS_PACKET_PATH=0 \
    -DFZX_ROAD_STREAM_BUDGET=1 -DFZX_RBG_FAST=1 -DFZX_SLAVE_ROAD=1 -DFZX_ROAD_LOD=1 \
    -I$(G)/engine_path -DFZX_SH2_DIVU=1 -O2 -std=c11 -Wall -Wextra \
    -I. -I.. -I../../include -I../../ref -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1 \
    -ffunction-sections -fdata-sections \
    -DE0_ISR_MAC_SAVE=$(E0_ISR_MAC_SAVE) -DE0_MAC_PROBE=$(E0_MAC_PROBE) -DE0_PRESENT_WAIT=$(E0_PRESENT_WAIT) -DFZX_E1_DRIVE=$(FZX_E1_DRIVE) -DE1_DRIVER=$(E1_DRIVER) -DE1_DRIVER_BOOST=$(E1_DRIVER_BOOST) \
    $(if $(filter 1,$(FZX_E1_DRIVE)),-I$(G)/physics -I$(G)/$(E1_SPRITES),)
# As ../Makefile: these SH2ZAM units carry same-named assembler macros.
SH_CFLAGS += $$(if $$(filter %/sh2zam_view.c %/sh2zam_view_center.c,$$<),-fno-lto,)
ifneq ($(E1_CLOSEUP),0)
SH_CFLAGS += -DE1_CLOSEUP=$(E1_CLOSEUP) -DE1_CLOSEUP_EARLY=$(E1_CLOSEUP_EARLY)
ifneq ($(E1_FIELD),0)
# FZX_CLOSEUP_CARS: 3D slots (engine_field.h FZX_FIELD_K_MAX).
SH_CFLAGS += -DE1_FIELD=$(E1_FIELD) -DFZX_CLOSEUP_CARS=6
SH_CFLAGS += -DE2_PACK=$(E2_PACK) -DE2_FASTPOSE=$(E2_FASTPOSE) -DE2_CULL=$(E2_CULL) -DE2_LOD=$(E2_LOD) -DE2_LOD_NEAR=$(E2_LOD_NEAR) -DE2_EARLY=$(E2_EARLY) -DE2_PROF=$(E2_PROF) -DE2_FASTSPRITE=$(E2_FASTSPRITE) -DE2_ROSTER=$(E2_ROSTER)
ifeq ($(E2_PACK),1)
SH_CFLAGS += -I$(G)/rivals
endif
ifeq ($(E2_ROSTER),1)
ifneq ($(E2_PACK):$(E2_CULL):$(E2_FASTSPRITE):$(E2_EARLY),1:1:1:1)
$(error E2_ROSTER needs E2_PACK=1 E2_CULL=1 E2_FASTSPRITE=1 E2_EARLY=1)
endif
ifeq ($(wildcard $(G)/roster/roster_identity.h),)
$(error E2_ROSTER needs generated/roster/roster_identity.h (tools/roster_identity.py ROSTER.PAK SHA256 OUT))
endif
SH_CFLAGS += -I$(G)/roster
endif
endif
endif
ifeq ($(E1_CLOSEUP),1)
# Arm A: the legacy native vehicle path (FZX_VEHICLE_NATIVE + FZX_VEHICLE_POSE1) for
# the vehicle files only. RefMachine and RefCamera are only the draw's input layouts;
# no original code is linked (engine_closeup.c fills them from the native physics).
# As ../Makefile: view_indexed shares assembler names with view_rails, and pose-1's
# assembly reads a file-static table, so both stay out of LTO.
SH_CFLAGS += -I$(G)/$(E1_REBAKE) -I$(G)/$(E1_VEHICLE)
# 16 root files plus MACHAB.PAK: the CD loader reads up to 32 root entries (default 16).
SH_CFLAGS += $$(if $$(filter %/texture_cd.c,$$<),-DFZX_TEXTURE_CD_ROOT_ENTRIES=32,)
SH_CFLAGS += $$(if $$(filter engine_closeup.c %/fzx_vehicle.c %/fzx_vehicle_draw.c,$$<),-DFZX_SOURCE_CAMERA=1 -DFZX_SOURCE_MACHINE=1 -DFZX_VEHICLE_NATIVE=1 -DFZX_VEHICLE_FAST=1 -DFZX_VEHICLE_POSE1=1,)
SH_CFLAGS += $$(if $$(filter %/sh2zam_view_indexed.c %/sh2zam_pose.c,$$<),-fno-lto,)
endif

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
	cp $(G)/physics/PHYS.PAK $(G)/$(E1_SPRITES)/RSPRITE.PAK "$(IMAGE_DIRECTORY)/"
endif
ifeq ($(E1_CLOSEUP),1)
	cp $(G)/$(E1_VEHICLE)/MACHAB.PAK "$(IMAGE_DIRECTORY)/"
endif
ifeq ($(E2_PACK),1)
	cp $(G)/rivals/RIVALS.PAK "$(IMAGE_DIRECTORY)/"
endif
ifeq ($(E2_ROSTER),1)
	cp $(G)/roster/ROSTER.PAK "$(IMAGE_DIRECTORY)/"
endif
	$(if $(filter 1,$(E0_CLUSTERS)),cp $(G)/road_cluster/ROADCL.PAK "$(IMAGE_DIRECTORY)/",)
