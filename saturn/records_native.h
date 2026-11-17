#ifndef FZX_RECORDS_NATIVE_H
#define FZX_RECORDS_NATIVE_H
#include "results_native.h"
#include "fzx_records_draw.h"

/* Uses 192 independently tinted CLUTs; RGB sprites need no palette slot. */
#define FZX_RECORDS_PALETTE_BYTES 6144u
#define FZX_RECORDS_COMMANDS (2u*FZX_RECORDS_DRAW_MAX+8u)
bool fzx_records_native(vdp1_cmdt_t *,unsigned,const fzx_records_draw_snapshot_t *,
    const void *pixels,void *palettes,const void *tints,unsigned tint_level,
    const void *previews,const uint16_t selected[6],unsigned *count);
bool fzx_records_model(unsigned character,uint32_t rgb,unsigned *index,
    unsigned *offset,unsigned *bytes);
bool fzx_records_model_camera(void);
#endif
