/* Engine sound (ENG_SND, generated/SOUND-PLAN.md): race events to SCSP clips and the course
 * music from its CD track, through SH2ZAM snd-1 (sh2z_snd, sh2z_cdda). The 68000 is parked.
 *
 * Hooks are single calls so they can move when the main loop is restructured:
 *   engine_snd_power()            user_init, before smpc_peripheral_init
 *   engine_snd_load(scratch, n)   boot, while the disc is free: the clip pack into sound RAM
 *   engine_snd_start(epoch)       after the last disc read: the course music, events open for epoch
 *   engine_snd_tick(body, vehicle) every physics tick, after the step (reads the state only)
 *   engine_snd_cars(body, cars, n) once per frame where the field's cars are known (rival engines)
 *   engine_snd_build()            once per frame build: this frame's events and engine targets, CD service
 *   engine_snd_seal()             at the frame's submit: its sound waits for its present
 *   engine_snd_vblank(presented)  every VBlank-In (kept tiny): applies a presented frame's sound
 *   engine_snd_frame()            build + seal + apply at once, for loops without a present callback
 *   engine_snd_leave(frames)      LOAD/leave: close the epoch, fade the music; engine_snd_quiet()
 *                                 says when the disc is free for reads. */
#ifndef ENGINE_SND_H
#define ENGINE_SND_H
#include <stdbool.h>
#include <stdint.h>
#include "sh2zam/vehicle_dynamics_experimental.h"
#if E1_FIELD
#include "engine_field.h"
#endif

/* Read by captures: [0] status (1 ok; 2 pack load, 3 TOC, 4 no track), [1] events posted,
 * [2] events delivered, [3] posts rejected, [4] plays, [5] plays refused, [6] slots stolen,
 * [7] CD commands, [8] CD loop-backs, [9] CD state, [10] player engine FNS|OCT<<16,
 * [11] nearest rival distance (units), [12] second rival distance, [13] boosts, [14] walls,
 * [15] laps announced. */
extern volatile uint32_t engine_snd_report[16];

void engine_snd_power(void);
bool engine_snd_load(void *scratch, uint32_t scratch_bytes);
void engine_snd_start(uint32_t epoch);
void engine_snd_tick(const sh2z_vd_body_t *body, const sh2z_vd_vehicle_t *vehicle, int32_t laps);
#if E1_FIELD
void engine_snd_cars(const sh2z_vd_body_t *body, const fzx_field_car_t *cars, unsigned n);
#endif
void engine_snd_build(void);
void engine_snd_seal(void);
void engine_snd_vblank(bool presented);
void engine_snd_frame(void);
void engine_snd_leave(uint32_t frames);
bool engine_snd_quiet(void);
#endif
