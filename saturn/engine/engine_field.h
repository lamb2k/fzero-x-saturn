/* Engine E1 close-up 3D-N test (E1_FIELD = rivals, generated/CLOSEUP-3DN.md): a rival
 * field around the player, so the cost of several 3D cars can be measured.
 *
 * The rivals are the SH2ZAM line field (line-field-1, as the E2 rivals lane's HOST
 * model) on a line built at load from the physics course itself: stations about every
 * 64 units along each spline span, their distances from the span's baked arc lengths,
 * poses from the spline (position, tangent, interpolated road up). The E2 lane's
 * RIVALS.PAK (CPU line lateral and speed caps from the ROM) is not used: lateral 0,
 * one speed cap, the course's half widths. Field config: synthetic, a 29-car grid
 * ahead of the player, target laps spread around the player's.
 *
 * The player is the field's external vehicle 0 (rank, contact). Contact kicks on the
 * player are NOT applied, so the player's race is unchanged.
 *
 * Every frame the nearest K cars (the player first) within distance D of the player
 * are drawn as 3D models, the rest as sprites. A car changes form only after 8 frames
 * past D - margin (to 3D) or D + margin (to sprite). K and D are run-time words
 * (fzx_field_k, fzx_field_d) so one image serves every arm. */
#ifndef FZX_ENGINE_FIELD_H
#define FZX_ENGINE_FIELD_H
#include <stdbool.h>
#include <stdint.h>
#include "sh2zam/vehicle_dynamics_experimental.h"

#ifndef E1_FIELD
#define E1_FIELD 0
#endif
#define FZX_FIELD_CARS (E1_FIELD + 1)
#define FZX_FIELD_K_MAX 6

/* A car for this frame: Q16 position, Q1.14 rows (forward, up, left), 3D or sprite. */
typedef struct {
    int32_t pos[3];
    int16_t basis[3][3];
    uint8_t is3d, slot; /* slot: the 3D slot (shade tables) when is3d */
    uint16_t id;        /* 0 the player */
} fzx_field_car_t;

/* Run-time arm words (pokeable): K 3D cars (0: all sprites, the control), D and the
 * margin in whole units. */
extern volatile uint32_t fzx_field_k, fzx_field_d, fzx_field_margin;
/* [0] status (1 ok; 2 line, 3 field), [1] stations, [2] line length Q8, [3] field ticks,
 * [4] started tick, [5] contacts, [6] cars within 400 u last frame, [7] 3D cars last frame,
 * [8] switches to 3D, [9] switches to sprite, [10] max cars within 400 u, [11] rivals finished,
 * [12] rivals retired, [13] left sign, [14] hover Q16, [15] field hash. */
extern volatile uint32_t fzx_field_report[16];

/* After the physics load: builds the line and the field (high work RAM). */
bool fzx_field_load(const sh2z_vd_track_t *track, const sh2z_vd_body_t *player);
/* One tick, after the player's step: the player into the field, then the field. */
void fzx_field_tick(const sh2z_vd_body_t *player);
/* This frame's cars (player first) with the K/D selection applied; returns the count. */
unsigned fzx_field_cars(fzx_field_car_t *out, const sh2z_vd_body_t *player);
#endif
