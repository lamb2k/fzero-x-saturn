#ifndef FZX_COUNTDOWN_DRAW_H
#define FZX_COUNTDOWN_DRAW_H
#include "fzx_countdown_pack.h"

/* At most 22 intact textured quads plus the black panel's clipped fan. */
enum {FZX_COUNTDOWN_DRAW_COMMANDS=FZX_COUNTDOWN_QUADS-1+(SH2Z_VIEW_GUARD_MAX_OUT-1)/2};
typedef struct {
    uint32_t texture;
    uint16_t width,height;
    int16_t x[4],y[4];
    uint16_t quad,solid,color,reserved;
} fzx_countdown_command_t;

enum {
    FZX_COUNTDOWN_DRAW_INVALID=1,FZX_COUNTDOWN_DRAW_PROJECT,
    FZX_COUNTDOWN_DRAW_CLIP,FZX_COUNTDOWN_DRAW_TEXTURE_CLIP,
    FZX_COUNTDOWN_DRAW_CAPACITY
};
typedef struct {
    uint32_t commands,quads,culled,guard_checks,clipped_solids,fault,error_quad;
    fzx_countdown_phase_t phase;
} fzx_countdown_draw_stats_t;
typedef struct {
    const fzx_countdown_pack_t *pack;
    sh2z_pos16_t positions[FZX_COUNTDOWN_VERTICES];
    sh2z_view_vertex_t projected[FZX_COUNTDOWN_VERTICES];
    sh2z_view_guard_vertex_t input[4],clipped[SH2Z_VIEW_GUARD_MAX_OUT];
    bool prepared;
} fzx_countdown_draw_work_t;

/* Boot-only copy of admitted original positions. Caller retains the immutable
 * admitted pack; textures must have passed the pack's separate upload identity
 * check. No allocation, global scratch or native dispatch is introduced. */
bool fzx_countdown_draw_prepare(fzx_countdown_draw_work_t *,const fzx_countdown_pack_t *);

/* Source order, back-face culling and full chart corner order are preserved.
 * The native caller must emit the source view's user clip and use transparent
 * RGB1555 texture mode, with no end codes and no added lighting. Solid colour
 * is RGB1555 black, required by the original pre-countdown panel.
 * Safe textured quads retain the complete chart and rely on user clipping.
 * A visible texture requiring near/far or signed-coordinate clipping fails;
 * arbitrary clipped UVs cannot be represented by one VDP1 rectangle.
 * count is zero on any failure; partial output must then be discarded.
 * Work/output must be disjoint from input state and immutable pack storage. */
bool fzx_countdown_draw(fzx_countdown_draw_work_t *,const fzx_view_t *,
    const RefCountdown *,fzx_countdown_command_t *,uint32_t capacity,
    uint32_t *count,fzx_countdown_draw_stats_t *);
#endif
