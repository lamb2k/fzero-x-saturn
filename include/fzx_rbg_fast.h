#ifndef FZX_RBG_FAST_H
#define FZX_RBG_FAST_H
/* FZX_RBG_FAST=1 (default 0): cheaper per-frame preparation of the floor and
 * cloud rotation layers.
 *  - src/fzx_view.c: clipping, plane tests and projection with 32x32 products
 *    and the SH-2 divider where they fit; the same results.
 *  - fzx_rbg_window (src/fzx_rbg_fast.c, one copy for both layers): the line
 *    window walks each polygon edge once with a 16.16 step instead of
 *    intersecting every edge on every line. It keeps each line's running
 *    minimum and maximum pixel in the output window itself (no extra RAM).
 *    The step's rounding can move an edge pixel only where the exact crossing
 *    lies within about 0.004 px of a pixel boundary.
 * Disabled, nothing here is compiled. */
#include <stdint.h>
#include "fzx_view.h"
/* window[line] = {2*lo,2*hi} as the per-line original, {640,0} where empty. */
void fzx_rbg_window(const fzx_view_t *v,const int32_t xy[10][2],unsigned count,
    uint16_t window[224][2],uint32_t *visible_lines);
#endif
