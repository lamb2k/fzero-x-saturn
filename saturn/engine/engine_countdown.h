/* Engine race countdown (ENG_COUNTDOWN, docs/COUNTDOWN.md): the original 3, 2, 1, GO sign on the engine's own intro clock and camera.
 * The sign is the original's 23 planar quads (92 vertices, ROM-derived charts baked offline: COUNTDN.PAK, COUNTDTX.PAK). Its pose is
 * the original's (racer.c, the race intro: Matrix_SetLockedLookAt of the camera's backward axis around the racer's up axis, 0.2 scale,
 * placed from the racer along its up and forward axes and back along the camera's view axis, by a distance that falls for the first
 * 240 ticks and rises again in the last 30), computed here from the engine's body and camera. The panel and mouth states are the
 * original's thresholds on the intro timer (body.intro, which counts down from 460 as gRaceIntroTimer does):
 * digits 3 / 2 / 1 / GO at timer 220 / 160 / 100 / 40, a black panel above 220, the mouth open for 210..220, 150..160, 90..100 and 1..40.
 * Nothing here advances or changes the simulation: it reads the body and the camera. */
#ifndef ENGINE_COUNTDOWN_H
#define ENGINE_COUNTDOWN_H
#include <stdbool.h>
#include <stdint.h>
#include <yaul.h>
#include "engine_scene.h"
#include "fzx_view.h"
#include "sh2zam/vehicle_dynamics_experimental.h"

/* Read by captures: [0] status (0 unloaded, 1 ready, 2 range, 3 metadata read, 4 pack, 5 texture read, 6 prepare, 7 no VRAM room),
 * [1] frames drawn, [2] commands in the last drawn frame, [3] most commands in a frame, [4] frames whose draw was refused
 * (a visible textured face crossing the near plane: the sign is skipped that frame; expected 0), [5] last refusal fault,
 * [6] commands dropped for lack of list room (expected 0), [7] FRT ticks of the last build, [8] most FRT ticks of a build,
 * [9] texture bank VRAM address (19 bits), [10] texture bank bytes, [11] low RAM start, [12] low RAM end,
 * [13] frames with the timer in 1..460, [14] first timer drawn, [15] last timer drawn. */
extern volatile uint32_t fzx_cdn_report[16];

/* Race load: the pack from the disc into low RAM (metadata and work, from LOW_BASE) and VDP1 VRAM (the texture bank just below CEILING,
 * an offset in the texture partition, and not below FLOOR_MIN, the end of what is already placed). Returns the bank's offset (the new
 * ceiling), or 0 with the status saying why (the race then runs without the sign). *LOW_END is the first free low RAM byte after the
 * block. */
uint32_t fzx_cdn_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, uintptr_t low_base, uintptr_t *low_end,
                      void *scratch, uint32_t scratch_bytes, uint32_t floor_min);
/* Per frame, in the master's idle time after the view is built: builds the sign's commands for the body's intro timer and returns
 * their count (0: hidden, not loaded, or refused); fzx_cdn_commands() is where they are. */
unsigned fzx_cdn_stage(const fzx_view_t *view, const fzx_engine_cam_t *cam, const sh2z_vd_body_t *body);
const vdp1_cmdt_t *fzx_cdn_commands(void);
/* The caller had no list room for N of them. */
void fzx_cdn_dropped(unsigned n);
/* The commands one frame may need, including the user clip in front. */
enum { FZX_CDN_COMMANDS = 30u };
/* Low RAM the block needs, bytes. */
uint32_t fzx_cdn_low_bytes(void);
#endif
