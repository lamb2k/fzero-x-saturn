# Engine image (docs/ENGINE_PLAN.md), milestone E0: track flythrough.
# A separate Saturn program with its own main loop. It links no original game
# code: no reference closure, no stand-ins, no race bridge. The legacy image is
# built by ../Makefile and is not touched by anything here.
#
#   make -C saturn/engine SH2ZAM_ROOT=<checkout of the engine pin> [FZX_VDP1_DBANK=0|1]
#
# The SH2ZAM pin is saturn/engine/sh2zam.pin (branch claude/segmesh-2: claude/engine-train plus segmesh-2).
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
# Music as CD audio (generated/SOUND-PLAN.md): play music id ENG_CDDA_MUSIC from its disc track while
# the image runs. Needs generated/cdda/cdda_tracks.h (tools/disc_cdda.py, local) and a multi-track disc.
ENG_CDDA ?= 0
ENG_CDDA_MUSIC ?= 0
# Diagnostic: start this many sectors before the track end, so the loop-back happens within the run.
ENG_CDDA_LOOPTEST ?= 0
# 1: the floor/cloud bank queues are pushed with interrupts masked (the VBlank-In
# present pops them); 0 is the control that loses updates (fault 25 FLOOR / 23 CLOUDS).
E0_QUEUE_ATOMIC ?= 1
# 1: VBlank callbacks keep MACH/MACL (the BIOS dispatcher does not); 0 is the control.
E0_ISR_MAC_SAVE ?= 1
# Diagnostic: per-frame check for multiply registers changed under the VBlank interrupt.
E0_MAC_PROBE ?= 0
# Control (diagnostic only): no track job, so frames take one field back to back and
# every VBlank-In presents; with E0_QUEUE_ATOMIC=0 the queue race faults within seconds.
E0_QUEUE_STRESS ?= 0
# Control (diagnostic only): spins inside the queue push between reading and storing the count.
E0_QUEUE_WIDEN ?= 0
ifeq ($(filter 0 1,$(FZX_VDP1_DBANK)),)
$(error FZX_VDP1_DBANK must be 0 or 1)
endif
SH_OUTPUT_DIR := build-e0-bank$(FZX_VDP1_DBANK)$(if $(filter 1,$(FZX_VDP1_DBANK_STALE)),-stale,)-g$(E0_GUARD_MARGIN)-c$(E0_FAST_CULL)-l$(E0_LOD_SHIFT)-f$(E0_FAN_FIX)-r$(E0_RBG_LEAN)-k$(E0_CLUSTERS)-w$(E0_WORK_HIGH)$(if $(filter-out 0,$(E0_CHUNK_CAP)),-cap$(E0_CHUNK_CAP),)-n$(E0_LEAN_CLIP)-e$(E0_FOG_EARLY)$(if $(filter 1,$(E0_HW_COUNTER)),-hw,)$(if $(filter 1,$(E0_VDP2_MAP)),-map,)$(if $(filter 1,$(E0_SENTINEL_CONTROL)),-sentinel,)$(if $(filter 1,$(E0_POP_LOG)),-pop,)$(if $(filter 1,$(E0_CAP_STABLE)),-stable,)$(if $(filter 1,$(E0_CLUSTER_AUDIT)),-audit,)$(if $(filter 0,$(E0_QUEUE_ATOMIC)),-qrace,)$(if $(filter 0,$(E0_ISR_MAC_SAVE)),-nomac,)$(if $(filter 1,$(E0_MAC_PROBE)),-macprobe,)$(if $(filter 1,$(E0_QUEUE_STRESS)),-qstress,)$(if $(filter-out 0,$(E0_QUEUE_WIDEN)),-qw$(E0_QUEUE_WIDEN),)$(if $(filter 1,$(E0_PHASES)),-phases,)$(if $(filter 1,$(ENG_CDDA)),-cdda$(ENG_CDDA_MUSIC)$(if $(filter-out 0,$(ENG_CDDA_LOOPTEST)),-lt$(ENG_CDDA_LOOPTEST),),)
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
ifneq ($(strip $(E0_RECEIPT_CHECK)),)
$(error Missing or stale assets; run their generators: $(E0_RECEIPT_CHECK))
endif

SH2ZAM_UNITS := sh2zam.c sh2zam_angles.c sh2zam_batch_sh2.c sh2zam_box2.c sh2zam_cell16.c sh2zam_divu.c \
    sh2zam_geometry.c sh2zam_m68k.c sh2zam_prepared.c sh2zam_reuse.c sh2zam_rotation_batch.c sh2zam_shade.c \
    sh2zam_spatial.c sh2zam_transform.c sh2zam_trig.c sh2zam_view.c sh2zam_view_center.c \
    sh2zam_view_guard_src.c sh2zam_view_guard_src_center.c sh2zam_view_round.c \
    sh2zam_view_merge_tier.c sh2zam_shade_fused.c sh2zam_view_rails.c sh2zam_segmesh.c sh2zam_segmesh_cull.c sh2zam_vdp1_banks.c

SH_SRCS := engine_main.c engine_scene.c engine_path.c \
    ../sky_native.c ../skyline_native.c ../planes_native.c ../floor_rbg_native.c ../clouds_vdp2_native.c \
    ../road_native.c ../road_cd.c ../road_stream_native.c ../road_stream_commands.c ../slave_road.c ../texture_cd.c \
    ../../src/fzx_view.c ../../src/fzx_view16.c ../../src/fzx_view16_center.c ../../src/fzx_sky.c ../../src/fzx_skyline.c ../../src/fzx_planes.c \
    ../../src/fzx_floor_rbg.c ../../src/fzx_clouds_rbg.c ../../src/fzx_rbg_fast.c \
    ../../src/fzx_road_pack.c ../../src/fzx_road_stream.c ../../src/fzx_road_stream_draw.c \
    $(G)/course_record.c \
    $(addprefix $(SH2ZAM_ROOT)/saturn/src/,$(sort $(SH2ZAM_UNITS)))

SH_CFLAGS += -DFZX_ENGINE=1 -DFZX_FRAME_PROFILE=1 -DFZX_VDP1_DBANK=$(FZX_VDP1_DBANK) \
    -DFZX_VDP1_DBANK_STALE=$(FZX_VDP1_DBANK_STALE) -DFZX_ROAD_STREAM_GUARD_MARGIN=$(E0_GUARD_MARGIN) \
    -DFZX_ROAD_STREAM_PHASES=$(E0_PHASES) -DFZX_SCENE_PHASES=$(E0_PHASES) -DFZX_ROAD_STREAM_FAST_CULL=$(E0_FAST_CULL) -DFZX_ROAD_STREAM_LOD_SHIFT=$(E0_LOD_SHIFT) -DFZX_ROAD_STREAM_FAN_FIX=$(E0_FAN_FIX) -DFZX_RBG_LEAN=$(E0_RBG_LEAN) -DFZX_DBANK_QUEUE_ATOMIC=$(E0_QUEUE_ATOMIC) -DE0_ISR_MAC_SAVE=$(E0_ISR_MAC_SAVE) -DE0_MAC_PROBE=$(E0_MAC_PROBE) -DE0_QUEUE_STRESS=$(E0_QUEUE_STRESS) -DFZX_DBANK_QUEUE_WIDEN=$(E0_QUEUE_WIDEN) \
    -DFZX_ROAD_STREAM_CLUSTERS=$(E0_CLUSTERS) -DFZX_ROAD_STREAM_CLUSTER_AUDIT=$(E0_CLUSTER_AUDIT) -I$(G)/road_cluster -DFZX_ROAD_STREAM_WORK_HIGH=$(E0_WORK_HIGH) -DE0_CHUNK_CAP=$(E0_CHUNK_CAP) -DFZX_ROAD_STREAM_LEAN_CLIP=$(E0_LEAN_CLIP) -DFZX_ROAD_STREAM_FOG_EARLY=$(E0_FOG_EARLY) -DE0_HW_COUNTER=$(E0_HW_COUNTER) -DE0_VDP2_MAP=$(E0_VDP2_MAP) -DE0_SENTINEL_CONTROL=$(E0_SENTINEL_CONTROL) -DE0_POP_LOG=$(E0_POP_LOG) -DE0_CAP_STABLE=$(E0_CAP_STABLE) \
    -DFZX_SOURCE_SKY=1 -I$(G)/sky -DFZX_SKY_SIMPLIFY=1 -DFZX_SOURCE_SKYLINE=1 -I$(G)/skyline -DFZX_SKYLINE_CD=1 \
    -DFZX_SOURCE_PLANES=1 -I$(G)/planes_cloud -DFZX_PLANE_CLOUD_ONLY=1 -DFZX_PLANES_DEGENERATE_UV=1 \
    -DFZX_SOURCE_FLOOR_RBG=1 -I$(G)/floor_rbg -DFZX_CLOUDS_VDP2=1 \
    -DFZX_SOURCE_ROAD=1 -I$(G)/road_pack -DFZX_SOURCE_ROAD_TILES=1 -DFZX_SOURCE_ROAD_STREAM=1 -I$(G)/road_bake \
    -DFZX_ROAD_STREAM_NATIVE_LAYOUT=1 -DFZX_RS_PACK_PATH=0 -DFZX_ROAD_STREAM_EMIT=1 -DFZX_RS_PACKET_PATH=0 \
    -DFZX_ROAD_STREAM_BUDGET=1 -DFZX_RBG_FAST=1 -DFZX_SLAVE_ROAD=1 -DFZX_ROAD_LOD=1 \
    -I$(G)/engine_path -DFZX_SH2_DIVU=1 -O2 -std=c11 -Wall -Wextra \
    -I. -I.. -I../../include -I../../ref -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1 \
    -ffunction-sections -fdata-sections
ifeq ($(ENG_CDDA),1)
ifeq ($(wildcard $(G)/cdda/cdda_tracks.h),)
$(error ENG_CDDA=1 needs $(G)/cdda/cdda_tracks.h: run tools/disc_cdda.py --header)
endif
SH_SRCS += engine_cdda.c
SH_CFLAGS += -DENG_CDDA=1 -DENG_CDDA_MUSIC=$(ENG_CDDA_MUSIC) -DENG_CDDA_LOOPTEST=$(ENG_CDDA_LOOPTEST) -I$(G)/cdda
endif
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
IP_TITLE := Engine E0 track flythrough
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
	$(if $(filter 1,$(E0_CLUSTERS)),cp $(G)/road_cluster/ROADCL.PAK "$(IMAGE_DIRECTORY)/",)
