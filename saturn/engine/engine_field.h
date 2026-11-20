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
#ifndef E2_PACK
#define E2_PACK 0
#endif
#ifndef E2_FASTPOSE
#define E2_FASTPOSE 0
#endif
#ifndef E2_CULL
#define E2_CULL 0
#endif
#ifndef E2_LOD
#define E2_LOD 0
#endif
#ifndef E2_EARLY
#define E2_EARLY 0
#endif
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

/* E2 field lane: [0] pack (1 ok, 2 refused, 3 count/length, 4 starts), [1] max node start delta Q8,
 * [2] cars drawn last frame, [3] max drawn, [4] culled behind, [5] culled far, [6] culled off screen,
 * [7] shadows last frame, [8] culled total, [9]..[15] spare. */
extern volatile uint32_t fzx_e2_report[16];
#if E2_PACK
/* Before fzx_field_load: RIVALS.PAK from the disc (size and hash checked). */
bool fzx_field_pack_load(void *scratch, uint32_t scratch_bytes);
#endif
/* After the physics load: builds the line and the field (high work RAM). */
bool fzx_field_load(const sh2z_vd_track_t *track, const sh2z_vd_body_t *player);
/* One tick, after the player's step: the player into the field, then the field. */
void fzx_field_tick(const sh2z_vd_body_t *player);
#if defined(ENG_ATTACKS) && ENG_ATTACKS
/* The player's distance along the field's line and lateral (Q8), as the field takes them each tick. */
void fzx_field_player_line(const sh2z_vd_body_t *player, int32_t *dist, int32_t *lateral);
#endif
/* This frame's cars (player first) with the K/D selection applied; returns the count. */
unsigned fzx_field_cars(fzx_field_car_t *out, const sh2z_vd_body_t *player);
#if defined(ENG_SCENES) && ENG_SCENES
/* SCENES.md 11.2 / 11.3: the cup entrants (the player, then the field's rivals) and each car's
 * state at the classification trigger (fills key frame, course length and laps). */
#include "engine_cup.h"
bool fzx_field_entrants(fzx_entrants_t *out, uint8_t player_machine, uint8_t player_skin);
unsigned fzx_field_classify_in(fzx_cl_in_t *out, int32_t player_ms, fzx_cl_key_t *key);
#endif
#endif
