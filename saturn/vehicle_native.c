#include "machine_native.h"
#include "fzx_vehicle.h"
#include "vehicle_identity.h"
#if FZX_SOURCE_ROAD_STREAM
#include "road_stream_native.h"
#define fzx_road_tile_low_end fzx_road_stream_low_end
#define fzx_road_tile_status fzx_road_stream_status
#else
#include "road_tiles_native.h"
#endif
#include "texture_cd.h"
#include <string.h>
#ifndef FZX_VEHICLE_FAST
#define FZX_VEHICLE_FAST 0
#endif

extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
extern RefCamera fzx_source_camera;
volatile uint32_t fzx_machine_status,fzx_machine_busy,fzx_machine_ticks,fzx_machine_fields;
volatile uint32_t fzx_machine_low_start,fzx_machine_low_end;
RefMachine fzx_source_machine;
fzx_machine_stats_t fzx_machine_stats;
fzx_vehicle_stats_t fzx_vehicle_stats;
static fzx_vehicle_pack_t pack;
static fzx_vehicle_work_t *work;
#ifndef FZX_VEHICLE_NATIVE
#define FZX_VEHICLE_NATIVE 0
#endif
#if FZX_VEHICLE_NATIVE
/* Native arm: the per-draw work (projected vertices, lights, native face tables,
 * pair scratch) lives in high work RAM; only the pack metadata stays low. */
static fzx_vehicle_work_t high_work __attribute__((aligned(32)));
#define WORK_LOW_BYTES 0u
#else
#define WORK_LOW_BYTES sizeof(*work)
#endif
#if FZX_VEHICLE_PROFILE
volatile uint32_t fzx_vehicle_phase_ticks[6];
void fzx_vehicle_profile_mark(unsigned phase){if(phase<6)fzx_vehicle_phase_ticks[phase]=cpu_frt_count_get();}
#endif
#if FZX_VEHICLE_SUBPROFILE
/* Sub-phase accumulators (see fzx_vehicle_draw.c) and native wrapper marks:
 * start, after ref_machine, after command setup, after draw, after palette upload. */
volatile uint32_t fzx_vehicle_sub_ticks[7],fzx_vehicle_sub_counts[5],fzx_vehicle_native_marks[5];
/* Audit, computed after the timed region: FNV-1a of this frame's emitted VDP1
 * commands (user clip included) and of the whole palette+Gouraud shade table. */
volatile uint32_t fzx_vehicle_audit[3];
static uint32_t audit_fnv(uint32_t h,const volatile uint8_t *p,unsigned n){for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
uint32_t fzx_vehicle_sub_now(void){return cpu_frt_count_get();}
#define NATIVE_MARK(n,v) (fzx_vehicle_native_marks[n]=(v))
#else
#define NATIVE_MARK(n,v) ((void)0)
#endif

bool fzx_machine_load(void *vram){
    uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u,log=(uintptr_t)fzx_input_log;
    uintptr_t metadata=(fzx_road_tile_low_end+95u)&~(uintptr_t)31u;
    uintptr_t cache=(metadata+FZX_MACHINE_REBAKE_METADATA_BYTES+31u)&~(uintptr_t)31u,end=cache+WORK_LOW_BYTES;
    if(fzx_road_tile_status!=1||metadata<0x00200000u||end+32>0x00300000u||scratch+fzx_texture_cd_scratch_bytes()>log-32){fzx_machine_status=2;return false;}
    fzx_machine_low_start=(uint32_t)metadata;fzx_machine_low_end=(uint32_t)end;
#if FZX_VEHICLE_NATIVE
    work=&high_work;
#else
    work=(fzx_vehicle_work_t *)cache;
#endif
    for(unsigned i=0;i<8;++i){((volatile uint32_t *)metadata)[(int)i-8]=0x5ac71e39u;((volatile uint32_t *)end)[i]=0x5ac71e39u;}
    if(!fzx_texture_cd_load("MACHINE.PAK",FZX_MACHINE_REBAKE_METADATA_BYTES,FZX_MACHINE_REBAKE_METADATA_HASH,(void *)metadata,(void *)scratch,log-32-scratch)||
       !fzx_vehicle_pack_open(&pack,(void *)metadata,FZX_MACHINE_REBAKE_METADATA_BYTES,FZX_MACHINE_REBAKE_TEXTURE_BYTES,FZX_MACHINE_REBAKE_METADATA_HASH,false)||
       !fzx_texture_cd_load("MACHTX.PAK",FZX_MACHINE_REBAKE_TEXTURE_BYTES,FZX_MACHINE_REBAKE_TEXTURE_HASH,vram,(void *)scratch,log-32-scratch)){fzx_machine_status=3;return false;}
    memset(work,0,sizeof(*work));fzx_machine_status=1;return true;
}

typedef struct {vdp1_cmdt_t *commands;uintptr_t pixels,shades;unsigned count;vdp1_cmdt_t texture,solid;} submit_t;
static bool emit(void *context,const fzx_vehicle_quad_t *q){
    submit_t *s=context;vdp1_cmdt_t *cmd=s->commands+s->count;
    if(q->solid){*cmd=s->solid;const rgb1555_t color={.raw=q->color};vdp1_cmdt_color_set(cmd,color);}
    else {
        if(q->face*8u+8>FZX_MACHINE_REBAKE_GOURAUD_BYTES||q->palette*32u+32>FZX_MACHINE_REBAKE_PALETTE_BYTES)return false;
        *cmd=s->texture;
        vdp1_cmdt_char_base_set(cmd,(vdp1_vram_t)(s->pixels+q->texture));vdp1_cmdt_char_size_set(cmd,q->width,q->height);
        const uintptr_t palette=s->shades+q->palette*32u,gouraud=s->shades+FZX_MACHINE_REBAKE_PALETTE_BYTES+q->face*8u;
#if FZX_VEHICLE_FAST
        /* Same eight bytes as memcpy: the shade table and quad are 4-byte aligned. */
        if(!(gouraud&3u)){uint32_t words[2];memcpy(words,q->gouraud,8);
            ((volatile uint32_t *)gouraud)[0]=words[0];((volatile uint32_t *)gouraud)[1]=words[1];}
        else
#endif
        memcpy((void *)gouraud,q->gouraud,8);cmd->cmd_colr=(uint16_t)((palette&0x7ffffu)>>3);cmd->cmd_grda=(uint16_t)((gouraud&0x7ffffu)>>3);
    }
    cmd->cmd_xa=q->x[0];cmd->cmd_ya=q->y[0];cmd->cmd_xb=q->x[1];cmd->cmd_yb=q->y[1];
    cmd->cmd_xc=q->x[2];cmd->cmd_yc=q->y[2];cmd->cmd_xd=q->x[3];cmd->cmd_yd=q->y[3];++s->count;return true;
}
bool fzx_machine_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,void *pixels,void *shades,uint32_t *count){
    if(!commands||capacity<2||!view||!pixels||!shades||!count||fzx_machine_status!=1)return false;
    const uint16_t start=cpu_frt_count_get();fzx_machine_busy=1;NATIVE_MARK(0,start);
    if(!ref_machine(&fzx_source_machine,pack.data+51)){fzx_machine_status=4;fzx_machine_busy=0;return false;}
    NATIVE_MARK(1,cpu_frt_count_get());
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    submit_t s={.commands=commands+1,.pixels=(uintptr_t)pixels,.shades=(uintptr_t)shades};
    const vdp1_cmdt_draw_mode_t texture={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_CLUT_16,.cc_mode=VDP1_CMDT_CC_GOURAUD};
    const vdp1_cmdt_draw_mode_t solid={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_distorted_sprite_set(&s.texture);vdp1_cmdt_draw_mode_set(&s.texture,texture);
    vdp1_cmdt_polygon_set(&s.solid);vdp1_cmdt_draw_mode_set(&s.solid,solid);
    NATIVE_MARK(2,cpu_frt_count_get());
    bool ok=fzx_vehicle_draw(&pack,view,&fzx_source_camera,&fzx_source_machine,work,capacity-1,emit,&s,&fzx_vehicle_stats);
    fzx_machine_stats=(fzx_machine_stats_t){.lod=fzx_vehicle_stats.lod,.draws=fzx_vehicle_stats.draws,.culled=fzx_vehicle_stats.culled,
        .clipped=fzx_vehicle_stats.clipped,.saturated=fzx_vehicle_stats.saturated,.fault=fzx_vehicle_stats.fault,
        .positions=fzx_vehicle_stats.positions,.normals=fzx_vehicle_stats.normals};
    if(!ok){fzx_machine_status=5;fzx_machine_busy=0;return false;}
    NATIVE_MARK(3,cpu_frt_count_get());
    for(unsigned i=0;i<pack.palettes;++i)if(work->palette_dirty[i])memcpy((uint8_t *)shades+i*32u,work->palette[i],32);
    NATIVE_MARK(4,cpu_frt_count_get());
    *count=s.count+1;fzx_machine_ticks=(uint16_t)(cpu_frt_count_get()-start);fzx_machine_busy=0;
#if FZX_VEHICLE_SUBPROFILE
    fzx_vehicle_audit[0]=*count;
    fzx_vehicle_audit[1]=audit_fnv(2166136261u,(const volatile uint8_t *)commands,(unsigned)(*count*sizeof(*commands)));
    fzx_vehicle_audit[2]=audit_fnv(2166136261u,(const volatile uint8_t *)shades,FZX_MACHINE_SHADE_BYTES);
#endif
    return true;
}
