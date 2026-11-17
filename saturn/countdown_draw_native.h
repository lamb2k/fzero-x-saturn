#ifndef FZX_COUNTDOWN_DRAW_NATIVE_H
#define FZX_COUNTDOWN_DRAW_NATIVE_H
#include <yaul.h>
#include "fzx_countdown_draw.h"

enum {
    FZX_COUNTDOWN_NATIVE_COMMANDS=FZX_COUNTDOWN_DRAW_COMMANDS+1,
    FZX_COUNTDOWN_NATIVE_INVALID=1,FZX_COUNTDOWN_NATIVE_PHASE,
    FZX_COUNTDOWN_NATIVE_VRAM,FZX_COUNTDOWN_NATIVE_BUILD,
    FZX_COUNTDOWN_NATIVE_CAPACITY,FZX_COUNTDOWN_NATIVE_COMMAND
};
typedef struct {
    fzx_countdown_draw_work_t draw;
    fzx_countdown_command_t pending[FZX_COUNTDOWN_DRAW_COMMANDS];
} fzx_countdown_native_work_t;
typedef struct {
    fzx_countdown_draw_stats_t draw;
    uint32_t commands,fault;
} fzx_countdown_native_stats_t;

/* Caller owns this work and the admitted immutable pack for its lifetime.
 * No allocation, file loading, upload, synchronization or global state. */
bool fzx_countdown_draw_native_prepare(fzx_countdown_native_work_t *,const fzx_countdown_pack_t *);

/* phase must match this exact source snapshot. vram points to the previously
 * verified texture bank in VDP1 VRAM, with vram_bytes bytes reserved for it.
 * The caller proves residency/ownership; this function checks alignment,
 * supplied span, 19-bit VDP1 addressability and every selected chart extent.
 *
 * Writes one user clip followed by the prepared native commands, no END or
 * link command. Empty/hidden signs write nothing. Capacity includes that
 * user clip; FZX_COUNTDOWN_NATIVE_COMMANDS (30) covers the worst case.
 * Textures retain RGB1555 transparency, disabled end codes, whole chart
 * orientation and replace colour mode. Only the original black panel uses
 * solid polygons. No Gouraud table or per-primitive callback is involved.
 *
 * count is zero and native commands are untouched on every failure. Work
 * may change. Work, native commands, count and stats must be mutually disjoint
 * and must not overlap any immutable input.
 * Texture-clipping failures are retained in stats.draw, never approximated. */
bool fzx_countdown_draw_native(fzx_countdown_native_work_t *,const fzx_view_t *,
    const RefCountdown *,const fzx_countdown_phase_t *,void *vram,uint32_t vram_bytes,
    vdp1_cmdt_t *,uint32_t capacity,uint32_t *count,fzx_countdown_native_stats_t *);
#endif
