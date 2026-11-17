#include "countdown_draw_native.h"

bool fzx_countdown_draw_native_prepare(fzx_countdown_native_work_t *w,const fzx_countdown_pack_t *p){
    return w&&fzx_countdown_draw_prepare(&w->draw,p);
}
bool fzx_countdown_draw_native(fzx_countdown_native_work_t *w,const fzx_view_t *view,
    const RefCountdown *state,const fzx_countdown_phase_t *phase,void *vram,uint32_t vram_bytes,
    vdp1_cmdt_t *commands,uint32_t capacity,uint32_t *count,fzx_countdown_native_stats_t *stats){
    if(count)*count=0;
    if(!stats)return false;
    *stats=(fzx_countdown_native_stats_t){0};stats->fault=FZX_COUNTDOWN_NATIVE_INVALID;
    if(!count||!w||!w->draw.prepared||!w->draw.pack||!view||!state||!phase||!commands)return false;
    fzx_countdown_phase_t expected;stats->fault=FZX_COUNTDOWN_NATIVE_PHASE;
    if(!fzx_countdown_phase(state->timer,&expected)||expected.visible!=phase->visible||
       expected.mouth_open!=phase->mouth_open||expected.panel!=phase->panel)return false;
    const fzx_countdown_pack_t *pack=w->draw.pack;
    const uintptr_t base=(uintptr_t)vram,offset=base&0x7ffffu;
    stats->fault=FZX_COUNTDOWN_NATIVE_VRAM;
    if(!base||(base&7u)||!pack->texture_bytes||pack->texture_bytes>vram_bytes||
       vram_bytes>0x80000u-offset||base>UINTPTR_MAX-vram_bytes)return false;
    uint32_t prepared=0;stats->fault=FZX_COUNTDOWN_NATIVE_BUILD;
    if(!fzx_countdown_draw(&w->draw,view,state,w->pending,FZX_COUNTDOWN_DRAW_COMMANDS,&prepared,&stats->draw))return false;
    if(!prepared){stats->fault=0;return true;}
    stats->fault=FZX_COUNTDOWN_NATIVE_CAPACITY;
    if(!capacity||prepared>capacity-1)return false;
    /* Preflight the entire batch before modifying any native command. */
    stats->fault=FZX_COUNTDOWN_NATIVE_COMMAND;
    for(uint32_t i=0;i<prepared;++i){
        const fzx_countdown_command_t *q=w->pending+i;fzx_countdown_image_t image;
        if(!fzx_countdown_image(pack,q->quad,phase,&image)||q->reserved||q->color!=0x8000||
           q->solid!=(unsigned)image.solid||q->texture!=image.texture||q->width!=image.width||q->height!=image.height||
           (i&&q->quad<w->pending[i-1].quad))return false;
        for(unsigned k=0;k<4;++k)if(q->x[k]<-1024||q->x[k]>1023||q->y[k]<-1024||q->y[k]>1023)return false;
        const uint32_t bytes=(uint32_t)q->width*q->height*2u;
        if(!q->width||(q->width&7u)||q->width>504||!q->height||q->height>255||
           (q->texture&7u)||q->texture>pack->texture_bytes||bytes>pack->texture_bytes-q->texture)return false;
    }
    vdp1_cmdt_t texture={0},solid={0},clip={0};
    const vdp1_cmdt_draw_mode_t texture_mode={.end_code_disable=1,.trans_pixel_disable=0,
        .user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    const vdp1_cmdt_draw_mode_t solid_mode={.end_code_disable=1,.trans_pixel_disable=1,
        .user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_distorted_sprite_set(&texture);vdp1_cmdt_draw_mode_set(&texture,texture_mode);
    vdp1_cmdt_polygon_set(&solid);vdp1_cmdt_draw_mode_set(&solid,solid_mode);
    const rgb1555_t black={.raw=0x8000};vdp1_cmdt_color_set(&solid,black);
    vdp1_cmdt_user_clip_coord_set(&clip);
    clip.cmd_xa=view->clip[0];clip.cmd_ya=view->clip[1];
    clip.cmd_xc=(int16_t)(view->clip[2]-1);clip.cmd_yc=(int16_t)(view->clip[3]-1);commands[0]=clip;
    for(uint32_t i=0;i<prepared;++i){
        const fzx_countdown_command_t *q=w->pending+i;vdp1_cmdt_t *out=commands+i+1;
        *out=q->solid?solid:texture;
        if(!q->solid){vdp1_cmdt_char_base_set(out,(vdp1_vram_t)(base+q->texture));vdp1_cmdt_char_size_set(out,q->width,q->height);}
        out->cmd_xa=q->x[0];out->cmd_ya=q->y[0];out->cmd_xb=q->x[1];out->cmd_yb=q->y[1];
        out->cmd_xc=q->x[2];out->cmd_yc=q->y[2];out->cmd_xd=q->x[3];out->cmd_yd=q->y[3];
    }
    *count=prepared+1;stats->commands=*count;stats->fault=0;return true;
}
