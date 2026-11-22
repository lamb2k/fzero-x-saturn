/* Engine attacks, racer-racer contact, damage and KOs (lane claude/attacks, generated/ATTACKS-PLAN.md).
 *
 * One contact rule for every pair (the player and the 29 rivals): SH2ZAM body-contact-1, the
 * original's sphere rule (racer.c Racer_Update): reach 46, along-line gap 92, impact 1.1 x |n . dv|
 * shared by contact weight, a push while closing, an attacker's victim pushed 3x with no grip for
 * 60 ticks, the human player's attacks 10x. The player takes its contact through SH2ZAM
 * vehicle-dynamics-3 (sh2z_vd_step_contact). Rivals are line-field agents; a rival that attacks, is
 * hit by an attack, spins out or has crashed is driven here as an external vehicle (a line-space
 * body with lateral momentum, walls and damage) and handed back to the field afterwards.
 *
 * Energy below zero: a 150-tick spin-out, then crashed and retired (Racer_RetireRacer: the retire
 * frame and the place at that moment are kept for the classification). Only a rival the player can
 * see may be knocked out, as the original's machine LOD 0 rule: the stand-in visibility test is
 * the chase camera of the current player state (engine_drive.c's eye and pitch, no roll limit),
 * depth in (0, 2500] units and inside a fixed view of tan 0.75 across and 0.5625 up (FZX_ATK_TAN_*).
 * A rival out of view stays at 0.1 energy instead. fzx_atk_vis_gate 0 makes everyone KO-able
 * (the HOST reference harness's behaviour, where no racer is ever drawn).
 *
 * KO credit (func_8008DBB8): when a rival starts spinning out within 60 ticks of a contact the
 * player started with it, and the player is not finished, crashed or spinning: the player's KO
 * count, the KO log, 1/8 of max energy back on the next tick, and fzx_atk_ko_hook.
 *
 * CPU attacks (cpu.c): thinking on (id & 3) == tick % 4, on screen, after 1 s, on roads wider
 * than 138: chasers attack their chase target (side by a two-press double tap, or spin), by the
 * per-personality and difficulty chances; EXPERT and MASTER add attacks on the racer just behind;
 * any difficulty punishes a player driving backwards near CPUs. fzx_atk_difficulty sets the
 * difficulty (1 STANDARD).
 *
 * Plain C: the same file builds for HOST tests (no Saturn headers). */
#ifndef FZX_ENGINE_ATTACKS_H
#define FZX_ENGINE_ATTACKS_H
#include <stdbool.h>
#include <stdint.h>
#include "sh2zam/vehicle_dynamics_experimental.h"
#include "sh2zam/line_field_experimental.h"
#include "sh2zam/body_contact_experimental.h"

#ifndef ENG_ATTACKS
#define ENG_ATTACKS 0
#endif
#define FZX_ATK_MAX 30u
#define FZX_ATK_KO_LOG 32u

/* What the field gives the attacks (engine_field.c owns the state). */
typedef struct {
    sh2z_field_t *field;
    sh2z_vehicle_t *veh;
    sh2z_field_brain_t *brain;
    const sh2z_line_t *line;
    /* Q16 world position and Q1.14 rows (forward, up, left) of a line-space state. */
    void (*pose)(const sh2z_track_state_t *t, int32_t pos[3], int16_t rows[3][3]);
    const int32_t *config;        /* per vehicle, config_stride words: grid, lap ms, chase, max energy Q8, aggression, character, ... */
    uint32_t config_stride;
    uint32_t count;
} fzx_atk_field_t;

/* Run-time words (pokeable). */
extern volatile uint32_t fzx_atk_difficulty;  /* 0 NOVICE .. 3 MASTER */
extern volatile uint32_t fzx_atk_vis_gate;    /* 1: only visible rivals can be knocked out (default) */
extern volatile uint32_t fzx_atk_ai;          /* 1: CPU attacks on (default) */
/* [0] contacts started, [1] pairs tested last tick, [2] pairs in reach last tick, [3] KOs (player),
 * [4] rivals spun out, [5] rivals retired by damage, [6] side attacks started by CPUs, [7] spin attacks
 * by CPUs, [8] attack hits, [9] damage held at 0.1 off screen, [10] player damage total Q8, [11] external
 * rivals now, [12] player attacks that hit, [13] wall hits of external rivals, [14] state hash, [15] ticks. */
extern volatile uint32_t fzx_atk_report[16];
/* Rival damage by source (counters only): [0] contact, [1] attack, [2] wall; incidents and energy Q8. */
extern volatile uint32_t fzx_atk_dmg_src[3][2];
/* KO log: tick (field ticks since the start), victim, attacker (0 the player). */
typedef struct { uint32_t tick; uint8_t victim, attacker, reserved[2]; } fzx_atk_ko_t;
extern fzx_atk_ko_t fzx_atk_kos[FZX_ATK_KO_LOG];
/* Per vehicle: KOs credited to it (only the player can earn them). */
extern uint8_t fzx_atk_ko_count[FZX_ATK_MAX];
/* Per vehicle at retirement (Racer_RetireRacer): the field tick since the start (-1 not retired)
 * and the place (1-based) it held at that moment. The classification orders retired vehicles by
 * raceDistancePosition = frame + (total - place) / 10, so these two give the exact order. */
extern int32_t fzx_atk_retire_tick[FZX_ATK_MAX];
extern uint8_t fzx_atk_retire_place[FZX_ATK_MAX];
/* Called on each KO with the victim (the cup counts KOs and the fifth gives a spare machine). */
extern void (*volatile fzx_atk_ko_hook)(uint32_t victim);

/* After the field is built. */
void fzx_atk_init(const fzx_atk_field_t *f, uint32_t seed);
/* Before the player's step: every contact this tick; fills the player's contact. */
void fzx_atk_contact(const sh2z_vd_body_t *player, sh2z_vd_contact_t *out);
/* Contact deferral (lane claude/attacks-5): true when this tick's contact cannot touch the player
 * (no push, damage, hit or regain), given the player's state before its step and its distance and lateral on the
 * line, and whether one field tick (with this tick's known contact pushes) still runs before it.
 * Then the contact may run after the slave's kick with the same result. Conservative: the bound on
 * every rival's travel over the pending field tick is in engine_attacks.c. */
/* A contact run ahead of its tick (before the master waits for the slave's track): KO hooks it raises
 * are held (fzx_atk_hold_hooks(true) around it) and raised when the tick is taken (release). */
void fzx_atk_hold_hooks(bool on);
void fzx_atk_release_hooks(void);
bool fzx_atk_player_clear(const sh2z_vd_body_t *player, int32_t player_dist, int32_t player_lateral, bool field_pending);
/* After the player's step, before the field step: player spin-out bookkeeping, rivals driven here. */
void fzx_atk_move(const sh2z_vd_body_t *player);
/* After the field step: retirements, CPU attack decisions, the state hash. */
void fzx_atk_after(const sh2z_vd_body_t *player);
/* True while rival i is driven here (spin-out, crashed, attacking, knocked). */
bool fzx_atk_external(uint32_t i);
/* The visibility stand-in for machine LOD != 0 (see the top of this file). */
bool fzx_atk_visible(const sh2z_vd_body_t *player, uint32_t i);
/* The field the attacks run on (as given to fzx_atk_init). */
const fzx_atk_field_t *fzx_atk_field(void);
/* HOST tests only (the retire-order case): damage to rival i as a contact would deal it (Q16). */
void fzx_atk_test_damage(const sh2z_vd_body_t *player, uint32_t i, int32_t amount);
/* FNV-1a of the attack state (determinism and HOST = SH-2). */
uint32_t fzx_atk_hash(void);
/* Every 256 field ticks: the field hash mixed with the attack hash (HOST = SH-2 checkpoints). */
#define FZX_ATK_CHECKPOINTS 64u
extern uint32_t fzx_atk_chk[FZX_ATK_CHECKPOINTS];
/* The scripted hunter (the 5-KO scenario): steers beside the nearest rival ahead and side-attacks it.
 * Writes the N64 button word (A held; Z or R for the double tap) and the stick; returns 1 when it
 * has a target. Deterministic, from the player's state and the field only. */
int fzx_atk_hunter(const sh2z_vd_body_t *player, uint16_t *buttons, int8_t *stick);
#endif
