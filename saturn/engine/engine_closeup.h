/* Engine E1 close-up A/B, arm A (E1_CLOSEUP=1; generated/CLOSEUP-AB.md): the player
 * drawn as the 3D model instead of the rotation sprite.
 *
 * The legacy native vehicle path, unchanged: the prepared vehicle pack (MACHINE.PAK
 * metadata in work RAM, MACHTX.PAK texels in VDP1 VRAM; tools/machine_rebake.py),
 * the draw of src/fzx_vehicle_draw.c (FZX_VEHICLE_NATIVE: SH2ZAM vehicle-kernels-1,
 * template emit) and the pose of src/fzx_vehicle.c (FZX_VEHICLE_POSE1: SH2ZAM
 * pose-1). Its RefMachine/RefCamera inputs are filled here from the native physics
 * body and the chase camera; no original code runs. The lighting is the source's
 * outside-the-tube lighting (ambient 100, white light along (1, 1, 1), the course's
 * outside fog); the tube and pipe lighting is not modelled.
 *
 * VDP1 banks: the per-face Gouraud words are rewritten every frame while VDP1 may
 * still draw the previous list, so each bank has its own palette + Gouraud table
 * (and its own template commands that point at it). */
#ifndef FZX_ENGINE_CLOSEUP_H
#define FZX_ENGINE_CLOSEUP_H
#include <stdbool.h>
#include <stdint.h>
#include <yaul.h>
#include "engine_scene.h"
#include "fzx_view.h"

/* VDP1 commands the vehicle may write per frame (LOD 1: 112 faces, plus clip fans). */
#define FZX_CLOSEUP_SLOTS 112u

/* [0] status (1 ok; 2 pack load, 3 pack open, 4 texture load, 5 VRAM), [1] commands
 * last frame, [2] max, [3] sum, [4] frames drawn, [5] draw faults, [6] last fault,
 * [7] culled last frame, [8] clipped sum, [9] texel offset in VDP1 VRAM, [10] bank 0
 * shades, [11] bank 1 shades, [12] region end, [13] master FRT ticks last frame,
 * [14] max, [15] palette uploads. */
extern volatile uint32_t fzx_ab_report[16];

/* After the sprite load: the pack metadata to work RAM, the texels and two shade
 * tables into the texture partition just below texture offset `ceiling` (the sprite
 * slot pool). Returns the region's start offset (new floor for E0), 0 on failure. */
uint32_t fzx_closeup_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes);
/* Scenes image (ENG_SCENES=1): the same, from the pack of machine `family`
 * (MACH3D<ff>.PAK); [0] = 6 when the image has no pack for that family. */
uint32_t fzx_closeup_load_family(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes,
                                 uint32_t family);
bool fzx_closeup_has_family(uint32_t family);
/* Scenes image: per-race draw state back to its start (a retry; the pack stays). */
void fzx_closeup_reset(void);
/* The vehicle's commands after the player's shadow; returns the commands written. */
unsigned fzx_closeup_draw(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                          const fzx_engine_cam_t *cam, uint32_t bank);
/* Several 3D cars (FZX_CLOSEUP_CARS slots, each with its own shade and template tables
 * per bank): one car at a Q16 position with Q1.14 rows (forward, up, left). */
extern volatile uint32_t fzx_closeup_lod; /* the pose draw's source LOD (1..5) */
unsigned fzx_closeup_draw_pose(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                               const fzx_engine_cam_t *cam, uint32_t bank, uint32_t slot,
                               const int32_t pos[3], const int16_t basis[3][3]);
#if defined(ENG_FX) && ENG_FX
/* Race effects (engine_fx.c): the body colour the next draw of the 3D player shades with (the white
 * flash and the low-energy flicker); its palettes go to the player's own table only. */
void fzx_closeup_body_get(uint8_t rgb[3]);
void fzx_closeup_body_set(const uint8_t rgb[3]);
#endif
#endif
