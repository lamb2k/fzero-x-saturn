/* Engine line, milestone E1 (docs/ENGINE_PLAN.md): the drive slice.
 *
 * Native player physics (SH2ZAM vehicle-dynamics-1 through saturn/physics_native.c),
 * the chase camera that follows it, the player drawn as a rotation sprite
 * (SH2ZAM rotation-sprites-1, the draw16 path of claude/racer-sprites) and a
 * basic HUD (speed, lap, time, finish). No original game code is linked.
 *
 * Input is the real pad, or the R01 closed-loop test driver (the plays-right
 * fixtures' driver: 63 * clamp(kl * lateral + kh * heading), A held from the
 * race start), selected at run time by fzx_e1_driver. */
#ifndef FZX_ENGINE_DRIVE_H
#define FZX_ENGINE_DRIVE_H
#include <stdbool.h>
#include <stdint.h>
#include <yaul.h>
#include "engine_scene.h"
#include "fzx_view.h"

/* Close-up A/B (Makefile E1_CLOSEUP): 0 the sprite player, 1 the 3D model (engine_closeup.h),
 * 2 the high-quality near sprite set. */
#ifndef E1_CLOSEUP
#define E1_CLOSEUP 0
#endif
/* Close-up 3D-N (Makefile E1_FIELD = rivals, engine_field.h); 0 builds no field. */
#ifndef E1_FIELD
#define E1_FIELD 0
#endif

/* 1: the R01 test driver steers (60 Hz, from each tick's previous state);
 * 0: the pad, read once per presented frame and held for its ticks. */
extern volatile uint32_t fzx_e1_driver;
/* Pad override for captures: bit 16 set uses the low 16 bits as the pad. */
extern volatile uint32_t fzx_e1_pad_override;
/* [0] ticks stepped, [1] laps done, [2] race time ms, [3] finished (1), [4] speed Q16,
 * [5] stop tick (driver mode), [6] final state hash hi, [7] lo, [8] checkpoints written,
 * [9] sprite status (1 ok), [10] sprite draws last frame, [11] sprite skips,
 * [12] physics bytes, [13] lap 1 ms, [14] lap 2 ms, [15] lap 3 ms. */
extern volatile uint32_t fzx_e1_report[16];
/* 1 loaded; 2 pack load, 3 pack format, 4 sprite load, 5 sprite set, 6 shadow. */
extern volatile uint32_t fzx_e1_status;

/* Loads the physics pack (CD to work RAM). False on failure (status word says why). */
bool fzx_drive_load(void *scratch, uint32_t scratch_bytes);
/* After the track and the VDP1 banks: the raw sprite set into the cartridge from
 * cart_free, its CLUTs and the shadow into the CLUT partition after clut_used bytes,
 * and the sprite slot pool at the top of the texture partition (sprite-cache-1). */
#if E1_FIELD && E2_ROSTER
/* E2_ROSTER: the roster pack into the cartridge, each rival bound to its machine's near/far sets. */
bool fzx_drive_roster_load(void *scratch, uint32_t scratch_bytes, uintptr_t cart_limit);
extern volatile uint32_t fzx_e2_roster[8];
#endif
bool fzx_drive_sprites_load(const vdp1_vram_partitions_t *parts, uint32_t clut_used, uintptr_t cart_free,
                            uintptr_t cart_limit, void *scratch, uint32_t scratch_bytes);
/* Texture-partition offset where the slot pool starts (E0 users must end below it). */
uint32_t fzx_drive_texture_floor(void);
/* Slave, after the track build: copy queued cells into their slots (1 ms budget). */
void fzx_drive_slave_fill(void);
/* VDP1 map and cache counters for captures (engine_drive.c). */
extern volatile uint32_t fzx_e1_vram[16], fzx_e1_cache[12], fzx_e1_sprite_pos[4];
/* Steps the physics `ticks` times (60 Hz), reading the pad once. */
void fzx_drive_step(uint32_t ticks);
#if ENG_PHYS_AHEAD
/* ENG_PHYS_AHEAD: compute the next `ticks` ticks on a copy of the state, for the next fzx_drive_step to take. */
void fzx_drive_ahead_prepare(uint32_t ticks);
#if defined(ENG_ATTACKS) && ENG_ATTACKS
extern volatile uint32_t fzx_e1_ahead[12]; /* [8], [9]: the contact run ahead after the picture (engine_drive.c) */
#else
extern volatile uint32_t fzx_e1_ahead[8];
#endif
#endif
/* ENG_PICK_AHEAD (claude/speed-6, needs ENG_PHYS_AHEAD and the drive): the slave picks the next frame's track in the master's
 * idle time too, from the camera of the state the prediction ends in. */
#ifndef ENG_PICK_AHEAD
#define ENG_PICK_AHEAD 0
#endif
/* Controls (diagnostic only): 1 declares every third early pick unused (the undo path; the picture must equal the default's),
 * 2 gives the early pick the camera of the state before the prediction (a stale view; the picture must differ). */
#ifndef ENG_PICK_AHEAD_CONTROL
#define ENG_PICK_AHEAD_CONTROL 0
#endif
#if ENG_PICK_AHEAD
/* After fzx_drive_ahead_prepare: the camera, shown tick and outer bit of the predicted frame (false: no prediction). */
bool fzx_drive_ahead_view(const fzx_engine_cam_t *lens, const fzx_engine_cam_t **cam, uint32_t *tick, uint32_t *outer);
/* The chase camera for this frame (fzx_drive_camera's result), taken from the prediction when the step took it; true when taken. */
bool fzx_drive_camera_ahead(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens);
#endif
#if ENG_BAL_EARLY
/* ENG_BAL_EARLY: fzx_drive_step keeps the player's state after each tick and the rival
 * field steps here instead, after the slave's kick (the field reads the player and
 * never writes it). Same ticks, same order of field inputs. */
void fzx_drive_field_run(void);
#if defined(ENG_ATTACKS) && ENG_ATTACKS && defined(ENG_ATK_DEFER) && ENG_ATK_DEFER
/* ENG_ATK_DEFER: the next tick's contact, after the field ticks and before the master waits for the track. */
void fzx_drive_contact_ahead(void);
#endif
#endif
uint32_t fzx_drive_tick(void);
/* The chase camera for the current state; focal and tan_half from `lens`. */
void fzx_drive_camera(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens);
/* The player sprite (user clip, shadow, sprite) after the track; returns commands written. */
unsigned fzx_drive_sprites(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                           const fzx_engine_cam_t *cam);
#if E1_FIELD
#include "engine_field.h"
/* Every car of the field, far to near: sprites through the slot cache, 3D cars from
 * their staged commands (staged[slot], staged_n[slot]); returns commands written. */
unsigned fzx_drive_cars(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view, const fzx_engine_cam_t *cam,
                        const fzx_field_car_t *cars, unsigned n, const vdp1_cmdt_t *const *staged, const uint16_t *staged_n);
extern volatile uint32_t fzx_field_sim_ticks, fzx_field_draw[8];
#if defined(E2_EMIT_LINK) && E2_EMIT_LINK
/* E2_EMIT_LINK: fzx_drive_cars leaves the 3D cars' staged commands where they are and lists the
 * list's pieces in order (its own commands and the staged ones); the caller copies each piece once. */
typedef struct { const vdp1_cmdt_t *src; uint32_t n; } fzx_emit_seg_t;
extern fzx_emit_seg_t fzx_emit_segs[2u * FZX_FIELD_K_MAX + 2u];
extern unsigned fzx_emit_nsegs;
#endif
const sh2z_vd_body_t *fzx_drive_body(void);
#endif
#if FZX_PHYS_TUBES
/* 0 while the player is inside a pipe away from its joins (the road plan's outer bit). */
uint8_t fzx_drive_outer(void);
#endif
/* HUD text (dbgio, VDP2). */
void fzx_drive_hud(void);
/* The HUD text only, into the dbgio buffer; the caller flushes it (dbgio_flush). */
void fzx_drive_hud_text(void);
/* Formats the HUD text ahead of fzx_drive_hud_text (same strings, same state). */
void fzx_drive_hud_prepare(void);
#endif
