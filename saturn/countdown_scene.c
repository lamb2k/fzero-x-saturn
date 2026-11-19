#include "countdown_scene.h"
#include "texture_cd.h"
#include "countdown_identity.h"
#include <string.h>

extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
volatile uint32_t fzx_countdown_status,fzx_countdown_busy;
volatile uint32_t fzx_countdown_ticks,fzx_countdown_fields;
volatile uint32_t fzx_countdown_low_start,fzx_countdown_low_end;
volatile uint32_t fzx_countdown_texture_start,fzx_countdown_texture_bytes;
volatile uint32_t fzx_countdown_command_start;
RefCountdown fzx_source_countdown;
fzx_countdown_native_stats_t fzx_countdown_stats;

typedef struct {
    fzx_countdown_pack_t pack;
    fzx_countdown_native_work_t work;
    fzx_countdown_phase_t phase;
    void *vram;
    uint32_t reserved_bytes;
    bool published;
} scene_t;
static scene_t *scene;
_Static_assert(sizeof(RefCountdown)==72,"countdown snapshot ABI");
_Static_assert(sizeof(fzx_countdown_native_stats_t)==40,"countdown stats ABI");

static bool fail(uint32_t status){
    fzx_countdown_status=status;fzx_countdown_busy=0;
    fzx_countdown_command_start=0;return false;
}
bool fzx_countdown_guards(void){
    if(!scene||fzx_countdown_low_start<0x00200020u||
       fzx_countdown_low_end>0x002fffe0u||fzx_countdown_low_start>=fzx_countdown_low_end)return false;
    const volatile uint32_t *before=(const volatile uint32_t *)(uintptr_t)(fzx_countdown_low_start-32u);
    const volatile uint32_t *after=(const volatile uint32_t *)(uintptr_t)fzx_countdown_low_end;
    for(unsigned i=0;i<8;++i)
        if(before[i]!=FZX_COUNTDOWN_SCENE_GUARD_WORD||after[i]!=FZX_COUNTDOWN_SCENE_GUARD_WORD)return false;
    return true;
}
bool fzx_countdown_load(uint32_t low_end,void *vram,uint32_t reserved_bytes){
    scene=NULL;fzx_countdown_status=FZX_COUNTDOWN_SCENE_UNLOADED;fzx_countdown_busy=1;
    fzx_countdown_ticks=0;fzx_countdown_fields=0;fzx_countdown_low_start=0;fzx_countdown_low_end=0;
    fzx_countdown_texture_start=0;fzx_countdown_texture_bytes=0;fzx_countdown_command_start=0;
    memset(&fzx_source_countdown,0,sizeof(fzx_source_countdown));
    memset(&fzx_countdown_stats,0,sizeof(fzx_countdown_stats));
    const uintptr_t raw=(uintptr_t)sat_stub_data_end,log=(uintptr_t)fzx_input_log;
    const uintptr_t bank=(uintptr_t)vram,offset=bank&0x7ffffu;
    if(low_end<0x00200000u||low_end>0x002fffa0u||raw>UINTPTR_MAX-31u||log<32u||
       !bank||(bank&7u)||reserved_bytes<FZX_COUNTDOWN_TEXTURE_BYTES||
       reserved_bytes>0x80000u-offset||bank>UINTPTR_MAX-reserved_bytes)return fail(FZX_COUNTDOWN_SCENE_RANGE);
    const uintptr_t scratch=(raw+31u)&~(uintptr_t)31u;
    if(scratch>log-32u||log-32u-scratch>UINT32_MAX||
       fzx_texture_cd_scratch_bytes()>log-32u-scratch)return fail(FZX_COUNTDOWN_SCENE_RANGE);
    const uintptr_t metadata=((uintptr_t)low_end+95u)&~(uintptr_t)31u;
    const uintptr_t context=(metadata+FZX_COUNTDOWN_BYTES+31u)&~(uintptr_t)31u;
    const uintptr_t end=(context+sizeof(scene_t)+31u)&~(uintptr_t)31u;
    if(end>0x002fffe0u)return fail(FZX_COUNTDOWN_SCENE_RANGE);
    fzx_countdown_low_start=(uint32_t)metadata;fzx_countdown_low_end=(uint32_t)end;
    scene=(scene_t *)context;memset(scene,0,sizeof(*scene));
    volatile uint32_t *before=(volatile uint32_t *)(metadata-32u),*after=(volatile uint32_t *)end;
    for(unsigned i=0;i<8;++i)before[i]=after[i]=FZX_COUNTDOWN_SCENE_GUARD_WORD;
    const uint32_t scratch_bytes=(uint32_t)(log-32u-scratch);
    bool okay=fzx_texture_cd_load("COUNTDN.PAK",FZX_COUNTDOWN_BYTES,FZX_COUNTDOWN_HASH,
        (void *)metadata,(void *)scratch,scratch_bytes);
    if(!fzx_countdown_guards())return fail(FZX_COUNTDOWN_SCENE_GUARD);
    if(!okay)return fail(FZX_COUNTDOWN_SCENE_METADATA);
    okay=fzx_countdown_pack_open(&scene->pack,(void *)metadata,FZX_COUNTDOWN_BYTES,
        FZX_COUNTDOWN_HASH,FZX_COUNTDOWN_TEXTURE_BYTES,FZX_COUNTDOWN_TEXTURE_HASH);
    if(!fzx_countdown_guards())return fail(FZX_COUNTDOWN_SCENE_GUARD);
    if(!okay)return fail(FZX_COUNTDOWN_SCENE_PACK);
    okay=fzx_texture_cd_load("COUNTDTX.PAK",FZX_COUNTDOWN_TEXTURE_BYTES,FZX_COUNTDOWN_TEXTURE_HASH,
        vram,(void *)scratch,scratch_bytes);
    if(!fzx_countdown_guards())return fail(FZX_COUNTDOWN_SCENE_GUARD);
    if(!okay)return fail(FZX_COUNTDOWN_SCENE_TEXTURE);
    okay=fzx_countdown_draw_native_prepare(&scene->work,&scene->pack);
    if(!fzx_countdown_guards())return fail(FZX_COUNTDOWN_SCENE_GUARD);
    if(!okay)return fail(FZX_COUNTDOWN_SCENE_PREPARE);
    scene->vram=vram;scene->reserved_bytes=reserved_bytes;
    fzx_countdown_texture_start=(uint32_t)bank;fzx_countdown_texture_bytes=FZX_COUNTDOWN_TEXTURE_BYTES;
    fzx_countdown_status=FZX_COUNTDOWN_SCENE_READY;fzx_countdown_busy=0;return true;
}
bool fzx_countdown_publish(void){
    if(fzx_countdown_status!=FZX_COUNTDOWN_SCENE_READY)return false;
    fzx_countdown_busy=1;
    if(!fzx_countdown_guards())return fail(FZX_COUNTDOWN_SCENE_GUARD);
    scene->published=false;
    const bool okay=ref_countdown(&fzx_source_countdown)&&!fzx_source_countdown.faults&&
        fzx_countdown_phase(fzx_source_countdown.timer,&scene->phase);
    if(!fzx_countdown_guards())return fail(FZX_COUNTDOWN_SCENE_GUARD);
    if(!okay)return fail(FZX_COUNTDOWN_SCENE_PUBLISH);
    scene->published=true;fzx_countdown_busy=0;return true;
}
#if FZX_SIMPIPE_DBUF
/* Sim-pipeline checkpoint 3: the publication's phase and flag travel with the slot. */
void fzx_countdown_publication_save(fzx_countdown_phase_t *phase,uint32_t *published){
    if(!scene){*published=0;return;}*phase=scene->phase;*published=scene->published;}
void fzx_countdown_publication_load(const fzx_countdown_phase_t *phase,uint32_t published){
    if(scene){scene->phase=*phase;scene->published=published!=0;}}
#endif
bool fzx_countdown_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,uint32_t *count){
    if(count)*count=0;
    fzx_countdown_command_start=0;memset(&fzx_countdown_stats,0,sizeof(fzx_countdown_stats));
    if(fzx_countdown_status!=FZX_COUNTDOWN_SCENE_READY)return false;
    if(!commands||!view||!count)return fail(FZX_COUNTDOWN_SCENE_ARGUMENT);
    const uint16_t start=cpu_frt_count_get();fzx_countdown_busy=1;
    if(!fzx_countdown_guards())return fail(FZX_COUNTDOWN_SCENE_GUARD);
    if(!scene->published)return fail(FZX_COUNTDOWN_SCENE_PUBLISH);
    const bool okay=fzx_countdown_draw_native(&scene->work,view,&fzx_source_countdown,&scene->phase,
        scene->vram,scene->reserved_bytes,commands,capacity,count,&fzx_countdown_stats);
    const bool guards=fzx_countdown_guards();
    fzx_countdown_ticks=(uint16_t)(cpu_frt_count_get()-start);
    if(!guards||!okay){*count=0;return fail(guards?FZX_COUNTDOWN_SCENE_DRAW:FZX_COUNTDOWN_SCENE_GUARD);}
    if(*count)fzx_countdown_command_start=(uint32_t)(uintptr_t)commands;
    fzx_countdown_busy=0;return true;
}
