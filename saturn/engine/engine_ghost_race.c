/* Time Attack ghost, race side (ENG_GHOST=1): records the player, plays the kept ghost, saves a new best. See engine_ghost.h. */
#define FZX_GHOST_RACE_API
#include <string.h>
#include <yaul.h>
#include "engine_ghost.h"
#include "engine_modes.h"
#include "engine_records.h"
#include "engine_scenes.h"
#include "sh2zam/vehicle_dynamics_experimental.h"

extern fzx_session_t fzx_session;
volatile uint32_t fzx_ghost_on = 1u;     /* run-time: 0 = no recording, no playback (the control) */
/* The playback ghost's position this tick, Q16 (valid while fzx_ghost_report[1]); the renderer draws the translucent machine there. */
volatile int32_t fzx_ghost_pos[3];
/* [0] race begun in Time Attack with the ghost on, [1] playback live, [2] playback ticks, [3] recording samples so far (ticks / 2),
 * [4] recording overrun, [5] saved (1) after the last race, [6] save result, [7] kept ghost's race ms, [8] recorded bytes */
volatile uint32_t fzx_ghost_report[16];

/* The recording and the kept ghost: 8,236 B each, appended and read once every second tick (about 12 B), so low work RAM
 * (engine_low.x, ENG_LOW_COLD) holds them and the master stack keeps the room (the 24-course image with the modes did not link
 * with them in high work RAM). The backup library's staging copy (engine_records.c ghost_io) stays in high work RAM. */
#if ENG_LOW_COLD
__attribute__((section(".bss.lowcold.ghost_rec")))
#endif
static fzx_ghost_t rec_g;
#if ENG_LOW_COLD
__attribute__((section(".bss.lowcold.ghost_play")))
#endif
static fzx_ghost_t play_g;
static fzx_ghost_play_t play;
static bool rec_on, play_on, rec_done;

static bool save_queued;   /* a save is queued in the records service and its result is not yet in the report */

/* One backup-RAM file of the queued save (one menu frame's share); the report takes the result when the head file, the commit, is written. */
void fzx_ghost_race_pump(void) {
    if (!save_queued) return;
    if (fzx_records_ghost_pump()) return;
    save_queued = false;
    fzx_ghost_report[5] = fzx_ghost_store[5] == FZX_REC_OK; fzx_ghost_report[6] = fzx_ghost_store[5];
}
/* Everything queued is written now (a race begins: the next load reads the same files and buffer). */
void fzx_ghost_race_flush(void) {
    while (save_queued) fzx_ghost_race_pump();
}

void fzx_ghost_race_begin(void) {
    fzx_ghost_race_flush();
    rec_on = play_on = rec_done = false;
    memset((void *)fzx_ghost_report, 0, sizeof(fzx_ghost_report));
    if (!fzx_ghost_on || fzx_session.menu_mode != FZX_MODE_TA) return;
    fzx_ghost_rec_begin(&rec_g, fzx_session.course);
    rec_on = true;
    if (fzx_records_ghost_load(&play_g) && play_g.course == fzx_session.course) {
        fzx_ghost_play_begin(&play, &play_g);
        play_on = true;
        fzx_ghost_report[7] = play_g.race_ms;
    }
    fzx_ghost_report[0] = 1u;
}

void fzx_ghost_tick(const sh2z_vd_body_t *b) {
    if ((!rec_on && !play_on) || b->race_time <= 0) return;
    if (rec_on && !(b->flags & SH2Z_VD_FINISHED)) {
        fzx_ghost_rec_tick(&rec_g, b->pos);
        fzx_ghost_report[3] = (fzx_ghost_report[3] + 1u);
        fzx_ghost_report[4] = fzx_ghost_rec_overrun(&rec_g);
    }
    if (play_on) {
        int32_t p[3];
        if (fzx_ghost_play_tick(&play, p)) {
            fzx_ghost_pos[0] = p[0]; fzx_ghost_pos[1] = p[1]; fzx_ghost_pos[2] = p[2];
            ++fzx_ghost_report[2]; fzx_ghost_report[1] = 1u;
        } else { play_on = false; fzx_ghost_report[1] = 0u; }
    }
}

/* A new best was committed (Time Attack, place 1): queue this race's ghost (the pump writes it, a file a menu frame). */
fzx_rec_result_t fzx_ghost_race_save(const fzx_run_result_t *run) {
    if (!rec_on || rec_done || run->menu_mode != FZX_MODE_TA || run->reason != FZX_END_FINISH) return FZX_REC_ABSENT;
    if (!fzx_ghost_rec_end(&rec_g, run->race_ms)) { fzx_ghost_report[6] = 0xFFu; return FZX_REC_FAILED; }
    rec_done = true;
    fzx_ghost_report[8] = rec_g.bytes;
    fzx_ghost_report[5] = 0u; fzx_ghost_report[6] = 0u;
    save_queued = true;
    return fzx_records_ghost_save_begin(&rec_g);
}
