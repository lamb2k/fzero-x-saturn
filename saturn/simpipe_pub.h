/* Sim-pipeline checkpoint 4 (FZX_SIM_PIPELINE, default 0): where publication
 * writes. In a pipelined frame the slave runs the ticks and orig_publish for
 * N+1 while the master draws N, so producer and consumer must not share the
 * draw-facing globals. Producers write through FZX_PUB / FZX_PUB_WORD: in a
 * slave job fzx_pub_out points at the low-WRAM slot being built, otherwise
 * (serial frames, and every build without the knob) they are exactly the
 * global. The master copies a finished slot into the globals before it draws.
 * Without the knob these macros expand to the bare global: no change. */
#ifndef FZX_SIMPIPE_PUB_H
#define FZX_SIMPIPE_PUB_H
#ifndef FZX_SIM_PIPELINE
#define FZX_SIM_PIPELINE 0
#endif
#if FZX_SIM_PIPELINE
#include <stdbool.h>
#include <stdint.h>
#include "fzx.h"
#include "ref.h"
#include "fzx_countdown.h"
#include "fzx_hud.h"
#include "road_plan.h"
#include "skyline_state.h"
#include "backdrop_state.h"
#include "countdown_state.h"
/* Every draw-facing publication output (SIM-PIPELINE-PLAN.md, checkpoint 3
 * table). The core's pose/view statics are not here: the pipeline requires
 * FZX_NATIVE_CORE_EMPTY, whose render does not read them. */
typedef struct {
    uint32_t magic, generation, sequence, ticks;   /* ticks: the log index after the batch */
    uint32_t executed;                             /* ticks executed in this generation */
    uint32_t camera_status, sky_status, skyline_status, road_plan_status, road_plan_sequence;
    uint32_t results_draw;                         /* results would draw: never in a pipelined frame */
    int machine_pub;                               /* vehicle/machine_native.c type */
    fzx_countdown_phase_t countdown_phase;
    bool countdown_published;                      /* countdown scene_t type */
    RefPose pose;
    RefCamera camera;
    fzx_view_t view;
    RefMachine machine;
    RefCountdown countdown;
    RefSkyline skyline;
    RefBackdrop backdrop;
    RefRoadPlan road_plan;
    fzx_hud_clock_t hud_clock;
} fzx_pub_t;
extern fzx_pub_t *volatile fzx_pub_out;
#define FZX_PUB(field, global) (*(fzx_pub_out ? &fzx_pub_out->field : &(global)))
#define FZX_PUB_WORD(field, global) \
    (*(fzx_pub_out ? (volatile uint32_t *)&fzx_pub_out->field : &(global)))
/* The tick of the publication being drawn: the master's draw-side stamps use
 * it, not the live log index the slave is advancing. */
extern volatile uint32_t fzx_simpipe_draw_tick;
#define FZX_DRAW_TICK fzx_simpipe_draw_tick
#else
#define FZX_PUB(field, global) global
#define FZX_PUB_WORD(field, global) global
#define FZX_DRAW_TICK fzx_input_count
#endif
#endif
