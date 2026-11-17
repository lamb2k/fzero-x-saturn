#ifndef FZX_RESULTS_NATIVE_H
#define FZX_RESULTS_NATIVE_H
#include <yaul.h>
#include "fzx_results_draw.h"
#include "fzx_finish_draw.h"
#define FZX_RESULTS_PALETTE_BYTES (FZX_RESULTS_DRAW_MAX*32u)
#define FZX_RESULTS_UI_COMMANDS (FZX_RESULTS_DRAW_MAX*2u)
#define FZX_FINISH_COMMANDS (FZX_FINISH_TRIANGLES+1u)

/* Caller waits for the previous draw before changing palette VRAM. These
 * functions append no END command and never submit an incomplete list. */
bool fzx_results_ui_native(vdp1_cmdt_t *,unsigned capacity,const fzx_results_snapshot_t *,
                           const void *pixels,void *palettes,unsigned palette_bytes,
                           const void *tint_pixels,unsigned tint_level,unsigned *count);
bool fzx_finish_native(vdp1_cmdt_t *,unsigned capacity,const fzx_finish_pack_t *,
                       const sh2z_view_t *,fzx_finish_work_t *,const void *textures,unsigned *count);
#endif
