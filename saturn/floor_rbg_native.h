#ifndef FZX_FLOOR_RBG_NATIVE_H
#define FZX_FLOOR_RBG_NATIVE_H
#include <yaul.h>
#include "fzx_floor_rbg.h"
extern fzx_floor_rbg_t fzx_floor_rbg;
extern volatile uint32_t fzx_floor_status,fzx_floor_busy,fzx_floor_ticks;
extern volatile uint32_t fzx_floor_pending,fzx_floor_presented,fzx_floor_commits;
bool fzx_floor_load(void);
bool fzx_floor_native(vdp1_cmdt_t *,unsigned,const fzx_view_t *,const RefBackdrop *,uint32_t *);
/* Install in the SDK's variable-interval render callback: completed VDP1 image
 * and rotation data must become visible at the same frame-buffer change. */
void fzx_floor_present(void *);
#endif
