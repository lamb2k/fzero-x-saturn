/* Sim-pipeline checkpoint 3 (FZX_SIMPIPE_DBUF, default 0): serial double
 * buffering of the publication outputs (generated/SIM-PIPELINE-PLAN.md).
 * Two slots and a guarded job block live in low work RAM, carved before the
 * frame profile. Publication N is copied into slot N&1; the scene restores its
 * draw state from that slot before it draws. One CPU: the result is exact. */
#ifndef FZX_SIMPIPE_H
#define FZX_SIMPIPE_H
#include <stdbool.h>
#include <stdint.h>
#include "fzx.h"
#include "ref.h"
#include "fzx_countdown.h"
#include "road_plan.h"
#include "skyline_state.h"
#include "backdrop_state.h"
#include "countdown_state.h"
#include "fzx_hud.h"

#define FZX_SIMPIPE_SLOT_MAGIC 0x53504c54u  /* 'SPLT' */
#define FZX_SIMPIPE_JOB_MAGIC 0x53504a42u   /* 'SPJB' */
#define FZX_SIMPIPE_GUARD 0x5a17c3e9u
#define FZX_SIMPIPE_MAX_TICKS 6u            /* five PAL fields give six 60 Hz ticks (C-033) */

/* Every draw-facing publication output (see SIM-PIPELINE-PLAN.md, checkpoint 3). */
typedef struct {
    uint32_t magic, generation, sequence, ticks;
    uint32_t camera_status, camera_set, sky_status, skyline_status, road_plan_status, road_plan_sequence;
    int32_t machine_published;
    fzx_countdown_phase_t countdown_phase;
    uint32_t countdown_published;
    RefPose pose;               /* orig_pose: HUD and energy */
    RefCamera camera;           /* fzx_source_camera */
    fzx_view_t view;            /* fzx_native_view */
    RefMachine machine;         /* fzx_source_machine */
    RefCountdown countdown;     /* fzx_source_countdown */
    RefSkyline skyline;         /* fzx_source_skyline */
    RefBackdrop backdrop;       /* fzx_source_backdrop */
    RefRoadPlan road_plan;      /* fzx_source_road_plan */
    fzx_hud_clock_t hud_clock;  /* stepped per tick in logged_tick */
    fzx_core_publication_t core;/* fzx_set_pose / fzx_set_camera state in the core */
} fzx_simpipe_slot_t;

/* The job a slave would run for frame N+1 (sized here; used from checkpoint 4). */
typedef struct {
    uint32_t magic, generation, sequence, ticks;
    uint32_t words[FZX_SIMPIPE_MAX_TICKS];      /* buttons<<16 | stick x<<8 | stick y */
    uint32_t connected[FZX_SIMPIPE_MAX_TICKS];  /* digital.connected per tick */
    uint32_t status, done, reserved[2];
} fzx_simpipe_job_t;

/* Layout report: magic 'SPDB', version, base, slot0, slot1, job, end, slot bytes,
 * job bytes, payload bytes (2 slots), guard trips, sequence mismatches,
 * copy-outs, copy-ins, stale copy-ins (control only), status. */
extern volatile uint32_t fzx_simpipe_layout[16];
#endif
