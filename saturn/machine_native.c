#include "machine_native.h"
#include "road_tiles_native.h"
#include "texture_cd.h"
#include "machine_identity.h"
#include <string.h>
extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
volatile uint32_t fzx_machine_status,fzx_machine_busy,fzx_machine_ticks,fzx_machine_fields;
volatile uint32_t fzx_machine_low_start,fzx_machine_low_end;
RefMachine fzx_source_machine;
fzx_machine_stats_t fzx_machine_stats;
static fzx_machine_pack_t pack;
static fzx_machine_work_t *work;
bool fzx_machine_load(void *vram){
    uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u,log=(uintptr_t)fzx_input_log;
    uintptr_t metadata=(fzx_road_tile_low_end+95u)&~(uintptr_t)31u;
    uintptr_t cache=(metadata+FZX_MACHINE_METADATA_BYTES+31u)&~(uintptr_t)31u,end=cache+sizeof(*work);
    if(fzx_road_tile_status!=1||metadata<0x00200000u||end+32>0x00300000u||scratch+fzx_texture_cd_scratch_bytes()>log-32){fzx_machine_status=2;return false;}
    fzx_machine_low_start=(uint32_t)metadata;fzx_machine_low_end=(uint32_t)end;work=(fzx_machine_work_t *)cache;
    for(unsigned i=0;i<8;++i){((volatile uint32_t *)metadata)[(int)i-8]=0x5ac71e39u;((volatile uint32_t *)end)[i]=0x5ac71e39u;}
    if(!fzx_texture_cd_load("MACHINE.PAK",FZX_MACHINE_METADATA_BYTES,FZX_MACHINE_METADATA_HASH,(void *)metadata,(void *)scratch,log-32-scratch)||
       !fzx_machine_pack_open(&pack,(void *)metadata,FZX_MACHINE_METADATA_BYTES,FZX_MACHINE_TEXTURE_BYTES,FZX_MACHINE_METADATA_HASH,false)||
       !fzx_texture_cd_load("MACHTX.PAK",FZX_MACHINE_TEXTURE_BYTES,FZX_MACHINE_TEXTURE_HASH,vram,(void *)scratch,log-32-scratch)) {fzx_machine_status=3;return false;}
    memset(work,0,sizeof(*work));fzx_machine_status=1;return true;
}
typedef struct {vdp1_cmdt_t *cmd;uintptr_t pixels,shades;unsigned count;} submit_t;
static bool emit(void *context,const fzx_machine_quad_t *q){
    submit_t *s=context;vdp1_cmdt_t *cmd=s->cmd+s->count;memset(cmd,0,sizeof(*cmd));
    if(q->solid){
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
        vdp1_cmdt_polygon_set(cmd);vdp1_cmdt_draw_mode_set(cmd,mode);const rgb1555_t color={.raw=q->color};vdp1_cmdt_color_set(cmd,color);
    }else{
        if((unsigned)q->triangle*40+40>FZX_MACHINE_SHADE_BYTES)return false;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_CLUT_16,.cc_mode=VDP1_CMDT_CC_GOURAUD};
        vdp1_cmdt_distorted_sprite_set(cmd);vdp1_cmdt_draw_mode_set(cmd,mode);
        vdp1_cmdt_char_base_set(cmd,(vdp1_vram_t)(s->pixels+q->offset));vdp1_cmdt_char_size_set(cmd,q->width,q->height);
        uintptr_t palette=s->shades+q->triangle*40u,gouraud=palette+32;
        memcpy((void *)palette,q->light.palette,32);memcpy((void *)gouraud,q->light.gouraud,8);
        cmd->cmd_colr=(uint16_t)((palette&0x7ffffu)>>3);cmd->cmd_grda=(uint16_t)((gouraud&0x7ffffu)>>3);
    }
    cmd->cmd_xa=q->x[0];cmd->cmd_ya=q->y[0];cmd->cmd_xb=q->x[1];cmd->cmd_yb=q->y[1];
    cmd->cmd_xc=q->x[2];cmd->cmd_yc=q->y[2];cmd->cmd_xd=q->x[3];cmd->cmd_yd=q->y[3];++s->count;return true;
}
bool fzx_machine_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,void *pixels,void *shades,uint32_t *count){
    if(!commands||capacity<2||!view||!pixels||!shades||!count||fzx_machine_status!=1)return false;
    uint16_t start=cpu_frt_count_get();fzx_machine_busy=1;
    if(!ref_machine(&fzx_source_machine,fzx_machine_pack_fog(&pack))){fzx_machine_status=4;fzx_machine_busy=0;return false;}
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    submit_t s={commands+1,(uintptr_t)pixels,(uintptr_t)shades,0};
    if(!fzx_machine_draw(&pack,view,&fzx_source_machine,work,capacity-1,emit,&s,&fzx_machine_stats)){fzx_machine_status=5;fzx_machine_busy=0;return false;}
    *count=s.count+1;fzx_machine_ticks=(uint16_t)(cpu_frt_count_get()-start);fzx_machine_busy=0;return true;
}
