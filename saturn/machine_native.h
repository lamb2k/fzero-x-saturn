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
/* FZX_SIMPIPE_DBUF: the published-snapshot state travels with the slot. */
int32_t fzx_machine_published_get(void);
void fzx_machine_published_set(int32_t);
#endif
