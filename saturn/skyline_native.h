#ifndef FZX_SKYLINE_NATIVE_H
#define FZX_SKYLINE_NATIVE_H
#include <yaul.h>
#include "fzx_skyline.h"
#define FZX_SKYLINE_VRAM_BYTES 8320u
extern RefSkyline fzx_source_skyline;
extern fzx_skyline_stats_t fzx_skyline_stats;
extern volatile uint32_t fzx_skyline_status,fzx_skyline_busy,fzx_skyline_ticks;
bool fzx_skyline_load(void *vram);
void fzx_skyline_restart(void);
bool fzx_skyline_publish(uint32_t tick);
bool fzx_skyline_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                        const void *vram,uint32_t *count);
#endif
