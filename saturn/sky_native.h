#ifndef FZX_SKY_NATIVE_H
#define FZX_SKY_NATIVE_H
#include <yaul.h>
#include "fzx_sky.h"

#define FZX_SKY_MAX_COMMANDS 192u
#define FZX_SKY_VRAM_BYTES (2u*FZX_SKY_MAX_COMMANDS*8u)
extern RefBackdrop fzx_source_backdrop;
extern fzx_sky_stats_t fzx_sky_stats;
extern volatile uint32_t fzx_sky_status,fzx_sky_ticks,fzx_sky_count_max;
void fzx_sky_stack_paint(void);
void fzx_sky_restart(void);
bool fzx_sky_publish(void);
bool fzx_sky_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                    void *shade_bank,uint32_t *count);
#endif
