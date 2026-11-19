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
uint32_t fzx_drive_tick(void);
/* The chase camera for the current state; focal and tan_half from `lens`. */
void fzx_drive_camera(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens);
/* The player sprite (user clip, shadow, sprite) after the track; returns commands written. */
unsigned fzx_drive_sprites(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                           const fzx_engine_cam_t *cam);
/* HUD text (dbgio, VDP2). */
void fzx_drive_hud(void);
#endif
