#ifndef FZX_FINISH_MENU_NATIVE_H
#define FZX_FINISH_MENU_NATIVE_H
#include <yaul.h>
#include "fzx_finish_menu_draw.h"
#define FZX_FINISH_MENU_COMMANDS (FZX_FINISH_MENU_DRAW_MAX*2u)
#define FZX_FINISH_MENU_CLUT_BYTES (FZX_FINISH_MENU_DRAW_MAX*32u)
typedef struct {
    uint16_t words[3][32];
    uint32_t tick,frame;
    uint8_t used;
} fzx_finish_menu_palette_t;
/* After previous VDP1 completion. No END, submission or CRAM writes occur.
 * Publish the palette at the same VBlank as these commands' framebuffer.
 * Failure publishes neither count nor palette; discard partial commands. */
bool fzx_finish_menu_native(vdp1_cmdt_t *,unsigned,
    const fzx_finish_menu_draw_snapshot_t *,const void *pixels,void *cluts,
    fzx_finish_menu_palette_t *,unsigned *count);
/* Caller owns VBlank/presentation sequencing and verifies the integrated
 * CRAM-mode1/type0/zero-offset register profile before calling. */
bool fzx_finish_menu_palette_upload(const fzx_finish_menu_palette_t *,
    volatile uint16_t *cram,unsigned words);
#endif
