/* Engine race effects without the attack code (lane claude/fx-boost, ENG_FX, on in the field images).
 *
 * The source race's boost flame and wall sparks (racer.c Racer_Draw's booster pass, Racer_HitWall
 * and ovl_i2/effects.c), drawn with the source art (tools/fx_assets.py: FX.PAK in VDP1 VRAM below
 * the 3D player) as VDP1 commands after the cars, sorted far to near, at most FZX_FX_SLOTS a frame.
 * Particle ring, projection and the command words are SH2ZAM effect-pool-1.
 *  - Boosters: each racer's sockets (sBoosterInfos, one 17-word row per rendered machine id; the guest
 *    ids 33 and 34 through fzx_fx_guest_boosters) as source glows, cyan idle, green while boosting,
 *    yellow on a dash plate, sized 0.35 x socket size + flicker + 7.5 sqrt(boost timer / 100) and the
 *    machines' hover share; within 400 units a tail to speed x 0.9 behind; none beyond 900 units
 *    (the source's booster LOD). An accelerator press flares the player's glow for 27 ticks.
 *  - Wall sparks: the player's wall hit (the vehicle's RECOIL bit) and a rival's wall touch, at the
 *    wall side +-15 units, one racer at most one per five ticks (bodyWhiteTimer), also flashing the
 *    3D player's body white; the player's body flickers red below half energy, as the source.
 * Effects read the simulation and never write it: their random numbers are their own and nothing in
 * the race state depends on them (HOST checkpoints and the race are unchanged).
 * Racer-racer contact sparks, spin-out, explosions, debris, smoke and the attack highlight belong to
 * the attack code and are not here. Needs E1_FIELD with E2_PACK and E2_EARLY (the staged car list). */
#ifndef FZX_ENGINE_FX_H
#define FZX_ENGINE_FX_H
#include <stdbool.h>
#include <stdint.h>
#include <yaul.h>
#include "fzx_view.h"
#include "engine_scene.h"
#include "engine_field.h"
#include "sh2zam/vehicle_dynamics_experimental.h"

/* VDP1 commands the effects may add per frame (the fixed budget; the nearest are kept). */
#define FZX_FX_SLOTS 96u

/* [0] status (1 ok; 2 pack load, 3 VRAM), [1] commands last frame, [2] max, [3] particles live,
 * [4] sparks spawned, [5] frames the player boosts, [6] of them on a dash plate, [7] the most booster
 * commands in a frame, [8] frames with live sparks, [9] spawns refused (ring full), [10] commands
 * dropped (budget),
 * [11] booster commands last frame, [12] VDP1 offset of the art, [13] emit master FRT last frame,
 * [14] its max, [15] booster rows drawn empty (a guest id with no row). */
extern volatile uint32_t fzx_fx_report[16];
/* Commands the effects asked for: [0] last frame, [1] the most in one frame (the budget's demand). */
extern volatile uint32_t fzx_fx_demand[2];
#ifndef ENG_FX_PROF
#define ENG_FX_PROF 0
#endif
#if ENG_FX_PROF
/* Diagnostic (ENG_FX_PROF=1): per frame row, five halfwords: [0] fzx_fx_stage, [1] the master's jobs and [2] the slave's jobs (master FRT units),
 * [3] commands out | slave job mask << 7 (4 bits) | rivals staged << 11, [4] a fold of the collected commands (to compare two emitters). */
enum { FZX_FX_PROF_ROWS = 4200u };
extern uint16_t fzx_fx_prof[FZX_FX_PROF_ROWS][5];
extern uint32_t fzx_fx_prof_row;
extern volatile uint32_t fzx_fx_agg[2][16];
extern volatile uint32_t fzx_fx_sagg[8];
#endif
/* Frames whose effects the slave emitted. */
extern volatile uint32_t fzx_fx_on_slave;

/* After the 3D player's load: FX.PAK just below texture offset `ceiling`. Returns the new floor
 * (0 on failure). */
uint32_t fzx_fx_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes);
/* Retry (the resident set kept): every effect and effect state back to the race's start. */
void fzx_fx_reset(void);
/* Every tick, after the player's step: the player's wall sparks and flare, the rivals' wall
 * touches, and one particle step. */
void fzx_fx_tick(const sh2z_vd_body_t *player, int32_t max_energy, bool accel_pressed);
/* Every frame on the master after the cars are staged: the emit's inputs (view, the player, the
 * rivals' boosts) are copied. player_machine is the rendered machine id of the player. False: nothing
 * to emit. */
bool fzx_fx_stage(const fzx_view_t *view, const fzx_engine_cam_t *cam, const fzx_field_car_t *cars, unsigned n,
                  uint32_t player_machine);
/* The staged frame is emitted as four jobs of about equal cost (0: the player's boosters, the sparks and the nearest
 * rivals; then the next rivals in turn), each on either SH-2 (engine_main.c, SH2ZAM dual-claim-1: the slave takes
 * the first free job, the master the last), and the master merges their far-to-near lists. */
#define FZX_FX_JOBS 4u
void fzx_fx_job(unsigned job);
/* On the master once both jobs are done (slave_mask bit j: job j ran on the slave): the commands, far to near,
 * into dst, at most capacity. Returns the count. */
unsigned fzx_fx_collect(vdp1_cmdt_t *dst, unsigned capacity, unsigned slave_mask);
/* The counters of the frame's jobs (out: the count collected). */
void fzx_fx_job_done(unsigned slave_mask, unsigned out);
#endif
