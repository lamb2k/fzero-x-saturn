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
#include <stddef.h>
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
/* Native arm placement, to keep high work RAM for the stack. After the pack
 * metadata in low work RAM: FZX_VEHICLE_NATIVE_LOW_TABLES (default 1) puts the
 * per-LOD tables (positions, planes, face records, corner ids, pair table,
 * templates) there, then FZX_VEHICLE_NATIVE_LOW_WORK (default 0, the gate1
 * placement) the per-draw work (projected vertices, lights, palettes, pair
 * scratch). Either at 0 keeps that part in high work RAM. */
#ifndef FZX_VEHICLE_NATIVE_LOW_TABLES
#define FZX_VEHICLE_NATIVE_LOW_TABLES 1
#endif
#ifndef FZX_VEHICLE_NATIVE_LOW_WORK
#define FZX_VEHICLE_NATIVE_LOW_WORK 0
#endif
static fzx_vehicle_native_tables_t *tables;
#if FZX_VEHICLE_NATIVE_LOW_TABLES
#define TABLES_LOW_BYTES ((sizeof(*tables)+31u)&~31u)
#define TABLES_LOW_EXACT sizeof(*tables)
#else
static fzx_vehicle_native_tables_t high_tables __attribute__((aligned(32)));
#define TABLES_LOW_BYTES 0u
#define TABLES_LOW_EXACT 0u
#endif
#if FZX_VEHICLE_NATIVE_LOW_WORK
#define WORK_LOW_BYTES (TABLES_LOW_BYTES+sizeof(*work))
#else
/* Work in high WRAM: the low reservation is exactly the tables, as on gate1. */
static fzx_vehicle_work_t high_work __attribute__((aligned(32)));
#define WORK_LOW_BYTES TABLES_LOW_EXACT
#endif
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

#ifndef FZX_RACER_SPRITES
#define FZX_RACER_SPRITES 0
#endif
#if FZX_RACER_SPRITES
/* FZX_RACER_SPRITES (default 0): draw the race machine as a baked rotation
 * sprite set (RSPRITE.PAK, tools/racer_sprites.py; SH2ZAM rotation-sprites-1)
 * instead of the 3D model. The set takes the machine's own VRAM region (its
 * textures are not loaded), so no other VDP1 allocation moves. The source
 * draw state (ref_machine) and the simulation are unchanged. */
#include "racer_sprites_identity.h"
#include "racer_sprite_native.h"
#include "sh2zam/rotation_sprite_experimental.h"
#ifndef FZX_RACER_SPRITE_DEMO_N
#define FZX_RACER_SPRITE_DEMO_N 0
#endif
_Static_assert(FZX_RACER_SPRITE_BYTES<=FZX_MACHINE_TEXTURE_BYTES+FZX_MACHINE_SHADE_BYTES,
               "racer sprite set must fit the machine VRAM region");
_Static_assert(FZX_RACER_SPRITE_DEMO_N>=0&&FZX_RACER_SPRITE_DEMO_N<=FZX_RACER_SPRITE_MAX_EXTRA,"demo racer count");
static const sh2z_rspr_set_t *racer_set;
static uint32_t racer_vram;
static uint16_t racer_pmod;
volatile uint32_t fzx_racer_sprite_status,fzx_racer_sprite_draws,fzx_racer_sprite_skips,fzx_racer_sprite_cell;
volatile int32_t fzx_racer_sprite_eye_x,fzx_racer_sprite_eye_y,fzx_racer_sprite_eye_z,fzx_racer_sprite_scale;
typedef struct {sh2z_rspr_pick_t pick;sh2z_rspr_place_t place;int32_t depth;} racer_item_t;
static racer_item_t racer_items[FZX_RACER_SPRITE_MAX_EXTRA+1];

static bool racer_item(racer_item_t *it,const fzx_view_t *view,const RefMachine *m,bool player) {
    fzx_vehicle_pose_t pose;
    if(!fzx_vehicle_pose(&pose,view,&fzx_source_camera,m))return false;
    if(player){fzx_racer_sprite_eye_x=pose.eye[0];fzx_racer_sprite_eye_y=pose.eye[1];fzx_racer_sprite_eye_z=pose.eye[2];}
    if(!sh2z_rspr_select(&it->pick,racer_set,pose.eye[0],pose.eye[1],pose.eye[2]))return false;
    if(!sh2z_rspr_place(&it->place,&pose.view,racer_set))return false;
    if(player){fzx_racer_sprite_cell=it->pick.cell|((uint32_t)it->pick.mirror<<16);fzx_racer_sprite_scale=it->place.scale;}
    it->depth=pose.view.t[2];return true;
}

/* Far to near (painter order), one distorted sprite each. */
static unsigned racer_emit(vdp1_cmdt_t *commands,unsigned capacity,unsigned n) {
    for(unsigned i=1;i<n;++i) {
        const racer_item_t key=racer_items[i];unsigned j=i;
        while(j>0&&racer_items[j-1].depth<key.depth){racer_items[j]=racer_items[j-1];--j;}
        racer_items[j]=key;
    }
    unsigned out=0;
    for(unsigned i=0;i<n&&out<capacity;++i) {
        uint16_t words[16];
        if(!sh2z_rspr_emit(words,racer_set,&racer_items[i].pick,&racer_items[i].place,racer_vram,racer_pmod)) {
            ++fzx_racer_sprite_skips;continue;
        }
        memcpy(&commands[out],words,sizeof(words));++out;
    }
    return out;
}

/* Appends items from racer_items[start]; returns the new item count. */
static unsigned racer_bodies(unsigned start,unsigned n,const fzx_view_t *view,const fzx_racer_sprite_body_t *bodies) {
    unsigned items=start;
    for(unsigned i=0;i<n&&items<FZX_RACER_SPRITE_MAX_EXTRA+1;++i) {
        RefMachine m=fzx_source_machine;
        for(unsigned a=0;a<3;++a){m.model_pos[a]=bodies[i].pos[a];for(unsigned b=0;b<3;++b)m.model_basis[a][b]=bodies[i].basis[a][b];}
        if(racer_item(&racer_items[items],view,&m,false))++items;else ++fzx_racer_sprite_skips;
    }
    return items;
}

unsigned fzx_racer_sprites_draw(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                                const fzx_racer_sprite_body_t *bodies,unsigned n) {
    if(!commands||!view||!bodies||fzx_machine_status!=1||!racer_set||n>FZX_RACER_SPRITE_MAX_EXTRA)return 0;
    return racer_emit(commands,capacity,racer_bodies(0,n,view,bodies));
}
#endif

bool fzx_machine_load(void *vram){
    uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u,log=(uintptr_t)fzx_input_log;
    uintptr_t metadata=(fzx_road_tile_low_end+95u)&~(uintptr_t)31u;
    uintptr_t cache=(metadata+FZX_MACHINE_REBAKE_METADATA_BYTES+31u)&~(uintptr_t)31u,end=cache+WORK_LOW_BYTES;
    if(fzx_road_tile_status!=1||metadata<0x00200000u||end+32>0x00300000u||scratch+fzx_texture_cd_scratch_bytes()>log-32){fzx_machine_status=2;return false;}
    fzx_machine_low_start=(uint32_t)metadata;fzx_machine_low_end=(uint32_t)end;
#if FZX_VEHICLE_NATIVE
#if FZX_VEHICLE_NATIVE_LOW_WORK
    work=(fzx_vehicle_work_t *)(cache+TABLES_LOW_BYTES);
#else
    work=&high_work;
#endif
#if FZX_VEHICLE_NATIVE_LOW_TABLES
    tables=(fzx_vehicle_native_tables_t *)cache;
#else
    tables=&high_tables;
#endif
#else
    work=(fzx_vehicle_work_t *)cache;
#endif
    for(unsigned i=0;i<8;++i){((volatile uint32_t *)metadata)[(int)i-8]=0x5ac71e39u;((volatile uint32_t *)end)[i]=0x5ac71e39u;}
    if(!fzx_texture_cd_load("MACHINE.PAK",FZX_MACHINE_REBAKE_METADATA_BYTES,FZX_MACHINE_REBAKE_METADATA_HASH,(void *)metadata,(void *)scratch,log-32-scratch)||
       !fzx_vehicle_pack_open(&pack,(void *)metadata,FZX_MACHINE_REBAKE_METADATA_BYTES,FZX_MACHINE_REBAKE_TEXTURE_BYTES,FZX_MACHINE_REBAKE_METADATA_HASH,false)||
#if FZX_RACER_SPRITES
       ((uintptr_t)vram&7u)||
       !fzx_texture_cd_load("RSPRITE.PAK",FZX_RACER_SPRITE_BYTES,FZX_RACER_SPRITE_HASH,vram,(void *)scratch,log-32-scratch)){fzx_machine_status=3;return false;}
    racer_set=(const sh2z_rspr_set_t *)vram;racer_vram=(uint32_t)((uintptr_t)vram&0x7ffffu);
    if(!sh2z_rspr_valid(racer_set)){fzx_racer_sprite_status=2;fzx_machine_status=3;return false;}
    {
        vdp1_cmdt_t mode_cmd;memset(&mode_cmd,0,sizeof(mode_cmd));
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_CLUT_16};
        vdp1_cmdt_draw_mode_set(&mode_cmd,mode);racer_pmod=mode_cmd.cmd_pmod;
    }
    fzx_racer_sprite_status=1;
#else
       !fzx_texture_cd_load("MACHTX.PAK",FZX_MACHINE_REBAKE_TEXTURE_BYTES,FZX_MACHINE_REBAKE_TEXTURE_HASH,vram,(void *)scratch,log-32-scratch)){fzx_machine_status=3;return false;}
#endif
    memset(work,0,sizeof(*work));
#if FZX_VEHICLE_NATIVE
    memset(tables,0,sizeof(*tables));work->tables=tables;
#endif
    fzx_machine_status=1;return true;
}

#ifndef FZX_MACHINE_PUBLISH
#define FZX_MACHINE_PUBLISH 0
#endif
#if FZX_MACHINE_PUBLISH
/* Sim-pipeline checkpoint 2: orig_publish takes the machine snapshot (after
 * camera 0's flush, before any scene reader); the draw uses it. 1 valid, -1
 * the source refused (the draw then fails as before), 0 none this frame. */
#if FZX_SIMPIPE_DBUF
/* Global so the sim-pipeline part table can name it (checkpoint 3). */
int fzx_machine_published;
#define machine_published fzx_machine_published
#else
static int machine_published;
#endif
bool fzx_machine_publish(void){
    machine_published=0;
    if(fzx_machine_status!=1)return false;
    machine_published=ref_machine(&fzx_source_machine,pack.data+51)?1:-1;
    return true;
}
#endif
typedef struct {vdp1_cmdt_t *commands;uintptr_t pixels,shades;unsigned count;vdp1_cmdt_t texture,solid;} submit_t;
#if !FZX_VEHICLE_NATIVE
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
#endif
bool fzx_machine_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,void *pixels,void *shades,uint32_t *count){
    if(!commands||capacity<2||!view||!pixels||!shades||!count||fzx_machine_status!=1)return false;
    const uint16_t start=cpu_frt_count_get();fzx_machine_busy=1;NATIVE_MARK(0,start);
#if FZX_MACHINE_PUBLISH
    const bool source_ok=machine_published==1;machine_published=0;
#else
    const bool source_ok=ref_machine(&fzx_source_machine,pack.data+51);
#endif
    if(!source_ok){fzx_machine_status=4;fzx_machine_busy=0;return false;}
    NATIVE_MARK(1,cpu_frt_count_get());
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    submit_t s={.commands=commands+1,.pixels=(uintptr_t)pixels,.shades=(uintptr_t)shades};
    const vdp1_cmdt_draw_mode_t texture={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_CLUT_16,.cc_mode=VDP1_CMDT_CC_GOURAUD};
    const vdp1_cmdt_draw_mode_t solid={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,.color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_distorted_sprite_set(&s.texture);vdp1_cmdt_draw_mode_set(&s.texture,texture);
    vdp1_cmdt_polygon_set(&s.solid);vdp1_cmdt_draw_mode_set(&s.solid,solid);
    NATIVE_MARK(2,cpu_frt_count_get());
#if FZX_RACER_SPRITES
    (void)pixels;(void)shades;
    unsigned items=0;
    if(racer_item(&racer_items[0],view,&fzx_source_machine,true))items=1;else ++fzx_racer_sprite_skips;
#if FZX_RACER_SPRITE_DEMO_N
    {
        /* Demo field: copies of the player's state on a grid ahead of it (model z
         * axis = basis row 0 forward, x axis = basis row 2 sideways), spaced in
         * model radii, for the N-racer measurements before rivals exist. */
        static fzx_racer_sprite_body_t demo[FZX_RACER_SPRITE_DEMO_N];
        const float forward=3.0f*FZX_RACER_SPRITE_RADIUS*fzx_source_machine.model_scale[0];
        const float side=2.4f*FZX_RACER_SPRITE_RADIUS*fzx_source_machine.model_scale[2];
        for(unsigned i=0;i<FZX_RACER_SPRITE_DEMO_N;++i) {
            const float row=(float)(i/2u+1u),col=(i&1u)?1.0f:-1.0f;
            for(unsigned a=0;a<3;++a) {
                demo[i].pos[a]=fzx_source_machine.model_pos[a]+fzx_source_machine.model_basis[0][a]*forward*row+
                               fzx_source_machine.model_basis[2][a]*side*col;
                for(unsigned b=0;b<3;++b)demo[i].basis[a][b]=fzx_source_machine.model_basis[a][b];
            }
        }
        items=racer_bodies(items,FZX_RACER_SPRITE_DEMO_N,view,demo);
    }
#endif
    s.count=racer_emit(s.commands,capacity-1,items);fzx_racer_sprite_draws=s.count;
    fzx_machine_stats=(fzx_machine_stats_t){.draws=s.count};
    bool ok=true;
#elif FZX_VEHICLE_NATIVE
    /* Template emit: the core writes the commands and Gouraud words itself. */
    _Static_assert(sizeof(vdp1_cmdt_t)==32&&offsetof(vdp1_cmdt_t,cmd_colr)==6&&offsetof(vdp1_cmdt_t,cmd_xa)==12&&offsetof(vdp1_cmdt_t,cmd_grda)==28,"VDP1 command layout");
    const fzx_vehicle_target_t target={.commands=(uint32_t (*)[8])(void *)s.commands,.capacity=capacity-1,.pixels=pixels,.shades=shades,
        .palette_bytes=FZX_MACHINE_REBAKE_PALETTE_BYTES,.gouraud_bytes=FZX_MACHINE_REBAKE_GOURAUD_BYTES,
        .texture_ctrl=s.texture.cmd_ctrl,.texture_pmod=s.texture.cmd_pmod,.solid_ctrl=s.solid.cmd_ctrl,.solid_pmod=s.solid.cmd_pmod};
    uint32_t drawn=0;
    bool ok=fzx_vehicle_draw_native(&pack,view,&fzx_source_camera,&fzx_source_machine,work,&target,&drawn,&fzx_vehicle_stats);
    s.count=drawn;
#else
    bool ok=fzx_vehicle_draw(&pack,view,&fzx_source_camera,&fzx_source_machine,work,capacity-1,emit,&s,&fzx_vehicle_stats);
#endif
#if !FZX_RACER_SPRITES
    fzx_machine_stats=(fzx_machine_stats_t){.lod=fzx_vehicle_stats.lod,.draws=fzx_vehicle_stats.draws,.culled=fzx_vehicle_stats.culled,
        .clipped=fzx_vehicle_stats.clipped,.saturated=fzx_vehicle_stats.saturated,.fault=fzx_vehicle_stats.fault,
        .positions=fzx_vehicle_stats.positions,.normals=fzx_vehicle_stats.normals};
#endif
    if(!ok){fzx_machine_status=5;fzx_machine_busy=0;return false;}
    NATIVE_MARK(3,cpu_frt_count_get());
#if !FZX_RACER_SPRITES
    /* The sprite set occupies this region when FZX_RACER_SPRITES is on. */
    for(unsigned i=0;i<pack.palettes;++i)if(work->palette_dirty[i])memcpy((uint8_t *)shades+i*32u,work->palette[i],32);
#endif
    NATIVE_MARK(4,cpu_frt_count_get());
    *count=s.count+1;fzx_machine_ticks=(uint16_t)(cpu_frt_count_get()-start);fzx_machine_busy=0;
#if FZX_VEHICLE_SUBPROFILE
    fzx_vehicle_audit[0]=*count;
    fzx_vehicle_audit[1]=audit_fnv(2166136261u,(const volatile uint8_t *)commands,(unsigned)(*count*sizeof(*commands)));
    fzx_vehicle_audit[2]=audit_fnv(2166136261u,(const volatile uint8_t *)shades,FZX_MACHINE_SHADE_BYTES);
#endif
    return true;
}
