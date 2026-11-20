/* Engine race effects (lane claude/fx, ENG_FX, default 0; generated/FX-PLAN.md).
 *
 * The source race's effects (racer.c and ovl_i2/effects.c), drawn with the source art
 * (tools/fx_assets.py: FX.PAK in VDP1 VRAM below the 3D player) as VDP1 commands after the
 * cars, sorted far to near, at most FZX_FX_SLOTS a frame. Particle rings, projection and the
 * command words are SH2ZAM effect-pool-1.
 *  - Spin-out: a spinning rival turns through its yaw cells at the source's rate (steer
 *    0.2 x 0.99^t rad a tick, then the wreck keeps turning as it decays); a spin attack
 *    turns the model by its attack angle (the player's from vehicle-dynamics, the rivals' by
 *    the same rule); spinning racers jitter by up to 0.9 units. Every fourth tick an explosion
 *    puff and two flying sparks (gExplosions1, gFlyingSparks).
 *  - Sparks: on the first tick of a contact (gCollisionSparks at the pair's midpoint) and on
 *    wall hits (at the wall side), each racer at most one per five ticks (bodyWhiteTimer),
 *    which also flashes the 3D player white.
 *  - Retire: the big explosion (gExplosions2) and machine debris in the body colour; a wreck
 *    destroyed by a later hit explodes again.
 *  - Boost: each racer's boosters (sBoosterInfos) as source glows, cyan idle, green while
 *    boosting, yellow on a dash plate; within 400 units a tail to speed x 0.9 behind; none
 *    beyond 900 units, as the source's booster LOD.
 *  - Damage: crashed racers throw sparks and smoke (gSmokes); the 3D player's body flickers
 *    to red below half energy, as the source (its palettes go to its own table only).
 *  - Attack highlight: a side attack's red shell, a spin attack's yellow-to-red pulsing shell, a
 *    wreck's yellow flash: the car's own commands again behind it, scaled, in one colour
 *    (fzx_fx_shell, drawn by fzx_drive_cars).
 *  - Boost flare: an accelerator press grows the player's booster glow (racer.c unk_214).
 * Cost: the frame's emit runs on whichever SH-2 claims it first (dual-claim-1, engine_main.c);
 * the budget keeps the nearest FZX_FX_SLOTS commands; the effects' own projection is
 * effect-pool-1's one-divide spot.
 * Effects read the simulation and never write it: their random numbers are their own and
 * nothing in the race state depends on them (HOST checkpoints and the race are unchanged).
 * Needs ENG_ATTACKS=1 (the events) and E2_EARLY (the staged car list). */
#ifndef FZX_ENGINE_FX_H
#define FZX_ENGINE_FX_H
#include <stdbool.h>
#include <stdint.h>
#include <yaul.h>
#include "fzx_view.h"
#include "engine_scene.h"
#include "engine_field.h"
#include "sh2zam/vehicle_dynamics_experimental.h"

/* VDP1 commands the effects may add per frame (the fixed budget). */
#define FZX_FX_SLOTS 96u

/* [0] status (1 ok; 2 pack load, 3 VRAM), [1] commands last frame, [2] max, [3] particles live,
 * [4] sparks spawned, [5] puffs (spin-out), [6] retire explosions, [7] debris spawned,
 * [8] smoke spawned, [9] spawns refused (rings full), [10] commands dropped (budget),
 * [11] booster commands last frame, [12] VDP1 offset of the art, [13] emit master FRT last frame,
 * [14] its max, [15] spinning rivals drawn turned last frame. */
extern volatile uint32_t fzx_fx_report[16];
/* The last 32 effect events: race-effect tick << 8 | kind (1 retire explosion, 2 wreck destroyed,
 * 3 a rival's spin-out starts). Evidence only. */
extern volatile uint32_t fzx_fx_events[32];
/* Commands the effects asked for: [0] last frame, [1] the most in one frame (the budget's demand). */
extern volatile uint32_t fzx_fx_demand[2];

/* After the 3D player's load: FX.PAK just below texture offset `ceiling`. Returns the new floor
 * (0 on failure). */
uint32_t fzx_fx_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes);
/* Retry (the resident set kept): every effect and effect state back to the race's start. */
void fzx_fx_reset(void);
/* Every tick, after the contacts and before the player's step: contact sparks, the rivals'
 * spin, retire and wreck effects, and one particle step. */
void fzx_fx_tick(const sh2z_vd_body_t *player);
/* Every tick, after the player's step: the player's wall sparks, spin-out and retirement. */
void fzx_fx_player(const sh2z_vd_body_t *player, int32_t max_energy, bool accel_pressed);
/* Every frame, on the cars of fzx_field_cars: spin turns and jitter (render only). */
void fzx_fx_pose(fzx_field_car_t *cars, unsigned n);
/* The player's pose differs from its body this frame (a spin attack or jitter): draw the 3D
 * player from cars[0] instead of the body. */
bool fzx_fx_player_posed(void);
/* The attack highlight of car id this frame (racer.c's attackHighlight): the colour-table word
 * (CMDCOLR) for CLUT sprites, the RGB colour for polygons and the Q16 scale. False: none. */
bool fzx_fx_shell(uint32_t id, uint16_t *colr, uint16_t *rgb, int32_t *scale);
extern volatile uint32_t fzx_fx_shells;
/* Every frame, on the master after fzx_drive_cars (the shells): the emit's inputs (view, cars,
 * the player's body colour). False: nothing to emit. */
bool fzx_fx_stage(const fzx_view_t *view, const fzx_engine_cam_t *cam, const fzx_field_car_t *cars, unsigned n);
/* The effects' commands, far to near, from the staged frame: on either SH-2, whichever claims the
 * frame's job (engine_main.c, SH2ZAM dual-claim-1). Returns the count. */
unsigned fzx_fx_job(vdp1_cmdt_t *commands, unsigned capacity);
/* On the master once the job is done: the counters (slave: true when the slave ran it). */
void fzx_fx_job_done(bool slave);
extern volatile uint32_t fzx_fx_on_slave;
#endif
