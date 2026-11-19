#include "planes_native.h"
#include "texture_cd.h"
#include "planes_identity.h"
#include <string.h>
#include "mem_low.h"
/* The unchanged combined identity header predates the compact-pack tag. */
#ifndef FZX_PLANE_CLOUD_ONLY_PACK
#define FZX_PLANE_CLOUD_ONLY_PACK 0
#endif
_Static_assert(!FZX_PLANE_FIXTURE && FZX_PLANE_EXPECTED_BYTES==FZX_PLANE_RESIDENT_BYTES,"original plane bank required");
_Static_assert(FZX_PLANE_CLOUD_ONLY_PACK==FZX_PLANE_CLOUD_ONLY,"plane pack mode mismatch");
extern unsigned char sat_stub_data_end[];
fzx_plane_stats_t fzx_plane_stats;
volatile uint32_t fzx_plane_status,fzx_plane_busy,fzx_plane_ticks;
volatile uint32_t fzx_plane_fields;
bool fzx_planes_load(void *vram) {
    uintptr_t start=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u;
    if(start<0x00200000u || start>=FZX_LOW_SCRATCH_TOP || fzx_texture_cd_scratch_bytes()>FZX_LOW_SCRATCH_TOP-start)return false;
    bool ok=fzx_texture_cd_load("PLANES.PAK",FZX_PLANE_RESIDENT_BYTES,FZX_PLANE_HASH,vram,(void *)start,FZX_LOW_SCRATCH_TOP-start);
    fzx_plane_status=ok?1:2;return ok;
}
typedef struct {vdp1_cmdt_t *cmd;uintptr_t pixels;uint16_t *shades;unsigned count;} submit_t;
static bool emit(void *context,const fzx_plane_quad_t *q) {
#if FZX_PLANE_CLOUD_ONLY
    /* Shared geometry and hashes retain combined offsets. Rebase only here. */
    if(!q || q->layer!=1 || q->level>=FZX_PLANE_LEVELS || q->variant>=FZX_PLANE_VARIANTS ||
       q->width!=(64u>>q->level) || q->height!=(32u>>q->level) ||
       q->offset<FZX_PLANE_LAYER_BYTES || (q->offset&7u))return false;
    const uint32_t offset=q->offset-FZX_PLANE_LAYER_BYTES;
    const uint32_t bytes=(uint32_t)q->width*q->height*2u;
    if(offset>FZX_PLANE_RESIDENT_BYTES-bytes)return false;
#endif
    submit_t *s=context;vdp1_cmdt_t *cmd=s->cmd+s->count;
    memset(cmd,0,sizeof(*cmd));vdp1_cmdt_distorted_sprite_set(cmd);
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.user_clipping_mode=2,
        .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=q->layer?VDP1_CMDT_CC_REPLACE:VDP1_CMDT_CC_GOURAUD};
    vdp1_cmdt_draw_mode_set(cmd,mode);
#if FZX_PLANE_CLOUD_ONLY
    vdp1_cmdt_char_base_set(cmd,(vdp1_vram_t)(s->pixels+offset));
#else
    vdp1_cmdt_char_base_set(cmd,(vdp1_vram_t)(s->pixels+q->offset));
#endif
    vdp1_cmdt_char_size_set(cmd,q->width,q->height);
    cmd->cmd_xa=q->x[0];cmd->cmd_ya=q->y[0];cmd->cmd_xb=q->x[1];cmd->cmd_yb=q->y[1];
    cmd->cmd_xc=q->x[2];cmd->cmd_yc=q->y[2];cmd->cmd_xd=q->x[3];cmd->cmd_yd=q->y[3];
    uint16_t *shade=s->shades+s->count*4;memcpy(shade,q->shade,8);
    cmd->cmd_grda=(uint16_t)(((uintptr_t)shade&0x7ffffu)>>3);++s->count;return true;
}
bool fzx_planes_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,void *vram,void *shades,uint32_t *count) {
    if(!commands || capacity<2 || !view || !source || !vram || !shades || !count)return false;
    uint16_t start=cpu_frt_count_get();fzx_plane_busy=1;
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    submit_t submit={commands+1,(uintptr_t)vram,shades,0};
    if(capacity-1>FZX_PLANE_MAX_DRAWS)capacity=FZX_PLANE_MAX_DRAWS+1;
    if(!fzx_planes_draw(view,source,fzx_plane_fog,fzx_plane_mean,capacity-1,emit,&submit,&fzx_plane_stats)) {
        fzx_plane_status=3;return false;
    }
    *count=submit.count+1;fzx_plane_ticks=(uint16_t)(cpu_frt_count_get()-start);
    fzx_plane_status=1;fzx_plane_busy=0;return true;
}
