#ifndef FZX_ROAD_PROFILE_H
#define FZX_ROAD_PROFILE_H
#include <stdint.h>
#ifndef FZX_ROAD_TILE_PROFILE
#define FZX_ROAD_TILE_PROFILE 0
#endif
enum {FZX_RP_BASE,FZX_RP_TRANSFORM,FZX_RP_PROJECT,FZX_RP_CULL,FZX_RP_CLIP,
      FZX_RP_SPLIT,FZX_RP_MATERIAL,FZX_RP_EMIT,FZX_RP_HASH,FZX_RP_COUNT};
#if FZX_ROAD_TILE_PROFILE
/* Exclusive CPU intervals, not GPU time. Native consumers must compare total
 * with the independent video-field interval: a 16-bit clock alone cannot
 * detect a complete wrap between two samples. Single master CPU only. */
typedef struct {
    uint32_t magic,total,transitions,max_interval,phase,depth,faults,active;
    uint32_t ticks[FZX_RP_COUNT],calls[FZX_RP_COUNT];
} fzx_road_profile_t;
extern volatile fzx_road_profile_t fzx_road_profile;
uint16_t fzx_road_profile_clock(void);
void fzx_road_profile_begin(void);
void fzx_road_profile_end(void);
unsigned fzx_road_profile_enter(unsigned);
void fzx_road_profile_leave(unsigned);
#define RP_CALL(id,expr) __extension__ ({unsigned rp_old=fzx_road_profile_enter(id); \
    __typeof__(expr) rp_result=(expr);fzx_road_profile_leave(rp_old);rp_result;})
#define RP_BEGIN(id,name) unsigned name=fzx_road_profile_enter(id)
#define RP_END(name) fzx_road_profile_leave(name)
#else
#define RP_CALL(id,expr) (expr)
#define RP_BEGIN(id,name) ((void)0)
#define RP_END(name) ((void)0)
#endif
#endif
