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
#ifndef ENG_BAL_SPLIT
#define ENG_BAL_SPLIT 0
#endif
#if ENG_BAL_SPLIT
/* ENG_BAL_SPLIT (engine, claude/speed-2): the same prepare in parts that may run on different SH-2s. SETUP is everything
 * up to the fade bands (parameter B, window, the inner band), HALF0 and HALF1 the fade bands of the two sides of the
 * quad, in that order. The parts' output together equals fzx_clouds_rbg_prepare's. SETUP stores what the bands need in
 * the split record; HALF1 reads it (and writes only the counters of its own out and its emit's context). */
enum { FZX_CLOUDS_SETUP=1, FZX_CLOUDS_HALF0=2, FZX_CLOUDS_HALF1=4, FZX_CLOUDS_ALL=7 };
typedef struct { int32_t positions[8][3]; int32_t inner; uint32_t bands; } fzx_clouds_split_t;
bool fzx_clouds_rbg_prepare_part(const fzx_view_t *,const RefBackdrop *,
                                 fzx_clouds_rbg_t *,fzx_floor_emit_fn,void *,unsigned parts,fzx_clouds_split_t *);
#endif
#endif
