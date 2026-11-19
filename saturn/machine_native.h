#ifndef FZX_MACHINE_NATIVE_H
#define FZX_MACHINE_NATIVE_H
#include <yaul.h>
#include "fzx_machine_draw.h"
extern volatile uint32_t fzx_machine_status,fzx_machine_busy,fzx_machine_ticks,fzx_machine_fields;
extern volatile uint32_t fzx_machine_low_start,fzx_machine_low_end;
extern RefMachine fzx_source_machine;
extern fzx_machine_stats_t fzx_machine_stats;
bool fzx_machine_load(void *);
bool fzx_machine_native(vdp1_cmdt_t *,unsigned,const fzx_view_t *,void *,void *,uint32_t *);
/* FZX_MACHINE_PUBLISH=1: publication takes the machine snapshot the next draw uses. */
bool fzx_machine_publish(void);
/* FZX_SIMPIPE_DBUF: the published-snapshot flag travels with the slot. */
extern int fzx_machine_published;
/* FZX_VDP1_DBANK: the draw alternates between two shade banks (palettes +
 * Gouraud). Palettes are uploaded only when changed, so each bank tracks its
 * own staleness; register both banks once before the first draw. */
void fzx_machine_dbank(void *bank_a,void *bank_b);
#endif
