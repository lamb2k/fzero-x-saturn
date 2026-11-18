#ifndef FZX_CLOUDS_RBG_H
#define FZX_CLOUDS_RBG_H
#include "fzx_floor_rbg.h"

/* FZX_CLOUDS_VDP2=1: the source cloud plane (backdrop vertices 12..19) drawn by
 * VDP2 RBG0 rotation parameter B instead of VDP1 tiles. Parameter B maps the
 * level-0 cloud tile through the same reciprocal coefficient table as the floor;
 * the window is the projected cloud quad. Fade follows the source alpha ramp
 * (255 = invisible) as VDP1 colour-calculation bands 1..7 over RBG0, exactly as
 * the floor fog does: band 7 is opaque sky. Presentation adaptation only. */
typedef struct {
    fzx_floor_rp_t rp;
    uint16_t window[224][2];
    uint32_t fault,visible_lines,degenerate,commands,inner_alpha,active,hash;
} fzx_clouds_rbg_t;

bool fzx_clouds_rbg_prepare(const fzx_view_t *,const RefBackdrop *,
                            fzx_clouds_rbg_t *,fzx_floor_emit_fn,void *);
#endif
