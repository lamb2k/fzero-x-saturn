#include "road_tiles_native.h"
#include "road_native.h"
#include "fzx_road_profile.h"
#include "texture_cd.h"
#include "road_tile_identity.h"
#include <string.h>
extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
volatile uint32_t fzx_road_tile_status,fzx_road_tile_busy,fzx_road_tile_ticks,fzx_road_tile_fields;
volatile uint32_t fzx_road_tile_low_start,fzx_road_tile_low_end;
fzx_road_tile_stats_t fzx_road_tile_stats;
#if FZX_ROAD_TILE_VERTEX_CACHE
volatile uint32_t fzx_road_tile_cache_start,fzx_road_tile_cache_bytes;
volatile uint32_t fzx_road_tile_cache_hits,fzx_road_tile_cache_misses;
#endif
#if FZX_ROAD_TILE_PROFILE
uint16_t fzx_road_profile_clock(void){return cpu_frt_count_get();}
#endif
static fzx_road_tiles_t tiles;
bool fzx_road_tiles_load(void *vram) {
    uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u;
    uintptr_t log=(uintptr_t)fzx_input_log,metadata=(log+12288u*4u+63u)&~(uintptr_t)31u;
    uintptr_t end=metadata+FZX_ROAD_TILE_METADATA_BYTES;
#if FZX_ROAD_TILE_VERTEX_CACHE
    uintptr_t cache=(end+63u)&~(uintptr_t)31u;
    uint32_t cache_bytes=fzx_road_tiles_cache_bytes(fzx_native_road.count[FZX_ROAD_VERTEX]);
    if(!cache_bytes || cache_bytes>0x00300000u || cache>0x00300000u-cache_bytes){fzx_road_tile_status=2;return false;}
    end=cache+cache_bytes;
#endif
    if(log!=((scratch+4096u+63u)&~(uintptr_t)63u) || scratch<0x00200000u ||
       fzx_texture_cd_scratch_bytes()>log-32u-scratch || end+32u>0x00300000u){fzx_road_tile_status=2;return false;}
    fzx_road_tile_low_start=(uint32_t)log;fzx_road_tile_low_end=(uint32_t)end;
    for(unsigned i=0;i<8;++i){((volatile uint32_t *)log)[(int)i-8]=0x51a7c36du;((volatile uint32_t *)end)[i]=0x51a7c36du;fzx_input_log[12288u+i]=0x51a7c36du;}
#if FZX_ROAD_TILE_VERTEX_CACHE
    fzx_road_tile_cache_start=(uint32_t)cache;fzx_road_tile_cache_bytes=cache_bytes;
    for(unsigned i=0;i<8;++i)((volatile uint32_t *)cache)[(int)i-8]=0x51a7c36du;
#endif
    if(!fzx_texture_cd_load("ROADTL.PAK",FZX_ROAD_TILE_METADATA_BYTES,FZX_ROAD_TILE_METADATA_HASH,(void *)metadata,(void *)scratch,log-32u-scratch) ||
       !fzx_road_tiles_open(&tiles,&fzx_native_road,(void *)metadata,FZX_ROAD_TILE_METADATA_BYTES,FZX_ROAD_TILE_TEXTURE_BYTES,FZX_ROAD_TILE_METADATA_HASH) ||
       !fzx_texture_cd_load("ROADTX.PAK",FZX_ROAD_TILE_TEXTURE_BYTES,FZX_ROAD_TILE_TEXTURE_HASH,vram,(void *)scratch,log-32u-scratch)) {
        fzx_road_tile_status=3;return false;
    }
#if FZX_ROAD_TILE_VERTEX_CACHE
    if(!fzx_road_tiles_cache_bind(&tiles,(void *)cache,cache_bytes)){fzx_road_tile_status=2;return false;}
#endif
    fzx_road_tile_status=1;return true;
}
typedef struct {vdp1_cmdt_t *cmd;uintptr_t pixels;uint16_t *shades;unsigned count;} submit_t;
static bool emit(void *context,const fzx_road_tile_quad_t *q) {
    submit_t *s=context;vdp1_cmdt_t *cmd=s->cmd+s->count;memset(cmd,0,sizeof(*cmd));
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_GOURAUD};
    if(q->solid){vdp1_cmdt_polygon_set(cmd);const rgb1555_t base={.raw=0xc210};vdp1_cmdt_color_set(cmd,base);}
    else {vdp1_cmdt_distorted_sprite_set(cmd);vdp1_cmdt_char_base_set(cmd,(vdp1_vram_t)(s->pixels+q->offset));vdp1_cmdt_char_size_set(cmd,q->width,q->height);}
    vdp1_cmdt_draw_mode_set(cmd,mode);
    cmd->cmd_xa=q->x[0];cmd->cmd_ya=q->y[0];cmd->cmd_xb=q->x[1];cmd->cmd_yb=q->y[1];
    cmd->cmd_xc=q->x[2];cmd->cmd_yc=q->y[2];cmd->cmd_xd=q->x[3];cmd->cmd_yd=q->y[3];
    uint16_t *shades=s->shades+s->count*4;memcpy(shades,q->shade,8);cmd->cmd_grda=(uint16_t)(((uintptr_t)shades&0x7ffffu)>>3);
    ++s->count;return true;
}
bool fzx_road_tiles_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,const RefRoadPlan *plan,
                           int32_t pa,int32_t pb,void *pixels,void *shades,uint32_t *count) {
    if(!commands || capacity<2 || !view || !plan || !pixels || !shades || !count)return false;
    uint16_t start=cpu_frt_count_get();fzx_road_tile_busy=1;
#if FZX_ROAD_TILE_PROFILE
    fzx_road_profile_begin();
#endif
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    submit_t s={commands+1,(uintptr_t)pixels,shades,0};if(capacity-1>FZX_ROAD_TILE_MAX_DRAWS)capacity=FZX_ROAD_TILE_MAX_DRAWS+1;
#if FZX_ROAD_TILE_PROFILE
    bool ok=fzx_road_tiles_draw(&tiles,view,plan,pa,pb,capacity-1,emit,&s,&fzx_road_tile_stats);
    fzx_road_profile_end();
    if(!ok)
#else
    if(!fzx_road_tiles_draw(&tiles,view,plan,pa,pb,capacity-1,emit,&s,&fzx_road_tile_stats))
#endif
    {fzx_road_tile_status=4;fzx_road_tile_busy=0;return false;}
#if FZX_ROAD_TILE_VERTEX_CACHE
    fzx_road_tile_cache_hits=tiles.cache->hits;fzx_road_tile_cache_misses=tiles.cache->misses;
#endif
    *count=s.count+1;fzx_road_tile_ticks=(uint16_t)(cpu_frt_count_get()-start);fzx_road_tile_busy=0;fzx_road_tile_status=1;return true;
}
