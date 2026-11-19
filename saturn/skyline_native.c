#include "skyline_native.h"
#ifndef FZX_SKYLINE_CD
#define FZX_SKYLINE_CD 0
#endif
#if FZX_SKYLINE_CD
#include "skyline_identity.h"
#include "texture_cd.h"
extern unsigned char sat_stub_data_end[];
#else
#include "skyline_pixels.h"
#endif
#include <string.h>
_Static_assert(!FZX_SKYLINE_FIXTURE,"Original race skyline requires local source art");
#if !FZX_SKYLINE_CD
_Static_assert(sizeof(fzx_skyline_pixels)==FZX_SKYLINE_VRAM_BYTES,"skyline VRAM span");
#else
_Static_assert(FZX_SKYLINE_PACK_BYTES==FZX_SKYLINE_VRAM_BYTES,"skyline CD span");
#endif
RefSkyline fzx_source_skyline;
fzx_skyline_stats_t fzx_skyline_stats;
volatile uint32_t fzx_skyline_status,fzx_skyline_busy,fzx_skyline_ticks;
static bool ready,published;
static uint32_t published_tick;
bool fzx_skyline_load(void *vram) {
#if FZX_SKYLINE_CD
    const uintptr_t begin=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u;
    if(begin<0x00200000u || begin>=0x00300000u ||
       fzx_texture_cd_scratch_bytes()>0x00300000u-begin)return false;
    return fzx_texture_cd_load("SKYLINE.PAK",FZX_SKYLINE_PACK_BYTES,FZX_SKYLINE_PACK_HASH,
                               vram,(void *)begin,0x00300000u-begin);
#else
    if(!vram || ((uintptr_t)vram&7u))return false;
    volatile uint32_t *out=vram;
    for(unsigned i=0;i<FZX_SKYLINE_VRAM_BYTES/4u;++i) {
        uint32_t value;memcpy(&value,&fzx_skyline_pixels[i*4u],4);out[i]=value;
    }
    for(unsigned i=0;i<FZX_SKYLINE_VRAM_BYTES/4u;++i) {
        uint32_t value;memcpy(&value,&fzx_skyline_pixels[i*4u],4);if(out[i]!=value)return false;
    }
    return true;
#endif
}
void fzx_skyline_restart(void){ready=false;published=false;fzx_skyline_status=0;}
#ifndef FZX_ENGINE /* the engine line fills this state itself (saturn/engine) */
bool fzx_skyline_publish(uint32_t tick) {
    if(published && tick==published_tick)return fzx_skyline_status==1;
    fzx_skyline_busy=1;
    if(!ready){if(ref_skyline_init()){fzx_skyline_status=2;return false;}ready=true;}
    ref_skyline_tick(&fzx_source_skyline,0);
    if(fzx_source_skyline.faults || fzx_source_skyline.count!=REF_SKYLINE_COUNT){fzx_skyline_status=3;return false;}
    published_tick=tick;published=true;fzx_skyline_status=1;fzx_skyline_busy=0;return true;
}
#endif
typedef struct {vdp1_cmdt_t *commands;uintptr_t vram;uint32_t count;} submit_t;
static bool emit(void *context,const fzx_skyline_quad_t *q) {
    submit_t *s=context;vdp1_cmdt_t *cmd=&s->commands[s->count++];
    memset(cmd,0,sizeof(*cmd));vdp1_cmdt_distorted_sprite_set(cmd);
    /* Sega VDP1 bulletin #15: bit 10 enables clipping, bit 9 selects outside.
     * Two-bit field value 2 therefore draws inside the source scissor. */
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.user_clipping_mode=2,
        .color_mode=VDP1_CMDT_CM_CLUT_16,.cc_mode=VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_draw_mode_set(cmd,mode);
    vdp1_cmdt_char_base_set(cmd,(vdp1_vram_t)(s->vram+q->texture*2048u));
    vdp1_cmdt_char_size_set(cmd,64,64);
    cmd->cmd_colr=(uint16_t)(((s->vram+8192u+q->texture*32u)&0x7ffffu)>>3);
    cmd->cmd_xa=q->x[0];cmd->cmd_ya=q->y[0];cmd->cmd_xb=q->x[1];cmd->cmd_yb=q->y[1];
    cmd->cmd_xc=q->x[2];cmd->cmd_yc=q->y[2];cmd->cmd_xd=q->x[3];cmd->cmd_yd=q->y[3];
    return true;
}
bool fzx_skyline_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                        const void *vram,uint32_t *count) {
    if(!commands || !vram || !view || !count || capacity<2 || fzx_skyline_status!=1)return false;
    const uint16_t start=cpu_frt_count_get();fzx_skyline_busy=1;
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];
    commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    submit_t submit={commands,(uintptr_t)vram,1};
    if(!fzx_skyline_draw(view,&fzx_source_skyline,capacity-1,emit,&submit,&fzx_skyline_stats)) {
        fzx_skyline_status=4;return false;
    }
    *count=submit.count;fzx_skyline_ticks=(uint16_t)(cpu_frt_count_get()-start);
    fzx_skyline_busy=0;return true;
}
