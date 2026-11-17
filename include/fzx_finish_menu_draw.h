#ifndef FZX_FINISH_MENU_DRAW_H
#define FZX_FINISH_MENU_DRAW_H
#include "fzx_finish_menu.h"
#define FZX_FINISH_MENU_DRAW_MAX 16u
/* Original 320x240 half-open destination and clip coordinates. Images use the
 * generated finish-menu bank IDs. Glyph w/h are source extents; x2/y2 retain
 * the source's truncated scaled destination. Crop y by eight for Saturn. */
typedef struct {
    fzx_results_draw_t draws[FZX_FINISH_MENU_DRAW_MAX];
    uint32_t tick,frame;
    uint16_t count;
} fzx_finish_menu_draw_snapshot_t;
/* Read-only capture: never advances menu state, input, RNG or audio requests. */
bool fzx_finish_menu_draw_capture(const fzx_finish_menu_snapshot_t *,fzx_finish_menu_draw_snapshot_t *);
#endif
