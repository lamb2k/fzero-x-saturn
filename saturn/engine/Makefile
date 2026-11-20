# Engine image (docs/ENGINE_PLAN.md), milestone E0: track flythrough.
# A separate Saturn program with its own main loop. It links no original game
# code: no reference closure, no stand-ins, no race bridge. The legacy image is
# built by ../Makefile and is not touched by anything here.
#
#   make -C saturn/engine SH2ZAM_ROOT=<checkout of the engine pin> [FZX_VDP1_DBANK=0|1]
#
# The SH2ZAM pin is saturn/engine/sh2zam.pin (branch claude/tubes-train: claude/courses-train aa0a587 plus
# claude/vehicle-dynamics-2 (vehicle-dynamics-2, PR #54: pipes and cylinders); before it, branch
# claude/courses-train: claude/emit-train-2 ba7c237 plus
# claude/line-field-2 (line-field-2, the second-lap re-target); emit-train-2 is claude/snd-engine-train a59a4ef
# plus rotation-sprites-3; snd-engine-train is claude/dsp-engine-train-2 5f3f142 + snd-1;
# before it, branch claude/dsp-engine-train-2: claude/scenes-train-2 e0c5c89
# (claude/e2-field-train: claude/e1-train (segmesh-2, vehicle-dynamics-1, sprite-cache-1, rotation-sprites-2)
# plus line-field-1; plus scene-1) plus dsp-1).
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
# 1: the floor/cloud bank queues are pushed with interrupts masked (the VBlank-In
# present pops them); 0 is the control that loses updates (fault 25/23).
E0_QUEUE_ATOMIC ?= 1
# Control (diagnostic only): no track job, so frames take one field back to back and
# every VBlank-In presents; with E0_QUEUE_ATOMIC=0 the queue race faults within seconds.
E0_QUEUE_STRESS ?= 0
# Control (diagnostic only): spins inside the queue push between reading and storing the count.
E0_QUEUE_WIDEN ?= 0
# E1 drive slice (engine_drive.c): native physics, chase camera, sprite player, HUD.
# Needs generated/physics (tools/engine_drive_assets.py, local only) and
# generated/racer_sprites (tools/racer_sprites.py), and an SH2ZAM pin with
# vehicle-dynamics-1 and rotation-sprites-1 (claude/e1-train).
FZX_E1_DRIVE ?= 0
# 1: the R01 test driver steers from boot (captures); 0: the pad (owner).
E1_DRIVER ?= 0
# Test driver boost period in ticks (0 = R01; 240 = R02).
E1_DRIVER_BOOST ?= 0
# E1 player (generated/CLOSEUP-AB.md). Default with FZX_E1_DRIVE=1: 1, the owner's pick (arm AE,
# with E1_CLOSEUP_EARLY=1). 0 the sprite player (sprite-cache-1 slot pool, the switch); 1 the player as the 3D model (engine_closeup.c: the prepared vehicle
# pack, vehicle-kernels-1 and pose-1); 2 the high-quality near sprite set
# (generated/racer_sprites_hq: tools/racer_sprites.py --views tools/racer_sprites_hq.json).
E1_CLOSEUP ?= $(if $(filter 1,$(FZX_E1_DRIVE)),1,0)
# Arm A only: 1 builds the vehicle's commands while the slave builds the track (staged,
# copied after the shadow); 0 builds them after the track, as the sprite.
E1_CLOSEUP_EARLY ?= $(if $(filter 1,$(E1_CLOSEUP)),1,0)
# Scenes (SCENES.md): the boot-to-reset loop around the E1 drive with the 3D player
# (needs FZX_E1_DRIVE=1, E1_CLOSEUP=1, E1_CLOSEUP_EARLY=1 and the per-family packs
# generated/mach3d/MACH3D<ff>.PAK + mach3d_identity.h). 0 builds the E1 image unchanged.
ENG_SCENES ?= 0
# Roster LOAD phase (scenes, with E2_ROSTER): the field's sets from ROSTERZ.PAK, compressed and read
# selectively (tools/racer_stream.py pack + tools/roster_select_identity.py into generated/roster, local only).
ENG_ROSTER ?= 0
# Guest identities enabled in select, race, results and records (bit n = identity n:
# 33 Robotnik, 34 Metal Sonic). Owner 2026-11-19 (O-078): both locked in the test build.
FZX_GUEST_MASK ?= 0
# The 3D player packs on the disc: the select screen's entrants (C-069: Blue Falcon 0,
# Golden Fox 1, Fire Stingray 3). The CD loader reads at most 32 root entries.
ENG_ENTRANT_PACKS ?= 00 01 03
# The 3D vehicle's generated folders (stills of other machines: machine_rebake_fNN / closeup_fNN).
E1_REBAKE ?= machine_rebake
E1_VEHICLE ?= closeup
# Close-up 3D-N (generated/CLOSEUP-3DN.md): N rivals (1..29) on the SH2ZAM line field, the
# nearest K cars within D drawn in 3D (K, D: run-time words fzx_field_k, fzx_field_d).
# Needs E1_CLOSEUP=1 E1_CLOSEUP_EARLY=1 and an SH2ZAM pin with line-field-1.
# Default with the drive (3D player, early): the field of 29 rivals (E2 field lane, owner 2026-11-19 merge step).
E1_FIELD ?= $(if $(filter 1:1,$(E1_CLOSEUP):$(E1_CLOSEUP_EARLY)),29,0)
# E2 field lane (generated/E2-FIELD.md), with E1_FIELD only; each 0 = the 3D-N field as it was.
# E2_PACK: the E2 rival line (RIVALS.PAK: CPU line laterals, speed caps, widths) and the reference
#   GP field config (generated/rivals: tools/rivals_assets.py, ref/rivals_main.c, tools/rivals_field_header.py).
# E2_FASTPOSE: rival poses from the stations' rows interpolated (no renormalisation, no square roots).
# E2_CULL: cheap reject before any sprite work (behind, past the draw distance, off screen);
#   shadows only for near cars.
# E2_LOD: the field's distance level of detail at E2_LOD_NEAR (units: full rate, 1/2, 1/4; else 1/8).
# E2_EARLY: every car (3D and sprites; without the field, the 3D player) is built after the slave's
#   kick and before the master's field wait (staged), so after the VBlank the master only runs the
#   prelude and the copies.
E2_FIELD_ON := $(if $(filter-out 0,$(E1_FIELD)),1,0)
E2_PACK ?= $(E2_FIELD_ON)
E2_FASTPOSE ?= $(E2_FIELD_ON)
E2_CULL ?= $(E2_FIELD_ON)
E2_LOD ?= $(E2_FIELD_ON)
E2_LOD_NEAR ?= 1000,3000,8000
# E2_EARLY default with the 3D player built early (with or without the field).
E2_EARLY ?= $(if $(filter 1:1,$(E1_CLOSEUP):$(E1_CLOSEUP_EARLY)),1,0)
# Diagnostic: per-frame master phase times (fzx_e2_prof); changes timing slightly, never a floor measurement.
E2_PROF ?= 0
# E2_FASTSPRITE (needs E2_CULL): rival sprite poses reuse the cull's camera-space terms; one division for the up vector.
E2_FASTSPRITE ?= 0
# E2_ROSTER (needs E2_PACK, E2_CULL, E2_FASTSPRITE, E2_EARLY): each rival drawn from its own machine's
#   near/far rotation-sprite sets in the private roster pack (generated/roster/ROSTER.PAK and
#   roster_identity.h from tools/roster_identity.py, which refuses a pack with the wrong SHA-256).
E2_ROSTER ?= 0
# Sprite emit lane (claude/e2-emit, generated/E2-EMIT.md), field images; each 0 = the emit as it was.
# E2_EMIT_EXACT: every exact switch below that the image allows (HINT CMD SHADOW POSE SETUP LINK CULL YAW).
# Default 1 (coordinator 2026-11-19, generated/E2-EMIT.md); E2_EMIT_EXACT=0 is the emit as it was.
# CMD, POSE and YAW call SH2ZAM rotation-sprites-3 (sh2zam_rotation_sprite3.c, pin claude/emit-train-2).
E2_EMIT_EXACT ?= 1
# E2_EMIT_PROF: diagnostic, per-call emit sub-step times (fzx_emit_prof); changes timing slightly.
E2_EMIT_PROF ?= 0
# E2_EMIT_HINT (exact): cache lookups probe each car's last slot before the pool scan.
E2_EMIT_HINT ?= $(E2_EMIT_EXACT)
# E2_EMIT_CMD (exact): the sprite and shadow commands written in one pass, corners in 32-bit products.
E2_EMIT_CMD ?= $(E2_EMIT_EXACT)
# E2_EMIT_SHADOW (exact): shadows posed without the one-cell selection, with the car's own exact screen-up vector.
E2_EMIT_SHADOW ?= $(E2_EMIT_EXACT)
# E2_EMIT_POSE (exact; needs E2_CULL=1): rival poses from the cull's terms (sh2z_rspr_pose_terms; with
#   E2_FASTSPRITE, the same fast pose), divisions overlapped.
E2_EMIT_POSE ?= $(if $(filter 1,$(E2_CULL)),$(E2_EMIT_EXACT),0)
# E2_EMIT_SETUP (exact): the sprite camera's scale numerators kept while the focal length is unchanged.
E2_EMIT_SETUP ?= $(E2_EMIT_EXACT)
# E2_EMIT_LINK (exact; needs E2_EARLY=1): the 3D cars' staged commands copied once, after the track.
E2_EMIT_LINK ?= $(if $(filter 1,$(E2_EARLY)),$(E2_EMIT_EXACT),0)
# E2_EMIT_CULL (exact; needs E2_CULL=1): the cull's far depth kept, its edge tests in 32-bit products.
E2_EMIT_CULL ?= $(if $(filter 1,$(E2_CULL)),$(E2_EMIT_EXACT),0)
# E2_EMIT_YAW (exact; needs E2_EMIT_POSE=1): the yaw bin climbed from each car's last bin (convex bins only).
E2_EMIT_YAW ?= $(if $(filter 1,$(E2_EMIT_POSE)),$(E2_EMIT_EXACT),0)
# ENG_PACE: frame pacing. 0 as built (a frame as soon as it is ready); 1 adaptive (a two-field gap
#   after a VDP1 draw longer than a field); 2 the 30 fps lock (presents never one field apart);
#   3 probe (Yaul's fixed interval ENG_VDP1_INTERVAL); 4 the pipelined 30 fps lock (frame N+1 is
#   built while VDP1 draws N; the swap and the VDP2 present every two fields; needs FZX_VDP1_DBANK=1).
# ENG_PACE_LEAD (ENG_PACE=4): fields after the present two frames back at which a build starts
#   (1: at the previous submit; 2: one field later, one field less latency and build time).
# Default (claude/pace-pipeline, owner 2026-11-19 "Lock 30"): 4 with the banks, else 0.
# ENG_DSP_BANDS (DSP lane, src/fzx_band_corners.h): the floor's and clouds' fog-band corners.
#   0 the original 64-bit loops; 1 (default, coordinator 2026-11-19) the same values on the SH-2 from
#   32-bit deltas, per-band corners only when band_shared fails; 2 their products on the SCU DSP
#   (SH2ZAM dsp-1 scale16, pin claude/dsp-engine-train-2). Every arm bit-identical.
ENG_DSP_BANDS ?= 1
# Diagnostics with ENG_DSP_BANDS=2: CHECK counts DSP corners that differ from the SH-2 form
# (fzx_band_stats[7]); CORRUPT is the control (one kernel word altered after load).
ENG_DSP_BANDS_CHECK ?= 0
ENG_DSP_BANDS_CORRUPT ?= 0
ENG_PACE ?= $(if $(filter 1,$(FZX_VDP1_DBANK)),4,0)
ENG_VDP1_INTERVAL ?= 1
ENG_PACE_LEAD ?= 1
# Control (diagnostic only, ENG_PACE=4): 1 presents VDP2 a field before the framebuffer change
#   on short draws; the submit's mirror check must stop the image (status 45).
ENG_PACE_EARLY_CONTROL ?= 0
# Control (diagnostic only, ENG_PACE=4): 1 keeps the old HUD path (text flushed right after the
#   build, a frame ahead of its picture); 0 flushes each frame's HUD text in the blank of its present.
ENG_PACE_HUD_LEAD ?= 0
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
# Race sound (generated/SOUND-PLAN.md, SH2ZAM snd-1): race events to SCSP clips, engine pitch from
# speed, the two nearest rivals' engines, and the course music ENG_SND_MUSIC from its CD track.
# Needs generated/snd (tools/sfx_pack.py), generated/cdda (tools/disc_cdda.py), both local, an
# SH2ZAM checkout with snd-1 and a multi-track disc. ENG_SND_FADETEST=N: leave (fade the music,
# close the event epoch) at tick N, the LOAD hook's control.
# On by default for the drive images (scenes and the plain drive); ENG_SND=0 is the control.
ENG_SND ?= $(if $(filter 1,$(FZX_E1_DRIVE)),$(if $(filter 1,$(ENG_CDDA)),0,1),0)
# Course (tools/course_catalog.py): its asset set comes from generated/ for the first course and
# from generated/courses/<ENG_COURSE> for the others; the music defaults to the course's own
# (race.c D_i2_80106F14). ENG_COURSE=mute-city-1 builds the images it always did.
ENG_COURSE ?= mute-city-1
ENG_COURSES := mute-city-1 silence-1 sand-ocean-1 devils-forest-1 big-blue-1 port-town-1
COURSE_MUSIC_mute-city-1 := 0
COURSE_MUSIC_silence-1 := 1
COURSE_MUSIC_sand-ocean-1 := 2
COURSE_MUSIC_devils-forest-1 := 5
COURSE_MUSIC_big-blue-1 := 4
COURSE_MUSIC_port-town-1 := 3
ifeq ($(filter $(ENG_COURSE),$(ENG_COURSES)),)
$(error ENG_COURSE must be one of $(ENG_COURSES))
endif
ENG_SND_MUSIC ?= $(COURSE_MUSIC_$(ENG_COURSE))
COURSE_INDEX_mute-city-1 := 0
COURSE_INDEX_silence-1 := 1
COURSE_INDEX_sand-ocean-1 := 2
COURSE_INDEX_devils-forest-1 := 3
COURSE_INDEX_big-blue-1 := 4
COURSE_INDEX_port-town-1 := 5
# ENG_COURSE_SELECT=1 (scenes images): the course catalog (engine_courses.c, fzx_course_catalog) that the
# select scene reads; LOAD refuses a course the image has no packs for, and the music is the selected
# row's. 0 builds the images as before.
ENG_COURSE_SELECT ?= 0
# Tubes (lane claude-tub, saturn/engine/COURSES.md 6): pipes and cylinders in the player's physics
# (SH2ZAM vehicle-dynamics-2, physics pack v3 with joins). On by default for the Jack Cup's two tube
# courses; ENG_TUBES=0 builds them with vehicle-dynamics-1, which refuses their v3 packs (the
# control). The road courses take 0 and build as before.
ENG_TUBES ?= $(if $(filter sand-ocean-1 big-blue-1,$(ENG_COURSE)),1,0)
ifeq ($(filter 0 1,$(ENG_TUBES)),)
$(error ENG_TUBES must be 0 or 1)
endif
ENG_SND_FADETEST ?= 0
ifeq ($(filter 0 1 2,$(ENG_DSP_BANDS)),)
$(error ENG_DSP_BANDS must be 0, 1 or 2)
endif
# Prelude (E0_PRELUDE_*; generated/E0-PRELUDE.md). The exact three are the default (2026-11-19: identical pixels in every field).
# E0_PRELUDE_SHARE (exact): floor and cloud fog bands project each shared boundary line once.
E0_PRELUDE_SHARE ?= 1
# E0_PRELUDE_WINDOW (exact): line windows only on the lines the polygon reaches; 32-bit edge steps.
E0_PRELUDE_WINDOW ?= 1
# E0_PRELUDE_DIV (exact): floor/cloud mapping with shifts, the SH-2 divider and one division per q24.
E0_PRELUDE_DIV ?= 1
ifeq ($(filter 0 1,$(FZX_VDP1_DBANK)),)
$(error FZX_VDP1_DBANK must be 0 or 1)
endif
ifeq ($(filter 0 1 2 3 4,$(ENG_PACE)),)
$(error ENG_PACE must be 0..4)
endif
ifeq ($(ENG_PACE):$(FZX_VDP1_DBANK),4:0)
$(error ENG_PACE=4 needs FZX_VDP1_DBANK=1)
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
ifneq ($(ENG_SCENES),0)
ifneq ($(FZX_E1_DRIVE):$(E1_CLOSEUP):$(E1_CLOSEUP_EARLY),1:1:1)
$(error ENG_SCENES=1 needs FZX_E1_DRIVE=1 E1_CLOSEUP=1 E1_CLOSEUP_EARLY=1)
endif
endif
ifneq ($(ENG_ROSTER),0)
ifneq ($(ENG_SCENES):$(E2_ROSTER),1:1)
$(error ENG_ROSTER=1 needs ENG_SCENES=1 E2_ROSTER=1)
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
SH_OUTPUT_DIR := build-$(if $(filter-out 1,$(ENG_DSP_BANDS)),dspb$(ENG_DSP_BANDS)$(if $(filter 1,$(ENG_DSP_BANDS_CHECK)),c,)$(if $(filter 1,$(ENG_DSP_BANDS_CORRUPT)),x,)-,)$(if $(filter-out 4,$(ENG_PACE)),pace$(ENG_PACE)$(if $(filter 3,$(ENG_PACE)),i$(ENG_VDP1_INTERVAL),)-,$(if $(filter-out 1,$(ENG_PACE_LEAD)),pace4w$(ENG_PACE_LEAD)-,)$(if $(filter 1,$(ENG_PACE_EARLY_CONTROL)),pace4early-,)$(if $(filter 1,$(ENG_PACE_HUD_LEAD)),pace4hudlead-,))$(if $(filter 1,$(FZX_E1_DRIVE)),e1-d$(E1_DRIVER)$(if $(filter-out 0,$(E1_DRIVER_BOOST)),-b$(E1_DRIVER_BOOST),)$(if $(filter-out 0,$(E1_CLOSEUP)),-ab$(E1_CLOSEUP)$(if $(filter 1,$(E1_CLOSEUP_EARLY)),e,)$(if $(filter-out 0,$(E1_FIELD)),-f$(E1_FIELD)$(if $(filter 1,$(E2_PACK)),p,)$(if $(filter 1,$(E2_FASTPOSE)),q,)$(if $(filter 1,$(E2_CULL)),c,)$(if $(filter 1,$(E2_LOD)),l$(subst $(comma),_,$(E2_LOD_NEAR)),)$(if $(filter 1,$(E2_EARLY)),e,)$(if $(filter 1,$(E2_FASTSPRITE)),f,)$(if $(filter 1,$(E2_ROSTER)),r,)$(if $(filter 1,$(E2_PROF)),P,),)$(if $(filter 1,$(ENG_SCENES)),-scn$(if $(filter-out 0,$(FZX_GUEST_MASK)),-gm$(FZX_GUEST_MASK),)$(if $(filter 1,$(ENG_ROSTER)),-ro,),)$(if $(filter 0,$(E1_FIELD)),$(if $(filter 1,$(E2_EARLY)),E,),),)-,e0-)bank$(FZX_VDP1_DBANK)$(if $(filter 1,$(FZX_VDP1_DBANK_STALE)),-stale,)-g$(E0_GUARD_MARGIN)-c$(E0_FAST_CULL)-l$(E0_LOD_SHIFT)-f$(E0_FAN_FIX)-r$(E0_RBG_LEAN)-k$(E0_CLUSTERS)-w$(E0_WORK_HIGH)$(if $(filter-out 0,$(E0_CHUNK_CAP)),-cap$(E0_CHUNK_CAP),)-n$(E0_LEAN_CLIP)-e$(E0_FOG_EARLY)$(if $(filter 1,$(E0_HW_COUNTER)),-hw,)$(if $(filter 1,$(E0_VDP2_MAP)),-map,)$(if $(filter 1,$(E0_SENTINEL_CONTROL)),-sentinel,)$(if $(filter 1,$(E0_POP_LOG)),-pop,)$(if $(filter 1,$(E0_CAP_STABLE)),-stable,)$(if $(filter 1,$(E0_CLUSTER_AUDIT)),-audit,)$(if $(filter 0,$(E0_ISR_MAC_SAVE)),-nomac,)$(if $(filter 1,$(E0_MAC_PROBE)),-macprobe,)$(if $(filter 0,$(E0_QUEUE_ATOMIC)),-qrace,)$(if $(filter 1,$(E0_QUEUE_STRESS)),-qstress,)$(if $(filter-out 0,$(E0_QUEUE_WIDEN)),-qw$(E0_QUEUE_WIDEN),)$(if $(filter 1,$(E0_PHASES)),-phases,)$(if $(filter 0,$(E0_PRELUDE_SHARE)),-ps0,)$(if $(filter 0,$(E0_PRELUDE_WINDOW)),-pw0,)$(if $(filter 0,$(E0_PRELUDE_DIV)),-pd0,)$(if $(filter 1,$(ENG_CDDA)),-cdda$(ENG_CDDA_MUSIC)$(if $(filter-out 0,$(ENG_CDDA_LOOPTEST)),-lt$(ENG_CDDA_LOOPTEST),),)$(if $(filter 1,$(ENG_SND)),-snd$(ENG_SND_MUSIC)$(if $(filter-out 0,$(ENG_SND_FADETEST)),-ft$(ENG_SND_FADETEST),),)
EMIT_SWITCHES := $(E2_EMIT_PROF)$(E2_EMIT_HINT)$(E2_EMIT_CMD)$(E2_EMIT_SHADOW)$(E2_EMIT_POSE)$(E2_EMIT_SETUP)$(E2_EMIT_LINK)$(E2_EMIT_CULL)$(E2_EMIT_YAW)
SH_OUTPUT_DIR := $(SH_OUTPUT_DIR)$(if $(filter-out 0,$(subst 0,,$(EMIT_SWITCHES))),-em$(EMIT_SWITCHES),)
SH_OUTPUT_DIR := $(SH_OUTPUT_DIR)$(if $(filter-out mute-city-1,$(ENG_COURSE)),-$(ENG_COURSE),)$(if $(E1_DRIVER_KH),-kh$(E1_DRIVER_KH),)$(if $(filter 1,$(ENG_COURSE_SELECT)),-sel,)$(if $(filter 1,$(ENG_TUBES)),-tubes,)
SH_BUILD_DIR := $(SH_OUTPUT_DIR)/yaul
IMAGE_DIRECTORY := $(SH_OUTPUT_DIR)/cd
IMAGE_1ST_READ_BIN := A.BIN

include $(YAUL_INSTALL_ROOT)/share/build.pre.mk

SH2ZAM_ROOT ?=
ifeq ($(wildcard $(SH2ZAM_ROOT)/saturn/include/sh2zam/segmesh_experimental.h),)
$(error Set SH2ZAM_ROOT to a checkout of the engine pin (saturn/engine/sh2zam.pin))
endif

G := ../../generated
GCREL := $(if $(filter mute-city-1,$(ENG_COURSE)),,courses/$(ENG_COURSE)/)
GC := $(G)$(if $(GCREL),/$(GCREL:%/=%),)
E0_RECEIPTS := engine_path/engine_path.receipt road_pack/road_pack.receipt road_bake/road_bake.receipt \
               skyline/skyline.receipt sky/sky.receipt planes_cloud/planes.receipt floor_rbg/floor_rbg.receipt \
               $(if $(filter 1,$(E0_CLUSTERS)),road_cluster/road_cluster.receipt,)
E0_RECEIPT_CHECK := $(foreach R,$(E0_RECEIPTS),$(shell sh ../../tools/check_receipt.sh $(GC)/$(R) >/dev/null 2>&1 || echo $(R)))
ifeq ($(FZX_E1_DRIVE),1)
# The scenes image takes the 3D player from generated/mach3d (tools/mach3d_packs.py) and its own
# offline bakes (road_prep, engine_path_ranges).
E1_CLOSEUP_RECEIPTS := $(if $(filter 1,$(ENG_SCENES)),mach3d_scn/mach3d.receipt $(GCREL)road_prep/road_prep.receipt $(GCREL)engine_path_ranges/engine_path_ranges.receipt,$(E1_REBAKE)/machine_rebake.receipt $(E1_VEHICLE)/closeup.receipt)
E1_RECEIPT_CHECK := $(foreach R,$(GCREL)physics/physics.receipt $(E1_SPRITES)/racer_sprites.receipt $(if $(filter 1,$(E1_CLOSEUP)),$(E1_CLOSEUP_RECEIPTS),),$(shell sh ../../tools/check_receipt.sh $(G)/$(R) >/dev/null 2>&1 || echo $(R)))
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
    $(if $(filter 1,$(FZX_E1_DRIVE)),sh2zam_vehicle_dynamics.c sh2zam_unit30.c sh2zam_rotation_sprite.c sh2zam_sprite_cache.c sh2zam_vlc2.c,)     $(if $(filter 1,$(FZX_E1_DRIVE)),$(if $(filter 1,$(ENG_TUBES)),sh2zam_vehicle_dynamics2.c,),) \
    $(if $(filter-out 0,$(E1_FIELD)),$(if $(filter 1,$(E2_EMIT_CMD) $(E2_EMIT_POSE) $(E2_EMIT_YAW)),sh2zam_rotation_sprite3.c,),) \
    $(if $(filter 1,$(E1_CLOSEUP)),sh2zam_vehicle.c sh2zam_view_indexed.c sh2zam_pose.c,) \
    $(if $(filter-out 0,$(E1_FIELD)),sh2zam_line_follower.c sh2zam_line_field.c,) \
    $(if $(filter 1,$(ENG_SCENES)),sh2zam_scene.c,)

SH_SRCS := engine_main.c engine_scene.c engine_path.c \
    ../sky_native.c ../skyline_native.c ../planes_native.c ../floor_rbg_native.c ../clouds_vdp2_native.c \
    ../road_native.c ../road_cd.c ../road_stream_native.c ../road_stream_commands.c ../slave_road.c ../texture_cd.c \
    ../../src/fzx_view.c ../../src/fzx_view16.c ../../src/fzx_view16_center.c ../../src/fzx_sky.c ../../src/fzx_skyline.c ../../src/fzx_planes.c \
    ../../src/fzx_floor_rbg.c ../../src/fzx_clouds_rbg.c ../../src/fzx_rbg_fast.c \
    ../../src/fzx_road_pack.c ../../src/fzx_road_stream.c ../../src/fzx_road_stream_draw.c \
    $(GC)/course_record.c \
    $(if $(filter 1,$(FZX_E1_DRIVE)),engine_drive.c ../physics_native.c,) \
    $(if $(filter 1,$(E1_CLOSEUP)),engine_closeup.c ../../src/fzx_vehicle.c ../../src/fzx_vehicle_draw.c,) \
    $(if $(filter-out 0,$(E1_FIELD)),engine_field.c,) \
    $(if $(filter 1,$(ENG_SCENES)),engine_scenes.c engine_records.c engine_load_fast.c,) $(if $(filter 1,$(ENG_ROSTER)),engine_roster.c,) \
    $(addprefix $(SH2ZAM_ROOT)/saturn/src/,$(sort $(SH2ZAM_UNITS)))
ifneq ($(ENG_DSP_BANDS),0)
SH_SRCS += ../../src/fzx_band_corners.c
ifeq ($(ENG_DSP_BANDS),2)
ifeq ($(wildcard $(SH2ZAM_ROOT)/saturn/include/sh2zam/dsp_experimental.h),)
$(error ENG_DSP_BANDS=2 needs an SH2ZAM pin with dsp-1 (claude/dsp-engine-train-2))
endif
SH_SRCS += $(SH2ZAM_ROOT)/saturn/src/sh2zam_dsp.c
endif
SH_CFLAGS += -DENG_DSP_BANDS=$(ENG_DSP_BANDS) -DENG_DSP_BANDS_CHECK=$(ENG_DSP_BANDS_CHECK) -DENG_DSP_BANDS_CORRUPT=$(ENG_DSP_BANDS_CORRUPT)
endif

SH_CFLAGS += -DENG_PACE=$(ENG_PACE) $(if $(filter 3,$(ENG_PACE)),-DENG_VDP1_INTERVAL=$(ENG_VDP1_INTERVAL),) $(if $(filter 4,$(ENG_PACE)),-DENG_PACE_LEAD=$(ENG_PACE_LEAD) -DENG_PACE_EARLY_CONTROL=$(ENG_PACE_EARLY_CONTROL) -DENG_PACE_HUD_LEAD=$(ENG_PACE_HUD_LEAD),) -DFZX_ENGINE=1 -DFZX_FRAME_PROFILE=1 -DFZX_VDP1_DBANK=$(FZX_VDP1_DBANK) \
    -DFZX_VDP1_DBANK_STALE=$(FZX_VDP1_DBANK_STALE) -DFZX_ROAD_STREAM_GUARD_MARGIN=$(E0_GUARD_MARGIN) \
    -DFZX_ROAD_STREAM_PHASES=$(E0_PHASES) -DFZX_SCENE_PHASES=$(E0_PHASES) -DFZX_ROAD_STREAM_FAST_CULL=$(E0_FAST_CULL) -DFZX_ROAD_STREAM_LOD_SHIFT=$(E0_LOD_SHIFT) -DFZX_ROAD_STREAM_FAN_FIX=$(E0_FAN_FIX) -DFZX_RBG_LEAN=$(E0_RBG_LEAN) -DFZX_RBG_SHARE=$(E0_PRELUDE_SHARE) -DFZX_RBG_WINDOW=$(E0_PRELUDE_WINDOW) -DFZX_RBG_DIV=$(E0_PRELUDE_DIV) \
    -DFZX_ROAD_STREAM_CLUSTERS=$(E0_CLUSTERS) -DFZX_ROAD_STREAM_CLUSTER_AUDIT=$(E0_CLUSTER_AUDIT) -I$(GC)/road_cluster -DFZX_ROAD_STREAM_WORK_HIGH=$(E0_WORK_HIGH) -DE0_CHUNK_CAP=$(E0_CHUNK_CAP) -DFZX_ROAD_STREAM_LEAN_CLIP=$(E0_LEAN_CLIP) -DFZX_ROAD_STREAM_FOG_EARLY=$(E0_FOG_EARLY) -DE0_HW_COUNTER=$(E0_HW_COUNTER) -DE0_VDP2_MAP=$(E0_VDP2_MAP) -DE0_SENTINEL_CONTROL=$(E0_SENTINEL_CONTROL) -DE0_POP_LOG=$(E0_POP_LOG) -DE0_CAP_STABLE=$(E0_CAP_STABLE) \
    -DFZX_SOURCE_SKY=1 -I$(GC)/sky -DFZX_SKY_SIMPLIFY=1 -DFZX_SOURCE_SKYLINE=1 -I$(GC)/skyline -DFZX_SKYLINE_CD=1 \
    -DFZX_SOURCE_PLANES=1 -I$(GC)/planes_cloud -DFZX_PLANE_CLOUD_ONLY=1 -DFZX_PLANES_DEGENERATE_UV=1 \
    -DFZX_SOURCE_FLOOR_RBG=1 -I$(GC)/floor_rbg -DFZX_CLOUDS_VDP2=1 \
    -DFZX_SOURCE_ROAD=1 -I$(GC)/road_pack -DFZX_SOURCE_ROAD_TILES=1 -DFZX_SOURCE_ROAD_STREAM=1 -I$(GC)/road_bake \
    -DFZX_ROAD_STREAM_NATIVE_LAYOUT=1 -DFZX_RS_PACK_PATH=0 -DFZX_ROAD_STREAM_EMIT=1 -DFZX_RS_PACKET_PATH=0 \
    -DFZX_ROAD_STREAM_BUDGET=1 -DFZX_RBG_FAST=1 -DFZX_SLAVE_ROAD=1 -DFZX_ROAD_LOD=1 \
    -I$(GC)/engine_path -DFZX_SH2_DIVU=1 -O2 -std=c11 -Wall -Wextra \
    -I. -I.. -I../../include -I../../ref -I$(SH2ZAM_ROOT)/saturn/include -DSH2Z_REQUIRE_SH2=1 \
    -ffunction-sections -fdata-sections \
    -DE0_ISR_MAC_SAVE=$(E0_ISR_MAC_SAVE) -DE0_MAC_PROBE=$(E0_MAC_PROBE) -DFZX_DBANK_QUEUE_ATOMIC=$(E0_QUEUE_ATOMIC) -DE0_QUEUE_STRESS=$(E0_QUEUE_STRESS) -DFZX_DBANK_QUEUE_WIDEN=$(E0_QUEUE_WIDEN) -DFZX_E1_DRIVE=$(FZX_E1_DRIVE) -DE1_DRIVER=$(E1_DRIVER) -DE1_DRIVER_BOOST=$(E1_DRIVER_BOOST) $(if $(E1_DRIVER_KH),-DE1_DRIVER_KH=$(E1_DRIVER_KH)f,) \
    $(if $(filter 1,$(FZX_E1_DRIVE)),-I$(GC)/physics -I$(G)/$(E1_SPRITES),)
ifeq ($(ENG_CDDA),1)
ifeq ($(wildcard $(G)/cdda/cdda_tracks.h),)
$(error ENG_CDDA=1 needs $(G)/cdda/cdda_tracks.h: run tools/disc_cdda.py --header)
endif
SH_SRCS += engine_cdda.c
SH_CFLAGS += -DENG_CDDA=1 -DENG_CDDA_MUSIC=$(ENG_CDDA_MUSIC) -DENG_CDDA_LOOPTEST=$(ENG_CDDA_LOOPTEST) -I$(G)/cdda
endif
ifeq ($(ENG_SND),1)
ifeq ($(ENG_CDDA),1)
$(error ENG_SND plays the music itself: build it without ENG_CDDA)
endif
ifneq ($(FZX_E1_DRIVE),1)
$(error ENG_SND needs FZX_E1_DRIVE=1)
endif
SND_RECEIPT_CHECK := $(foreach R,snd/snd.receipt cdda/cdda.receipt,$(shell sh ../../tools/check_receipt.sh $(G)/$(R) >/dev/null 2>&1 || echo $(R)))
ifneq ($(strip $(SND_RECEIPT_CHECK)),)
$(error Missing or stale sound assets ($(SND_RECEIPT_CHECK)): run tools/sfx_pack.py --output generated/snd and tools/disc_cdda.py --music <bgm_cdda.py output> --ids all --header-only --header generated/cdda/cdda_tracks.h (local, from the ROM); or build with ENG_SND=0)
endif
ifeq ($(wildcard $(SH2ZAM_ROOT)/saturn/include/sh2zam/snd_experimental.h),)
$(error ENG_SND=1 needs the SH2ZAM pin (saturn/engine/sh2zam.pin, claude/snd-engine-train with snd-1))
endif
SH_SRCS += engine_snd.c $(SH2ZAM_ROOT)/saturn/src/sh2zam_snd.c
SH_CFLAGS += -DENG_SND=1 -DENG_SND_MUSIC=$(ENG_SND_MUSIC) -DENG_SND_FADETEST=$(ENG_SND_FADETEST) -I$(G)/snd -I$(G)/cdda
endif
# As ../Makefile: these SH2ZAM units carry same-named assembler macros.
SH_CFLAGS += $$(if $$(filter %/sh2zam_view.c %/sh2zam_view_center.c,$$<),-fno-lto,)
ifneq ($(E1_CLOSEUP),0)
SH_CFLAGS += -DE1_CLOSEUP=$(E1_CLOSEUP) -DE1_CLOSEUP_EARLY=$(E1_CLOSEUP_EARLY)
ifeq ($(E1_FIELD),0)
SH_CFLAGS += -DE2_EARLY=$(E2_EARLY)
endif
ifneq ($(E1_FIELD),0)
# FZX_CLOSEUP_CARS: 3D slots (engine_field.h FZX_FIELD_K_MAX).
SH_CFLAGS += -DE1_FIELD=$(E1_FIELD) -DFZX_CLOSEUP_CARS=6
SH_CFLAGS += -DE2_PACK=$(E2_PACK) -DE2_FASTPOSE=$(E2_FASTPOSE) -DE2_CULL=$(E2_CULL) -DE2_LOD=$(E2_LOD) -DE2_LOD_NEAR=$(E2_LOD_NEAR) -DE2_EARLY=$(E2_EARLY) -DE2_PROF=$(E2_PROF) -DE2_FASTSPRITE=$(E2_FASTSPRITE) -DE2_ROSTER=$(E2_ROSTER)
SH_CFLAGS += -DE2_EMIT_PROF=$(E2_EMIT_PROF) -DE2_EMIT_HINT=$(E2_EMIT_HINT) -DE2_EMIT_CMD=$(E2_EMIT_CMD) -DE2_EMIT_SHADOW=$(E2_EMIT_SHADOW) -DE2_EMIT_POSE=$(E2_EMIT_POSE) -DE2_EMIT_SETUP=$(E2_EMIT_SETUP) -DE2_EMIT_LINK=$(E2_EMIT_LINK) -DE2_EMIT_CULL=$(E2_EMIT_CULL) -DE2_EMIT_YAW=$(E2_EMIT_YAW)
ifeq ($(E2_EMIT_YAW):$(E2_EMIT_POSE),1:0)
$(error E2_EMIT_YAW needs E2_EMIT_POSE=1)
endif
ifeq ($(E2_EMIT_CULL):$(E2_CULL),1:0)
$(error E2_EMIT_CULL needs E2_CULL=1)
endif
ifeq ($(E2_EMIT_LINK):$(E2_EARLY),1:0)
$(error E2_EMIT_LINK needs E2_EARLY=1)
endif
ifeq ($(E2_EMIT_POSE):$(E2_CULL),1:0)
$(error E2_EMIT_POSE needs E2_CULL=1)
endif
ifeq ($(E2_PACK),1)
SH_CFLAGS += -I$(GC)/rivals
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
ifeq ($(ENG_SCENES),1)
SH_CFLAGS += -DENG_SCENES=1 -DFZX_GUEST_MASK=$(FZX_GUEST_MASK)ull -I$(G)/mach3d_scn -DFZX_CD_STREAM=1 -DFZX_LOAD_VERIFIED=1 -DENG_ROSTER=$(ENG_ROSTER) -I$(G)/roster -DFZX_ROAD_PREP_BAKED=1 -I$(GC)/road_prep -I$(GC)/engine_path_ranges
else
SH_CFLAGS += -I$(G)/$(E1_REBAKE) -I$(G)/$(E1_VEHICLE)
endif
# 16 root files plus MACHAB.PAK: the CD loader reads up to 32 root entries (default 16).
SH_CFLAGS += $$(if $$(filter %/texture_cd.c,$$<),-DFZX_TEXTURE_CD_ROOT_ENTRIES=32,)
SH_CFLAGS += $$(if $$(filter engine_closeup.c %/fzx_vehicle.c %/fzx_vehicle_draw.c,$$<),-DFZX_SOURCE_CAMERA=1 -DFZX_SOURCE_MACHINE=1 -DFZX_VEHICLE_NATIVE=1 -DFZX_VEHICLE_FAST=1 -DFZX_VEHICLE_POSE1=1,)
SH_CFLAGS += $$(if $$(filter %/sh2zam_view_indexed.c %/sh2zam_pose.c,$$<),-fno-lto,)
endif

# Low work RAM layout of this image: the path pack at 0x00200000 (0x8C000
# reserved), then the CD scratch (4,096 B), then a 12,288-word block the track
# workspace's loader expects ahead of it. The reused loaders find these through
# the two symbols below.
# A course after the first carries its own skyline layout (skyline_ring.h, tools/skyline_assets.py) and
# may use a larger track texture budget (tools/road_bake.py --texture-limit; placement checked at load); its
# sky gradient need not be monotonic in brightness (a sunset), so sky bands pick their colour by distance.
SH_CFLAGS += $(if $(filter 1,$(FZX_E1_DRIVE)),$(if $(filter 1,$(ENG_TUBES)),-DFZX_PHYS_TUBES=1,),)
SH_CFLAGS += $(if $(GCREL),-DENG_COURSE_SKYLINE=1 -DFZX_ROAD_STREAM_TEXTURE_LIMIT=240000u -DFZX_SKY_LINEAR_PICK=1 -DENG_COURSE_INDEX=$(COURSE_INDEX_$(ENG_COURSE)),)
ifeq ($(ENG_COURSE_SELECT),1)
ifneq ($(ENG_SCENES),1)
$(error ENG_COURSE_SELECT=1 needs ENG_SCENES=1)
endif
SH_SRCS += engine_courses.c
SH_CFLAGS += -DENG_COURSE_SELECT=1
endif

SH_LDFLAGS += -Wl,--defsym=_sat_stub_data_end=0x0028C000 -Wl,--defsym=_fzx_input_log=0x0028D000
# Unused functions and data are dropped at link time (SH2ZAM sections-1).
SH_LDFLAGS += -Wl,--gc-sections

IP_VERSION := V0.001
IP_RELEASE_DATE := 20261119
IP_AREAS := JTUBKAEL
IP_PERIPHERALS := J
IP_TITLE := $(if $(filter 1,$(ENG_SCENES)),Engine scenes loop,$(if $(filter 1,$(FZX_E1_DRIVE)),Engine E1 drive,Engine E0 track flythrough))
IP_MASTER_STACK_ADDR := 0x06100000
# Slave stack: the 8 KB IP.BIN area 0x06002000..0x06004000, guarded (../slave_road.c).
IP_SLAVE_STACK_ADDR := 0x06004000
IP_1ST_READ_ADDR := 0x06004000
IP_1ST_READ_SIZE := 0

include $(YAUL_INSTALL_ROOT)/share/build.post.iso-cue.mk

$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso: $(GC)/engine_path/ENGPATH.PAK
pre-build-iso: stage-e0-packs
.PHONY: stage-e0-packs
stage-e0-packs:
	mkdir -p "$(IMAGE_DIRECTORY)"
	cp $(GC)/engine_path/ENGPATH.PAK $(GC)/road_pack/ROAD.PAK $(GC)/road_bake/ROADLOD.PAK $(GC)/road_bake/ROADLTX.PAK \
	   $(GC)/skyline/SKYLINE.PAK $(GC)/planes_cloud/PLANES.PAK \
	   $(GC)/floor_rbg/FLOORCHR.BIN $(GC)/floor_rbg/FLOORMAP.BIN $(GC)/floor_rbg/FLOORKOF.BIN "$(IMAGE_DIRECTORY)/"
	$(if $(filter 1,$(E0_CLUSTERS)),cp $(GC)/road_cluster/ROADCL.PAK "$(IMAGE_DIRECTORY)/",)
	$(if $(filter 1,$(ENG_SND)),cp $(G)/snd/SNDRACE.PAK "$(IMAGE_DIRECTORY)/",)
ifeq ($(FZX_E1_DRIVE),1)
	cp $(GC)/physics/PHYS.PAK $(G)/$(E1_SPRITES)/RSPRITE.PAK "$(IMAGE_DIRECTORY)/"
endif
ifeq ($(ENG_SCENES),1)
	rm -f "$(IMAGE_DIRECTORY)"/MACH3D*.PAK
	cp $(foreach F,$(ENG_ENTRANT_PACKS),$(G)/mach3d_scn/MACH3D$(F).PAK) "$(IMAGE_DIRECTORY)/"
	$(if $(filter 1,$(ENG_ROSTER)),cp $(G)/roster/ROSTERZ.PAK "$(IMAGE_DIRECTORY)/",rm -f "$(IMAGE_DIRECTORY)/ROSTERZ.PAK")
	cp $(GC)/road_prep/ROADPREP.PAK "$(IMAGE_DIRECTORY)/"
else ifeq ($(E1_CLOSEUP),1)
	cp $(G)/$(E1_VEHICLE)/MACHAB.PAK "$(IMAGE_DIRECTORY)/"
endif
ifeq ($(E2_PACK),1)
	cp $(GC)/rivals/RIVALS.PAK "$(IMAGE_DIRECTORY)/"
endif
ifeq ($(E2_ROSTER),1)
	cp $(G)/roster/ROSTER.PAK "$(IMAGE_DIRECTORY)/"
endif
