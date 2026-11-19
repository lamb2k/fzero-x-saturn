#include "results_scene.h"
#include "fzx_results_bridge.h"
#include "results_identity.h"
#include "finish_identity.h"
#include "countdown_identity.h"
#include "texture_cd.h"
#include "race_workspace_native.h"
#include "countdown_scene.h"
#include <string.h>

extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
volatile uint32_t fzx_results_status,fzx_results_busy;
volatile uint32_t fzx_results_low_start,fzx_results_low_end;
volatile uint32_t fzx_results_cart_start,fzx_results_cart_end;
volatile uint32_t fzx_results_command_start,fzx_results_command_count;
volatile uint32_t fzx_results_vram_start,fzx_results_vram_bytes;
volatile uint32_t fzx_results_tint_level,fzx_results_texture_borrowed;
fzx_results_snapshot_t *fzx_source_results;
fzx_results_input_t fzx_source_results_input;

enum { GUARD=0x7f39a6c2u,GUARD_BYTES=32u };
typedef struct {
    fzx_results_state_t state;
    fzx_results_snapshot_t snapshot;
    fzx_finish_pack_t finish;
    fzx_finish_work_t work;
    const uint8_t *ui,*textures,*tints;
    uint8_t *vram;
    uint32_t scratch,scratch_bytes;
    uint32_t frame;
    unsigned reserved;
    bool published;
} scene_t;
static scene_t *scene;

static bool fail(unsigned status) {
    fzx_results_status=status;fzx_results_busy=0;fzx_results_command_count=0;
    fzx_results_command_start=0;return false;
}
static void guard_write(uintptr_t begin,uintptr_t end) {
    volatile uint32_t *before=(volatile uint32_t *)(begin-GUARD_BYTES),*after=(volatile uint32_t *)end;
    for(unsigned i=0;i<8;++i)before[i]=after[i]=GUARD;
}
static bool guard_check(uintptr_t begin,uintptr_t end) {
    const volatile uint32_t *before=(const volatile uint32_t *)(begin-GUARD_BYTES),*after=(const volatile uint32_t *)end;
    for(unsigned i=0;i<8;++i)if(before[i]!=GUARD||after[i]!=GUARD)return false;
    return true;
}
bool fzx_results_guards(void) {
    return scene&&fzx_results_low_start>=0x00200020u&&fzx_results_low_end<=0x002fffe0u&&
        fzx_results_low_start<fzx_results_low_end&&fzx_results_cart_start<fzx_results_cart_end&&
        guard_check(fzx_results_low_start,fzx_results_low_end)&&
        guard_check(fzx_results_cart_start,fzx_results_cart_end)&&fzx_race_workspace_guards();
}
static bool load(const char *name,uint32_t bytes,uint32_t hash,void *out) {
    const bool okay=fzx_texture_cd_load(name,bytes,hash,out,(void *)(uintptr_t)scene->scratch,scene->scratch_bytes);
    return fzx_results_guards()&&okay;
}
bool fzx_results_load(uint32_t low_end,uint32_t cart_end,void *vram,unsigned bytes) {
    scene=NULL;fzx_source_results=NULL;fzx_results_status=FZX_RESULTS_UNLOADED;fzx_results_busy=1;
    fzx_results_command_start=0;fzx_results_command_count=0;fzx_results_texture_borrowed=0;
    fzx_results_tint_level=UINT32_MAX;
    const uintptr_t base=(uintptr_t)dram_cart_area_get(),cart_limit=base+dram_cart_size_get();
    const uintptr_t meta=((uintptr_t)low_end+95u)&~(uintptr_t)31u;
    const uintptr_t context=(meta+FZX_FINISH_BYTES+31u)&~(uintptr_t)31u;
    const uintptr_t end=(context+sizeof(scene_t)+31u)&~(uintptr_t)31u;
    const uintptr_t bank=((uintptr_t)cart_end+95u)&~(uintptr_t)31u;
    const uintptr_t bank_end=(bank+FZX_RESULTS_UI_BYTES+FZX_FINISH_TEXTURE_BYTES+FZX_RESULTS_TINT_BYTES+31u)&~(uintptr_t)31u;
    const uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u;
    const uintptr_t log=(uintptr_t)fzx_input_log;
    const unsigned required=FZX_RESULTS_UI_BYTES+FZX_FINISH_TEXTURE_BYTES+FZX_RESULTS_PALETTE_BYTES+FZX_RESULTS_TINT_BANK_BYTES;
    const uintptr_t pixels=(uintptr_t)vram;
    if(low_end<0x00200000u||low_end>0x002fffa0u||end>0x002fffe0u||
       dram_cart_id_get()!=DRAM_CART_ID_4MIB||cart_end<base||cart_end>cart_limit-GUARD_BYTES||
       bank_end>cart_limit-GUARD_BYTES||!fzx_race_workspace_guards()||
       log<scratch+GUARD_BYTES||fzx_texture_cd_scratch_bytes()>log-scratch-GUARD_BYTES||
       pixels<(uintptr_t)VDP1_VRAM(0)||pixels>=(uintptr_t)VDP1_VRAM(0)+0x80000u||(pixels&7u)||
       required>bytes||FZX_COUNTDOWN_TEXTURE_BYTES>bytes||bytes>0x80000u-(pixels&0x7ffffu))return fail(FZX_RESULTS_RANGE);
    fzx_results_low_start=(uint32_t)meta;fzx_results_low_end=(uint32_t)end;
    fzx_results_cart_start=(uint32_t)bank;fzx_results_cart_end=(uint32_t)bank_end;
    guard_write(meta,end);guard_write(bank,bank_end);
    scene=(scene_t *)context;memset(scene,0,sizeof(*scene));
    scene->scratch=(uint32_t)scratch;scene->scratch_bytes=(uint32_t)(log-scratch-GUARD_BYTES);
    scene->ui=(const uint8_t *)bank;scene->textures=scene->ui+FZX_RESULTS_UI_BYTES;
    scene->tints=scene->textures+FZX_FINISH_TEXTURE_BYTES;scene->vram=vram;scene->reserved=bytes;
    if(!load("FINISH.PAK",FZX_FINISH_BYTES,FZX_FINISH_HASH,(void *)meta)||
       !load("FINISHTX.PAK",FZX_FINISH_TEXTURE_BYTES,FZX_FINISH_TEXTURE_HASH,(void *)scene->textures)||
       !load("RESULTUI.PAK",FZX_RESULTS_UI_BYTES,FZX_RESULTS_UI_HASH,(void *)scene->ui)||
       !load("RESTINT.PAK",FZX_RESULTS_TINT_BYTES,FZX_RESULTS_TINT_HASH,(void *)scene->tints))return fail(FZX_RESULTS_ASSET);
    if(!fzx_finish_pack_open(&scene->finish,(void *)meta,FZX_FINISH_BYTES,FZX_FINISH_HASH,
                            FZX_FINISH_TEXTURE_BYTES,FZX_FINISH_TEXTURE_HASH)||
       !fzx_finish_texture_check(&scene->finish,scene->textures,FZX_FINISH_TEXTURE_BYTES)||
       !fzx_finish_prepare(&scene->finish,&scene->work))return fail(FZX_RESULTS_ASSET);
    fzx_results_config_t config;
    if(!fzx_results_default_config(&config)||!fzx_results_reset(&scene->state,&config,NULL))return fail(FZX_RESULTS_STATE);
    fzx_source_results=&scene->snapshot;fzx_results_vram_start=(uint32_t)pixels;fzx_results_vram_bytes=required;
    if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);
#if FZX_SOURCE_RECORDS
    if(!fzx_records_load(fzx_results_low_end,fzx_results_cart_end,vram,bytes,scene->tints))return fail(FZX_RESULTS_ASSET);
#endif
    fzx_results_status=FZX_RESULTS_READY;fzx_results_busy=0;return true;
}
bool fzx_results_tick(bool connected) {
    if(fzx_results_status!=FZX_RESULTS_READY)return false;
    fzx_results_busy=1;
    if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);
    /* The diagnostic input log saturates at 12,288 entries. Animation must
     * follow every source tick, including a longer race and terminal state. */
    if(scene->frame==UINT32_MAX||
       !ref_results_input(&fzx_source_results_input,scene->frame+1u,connected)||
       !fzx_results_step(&scene->state,&fzx_source_results_input)||
       !fzx_results_snapshot(&scene->state,&scene->snapshot))return fail(FZX_RESULTS_STATE);
#if FZX_SOURCE_RECORDS
    if(!fzx_records_tick(&scene->state,scene->frame+1u,connected))return fail(FZX_RESULTS_STATE);
#endif
    if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);
    ++scene->frame;scene->published=true;fzx_results_busy=0;return true;
}
#if FZX_SIM_PIPELINE
/* Sim-pipeline checkpoint 4 (C-035 item 2): would fzx_results_native draw
 * anything for the state as it stands? Read-only. The slave publishes this
 * after its ticks so the master never reads live results state while the
 * slave advances it; a pipelined frame requires it to be false. */
bool fzx_results_draw_pending(void) {
    if(fzx_results_status!=FZX_RESULTS_READY||!scene)return false;
#if FZX_SOURCE_RECORDS
    if(fzx_records_active)return true;
#endif
    return scene->published&&(scene->snapshot.finish.visible||scene->snapshot.count);
}
#endif
bool fzx_results_restart(void) {
    if(fzx_results_status!=FZX_RESULTS_READY)return false;
    if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);
    fzx_results_busy=1;
    if(fzx_results_texture_borrowed&&
       !load("COUNTDTX.PAK",FZX_COUNTDOWN_TEXTURE_BYTES,FZX_COUNTDOWN_TEXTURE_HASH,scene->vram))return fail(FZX_RESULTS_ASSET);
#if FZX_SOURCE_RECORDS
    if(!fzx_records_restart())return fail(FZX_RESULTS_STATE);
#endif
    /* reset() handles aliased prior records with an internal copy. */
    if(!fzx_results_reset(&scene->state,&scene->state.config,&scene->state.records)||
       !fzx_results_snapshot(&scene->state,&scene->snapshot))return fail(FZX_RESULTS_STATE);
    scene->frame=0;scene->published=false;fzx_results_texture_borrowed=0;fzx_results_tint_level=UINT32_MAX;
    fzx_results_command_start=0;fzx_results_command_count=0;
    if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);
    fzx_results_busy=0;return true;
}
bool fzx_results_native(vdp1_cmdt_t *commands,unsigned capacity,unsigned *count) {
    if(count)*count=0;fzx_results_command_start=0;fzx_results_command_count=0;
    if(!count||!commands||fzx_results_status!=FZX_RESULTS_READY)return false;
    if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);
#if FZX_SOURCE_RECORDS
    if(fzx_records_active) {
        if(!fzx_records_scene_draw(commands,capacity,count))return fail(FZX_RESULTS_DRAW);
        fzx_results_texture_borrowed=1;
        return true;
    }
#endif
    if(!scene->published||(!scene->snapshot.finish.visible&&!scene->snapshot.count))return true;
    fzx_results_busy=1;
    if(fzx_source_countdown.timer)return fail(FZX_RESULTS_STATE);
    if(!fzx_results_texture_borrowed) {
        memcpy(scene->vram,scene->ui,FZX_RESULTS_UI_BYTES);
        memcpy(scene->vram+FZX_RESULTS_UI_BYTES,scene->textures,FZX_FINISH_TEXTURE_BYTES);
        fzx_results_texture_borrowed=1;
    }
    unsigned level=15;
    for(unsigned i=0;i<scene->snapshot.count;++i) {
        const fzx_results_draw_t *d=&scene->snapshot.draws[i];
        if(d->kind==FZX_RESULTS_IMAGE&&d->image<8&&fzx_results_tint_offsets[d->image]!=UINT32_MAX&&
           d->r==255&&d->g==d->b&&d->g!=255){level=(d->g*15u+127u)/255u;break;}
    }
    uint8_t *const palettes=scene->vram+FZX_RESULTS_UI_BYTES+FZX_FINISH_TEXTURE_BYTES;
    uint8_t *const tints=palettes+FZX_RESULTS_PALETTE_BYTES;
    if(fzx_results_tint_level!=level) {
        memcpy(tints,scene->tints+level*FZX_RESULTS_TINT_BANK_BYTES,FZX_RESULTS_TINT_BANK_BYTES);
        fzx_results_tint_level=level;
    }
    unsigned n=0,ui=0;
    if(scene->snapshot.finish.visible) {
        int32_t matrix[18];sh2z_view_t view;
        if(!fzx_results_finish_matrix(&scene->snapshot,matrix)||!fzx_finish_view(matrix,&view)||
           !fzx_finish_native(commands,capacity,&scene->finish,&view,&scene->work,
                              scene->vram+FZX_RESULTS_UI_BYTES,&n))return fail(FZX_RESULTS_DRAW);
    }
    if(!fzx_results_ui_native(commands+n,capacity-n,&scene->snapshot,scene->vram,palettes,
                              FZX_RESULTS_PALETTE_BYTES,tints,level,&ui))return fail(FZX_RESULTS_DRAW);
    if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);
    *count=n+ui;fzx_results_command_count=*count;
    if(*count)fzx_results_command_start=(uint32_t)(uintptr_t)commands;
    fzx_results_busy=0;return true;
}
