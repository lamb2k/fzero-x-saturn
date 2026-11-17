#include "records_scene.h"
#include "fzx_records_bridge.h"
#include "records_identity.h"
#include "records_models_identity.h"
#include "results_identity.h"
#include "countdown_identity.h"
#include "texture_cd.h"
#include "race_workspace_native.h"
#include "countdown_scene.h"
#include <string.h>

extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_log[];
volatile uint32_t fzx_records_status,fzx_records_busy,fzx_records_active;
volatile uint32_t fzx_records_low_start,fzx_records_low_end;
volatile uint32_t fzx_records_cart_start,fzx_records_cart_end;
volatile uint32_t fzx_records_command_start,fzx_records_command_count;
volatile uint32_t fzx_records_vram_start,fzx_records_vram_bytes;
fzx_records_snapshot_t *fzx_source_records;
fzx_records_draw_snapshot_t *fzx_source_records_draw;
fzx_records_input_t fzx_source_records_input;
enum {GUARD=0x1b76c9e3u,GUARD_BYTES=32u};
typedef struct {
    fzx_records_state_t state;
    fzx_records_snapshot_t snapshot;
    fzx_records_draw_snapshot_t draw;
    const uint8_t *ui,*models,*tints;
    uint8_t *vram;
    uint16_t selected[6];
    uint32_t tint_level;
    bool borrowed,camera_checked;
} scene_t;
static scene_t *scene;
static bool fail(unsigned status) {
    fzx_records_status=status;fzx_records_busy=0;
    fzx_records_command_start=0;fzx_records_command_count=0;return false;
}
static void guards(uintptr_t begin,uintptr_t end) {
    volatile uint32_t *a=(volatile uint32_t *)(begin-GUARD_BYTES),*b=(volatile uint32_t *)end;
    for(unsigned i=0;i<8;++i)a[i]=b[i]=GUARD;
}
static bool check(uintptr_t begin,uintptr_t end) {
    const volatile uint32_t *a=(volatile uint32_t *)(begin-GUARD_BYTES),*b=(volatile uint32_t *)end;
    for(unsigned i=0;i<8;++i)if(a[i]!=GUARD||b[i]!=GUARD)return false;
    return true;
}
bool fzx_records_guards(void) {
    return scene&&fzx_records_low_start>=0x00200020u&&fzx_records_low_end<=0x002fffe0u&&
        fzx_records_low_start<fzx_records_low_end&&fzx_records_cart_start<fzx_records_cart_end&&
        check(fzx_records_low_start,fzx_records_low_end)&&check(fzx_records_cart_start,fzx_records_cart_end)&&
        fzx_race_workspace_guards();
}
bool fzx_records_load(uint32_t low_end,uint32_t cart_end,void *vram,unsigned bytes,const void *tints) {
    scene=NULL;fzx_source_records=NULL;fzx_source_records_draw=NULL;
    fzx_records_status=FZX_RECORDS_UNLOADED;fzx_records_busy=1;fzx_records_active=0;
    fzx_records_command_start=0;fzx_records_command_count=0;
    const uintptr_t base=(uintptr_t)dram_cart_area_get(),limit=base+dram_cart_size_get();
    const uintptr_t context=((uintptr_t)low_end+95u)&~(uintptr_t)31u;
    const uintptr_t end=(context+sizeof(scene_t)+31u)&~(uintptr_t)31u;
    const uintptr_t bank=((uintptr_t)cart_end+95u)&~(uintptr_t)31u;
    const uintptr_t bank_end=(bank+FZX_RECORDS_UI_BYTES+FZX_RECORDS_MODELS_BYTES+31u)&~(uintptr_t)31u;
    const uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u,log=(uintptr_t)fzx_input_log;
    const uintptr_t pixel=(uintptr_t)vram,tint=(uintptr_t)tints;
    const unsigned required=FZX_RECORDS_UI_BYTES+FZX_RECORDS_PALETTE_BYTES+
        FZX_RESULTS_TINT_BANK_BYTES+FZX_RECORDS_MODELS_SIX_VRAM_BYTES;
    if(low_end<0x00200000u||low_end>0x002fffa0u||end>0x002fffe0u||
       dram_cart_id_get()!=DRAM_CART_ID_4MIB||cart_end<base||cart_end>limit-GUARD_BYTES||
       bank_end>limit-GUARD_BYTES||tint<base||tint>limit||FZX_RESULTS_TINT_BYTES>limit-tint||
       log<scratch+GUARD_BYTES||fzx_texture_cd_scratch_bytes()>log-scratch-GUARD_BYTES||
       pixel<(uintptr_t)VDP1_VRAM(0)||pixel>=(uintptr_t)VDP1_VRAM(0)+0x80000u||(pixel&7u)||
       required>bytes||FZX_COUNTDOWN_TEXTURE_BYTES>bytes||bytes>0x80000u-(pixel&0x7ffffu)||
       !fzx_race_workspace_guards())return fail(FZX_RECORDS_RANGE);
    fzx_records_low_start=(uint32_t)context;fzx_records_low_end=(uint32_t)end;
    fzx_records_cart_start=(uint32_t)bank;fzx_records_cart_end=(uint32_t)bank_end;
    guards(context,end);guards(bank,bank_end);
    scene=(scene_t *)context;memset(scene,0,sizeof(*scene));
    scene->ui=(const uint8_t *)bank;scene->models=scene->ui+FZX_RECORDS_UI_BYTES;
    scene->tints=tints;scene->vram=vram;scene->tint_level=UINT32_MAX;
    for(unsigned i=0;i<6;++i)scene->selected[i]=UINT16_MAX;
    if(!fzx_texture_cd_load("RECORDUI.PAK",FZX_RECORDS_UI_BYTES,FZX_RECORDS_UI_HASH,
            (void *)scene->ui,(void *)scratch,(uint32_t)(log-scratch-GUARD_BYTES))||
       !fzx_records_guards()||
       !fzx_texture_cd_load("RMODELS.PAK",FZX_RECORDS_MODELS_BYTES,FZX_RECORDS_MODELS_HASH,
            (void *)scene->models,(void *)scratch,(uint32_t)(log-scratch-GUARD_BYTES)))return fail(FZX_RECORDS_ASSET);
    if(!fzx_records_guards())return fail(FZX_RECORDS_GUARD);
    fzx_source_records=&scene->snapshot;fzx_source_records_draw=&scene->draw;
    fzx_records_vram_start=(uint32_t)pixel;fzx_records_vram_bytes=required;
    fzx_records_status=FZX_RECORDS_READY;fzx_records_busy=0;return true;
}
bool fzx_records_tick(fzx_results_state_t *results,uint32_t frame,bool connected) {
    if(!results||fzx_records_status!=FZX_RECORDS_READY)return false;
    if(!fzx_records_guards())return fail(FZX_RECORDS_GUARD);
    if(!fzx_records_active&&results->phase!=FZX_RESULTS_RECORDS_REQUEST)return true;
    fzx_records_busy=1;
    if(!fzx_records_active) {
        if(!fzx_records_begin(&scene->state,results))return fail(FZX_RECORDS_STATE);
        fzx_records_active=1;
    }
    if(scene->state.snapshot.phase!=FZX_RECORDS_FINISH_MENU_READY) {
        if(!ref_records_input(&fzx_source_records_input,frame,connected)||
           !fzx_records_step(&scene->state,&fzx_source_records_input)||
           !fzx_records_snapshot(&scene->state,&scene->snapshot))return fail(FZX_RECORDS_STATE);
        /* Camera/randomness already advanced this frame. Publish only the
         * additional RNG consumed by this successful original records step. */
        if(scene->snapshot.rng_calls&&
           !ref_records_commit_rng(&fzx_source_records_input,&scene->snapshot))return fail(FZX_RECORDS_STATE);
        if(!fzx_records_draw_capture(&scene->snapshot,&scene->draw))return fail(FZX_RECORDS_DRAW);
        results->records=scene->snapshot.records;
    }
    if(!fzx_records_guards())return fail(FZX_RECORDS_GUARD);
    fzx_records_busy=0;return true;
}
bool fzx_records_restart(void) {
    if(fzx_records_status!=FZX_RECORDS_READY)return false;
    if(!fzx_records_guards())return fail(FZX_RECORDS_GUARD);
    /* Results owns countdown restoration. Its session records were updated
     * after each successful source step, including the completed name. */
    memset(&scene->state,0,sizeof(scene->state));memset(&scene->snapshot,0,sizeof(scene->snapshot));
    memset(&scene->draw,0,sizeof(scene->draw));scene->borrowed=false;scene->camera_checked=false;
    scene->tint_level=UINT32_MAX;for(unsigned i=0;i<6;++i)scene->selected[i]=UINT16_MAX;
    fzx_records_active=0;fzx_records_busy=0;fzx_records_command_start=0;fzx_records_command_count=0;
    return true;
}
bool fzx_records_scene_draw(vdp1_cmdt_t *commands,unsigned capacity,unsigned *count) {
    if(count)*count=0;fzx_records_command_start=0;fzx_records_command_count=0;
    if(!commands||!count||fzx_records_status!=FZX_RECORDS_READY)return false;
    if(!fzx_records_guards())return fail(FZX_RECORDS_GUARD);
    if(!fzx_records_active)return true;
    fzx_records_busy=1;
    if(scene->draw.count>FZX_RECORDS_DRAW_MAX||scene->draw.machine_count>6||
       scene->draw.spinner.visible>1)return fail(FZX_RECORDS_DRAW);
    if(fzx_source_countdown.timer)return fail(FZX_RECORDS_STATE);
    if(!scene->borrowed) {memcpy(scene->vram,scene->ui,FZX_RECORDS_UI_BYTES);scene->borrowed=true;}
    uint8_t *palettes=scene->vram+FZX_RECORDS_UI_BYTES;
    uint8_t *tints=palettes+FZX_RECORDS_PALETTE_BYTES;
    uint8_t *previews=tints+FZX_RESULTS_TINT_BANK_BYTES;
    unsigned level=15;
    for(unsigned i=0;i<scene->draw.count;++i) {
        const fzx_results_draw_t *d=&scene->draw.draws[i];
        if(d->kind!=FZX_RESULTS_RECT&&d->image<8&&fzx_results_tint_offsets[d->image]!=UINT32_MAX&&
           d->r==255&&d->g==d->b&&d->g!=255){level=(d->g*15u+127u)/255u;break;}
    }
    if(scene->tint_level!=level) {
        memcpy(tints,scene->tints+level*FZX_RESULTS_TINT_BANK_BYTES,FZX_RESULTS_TINT_BANK_BYTES);
        scene->tint_level=level;
    }
    if(scene->draw.machine_count&&!scene->camera_checked) {
        if(!fzx_records_model_camera())return fail(FZX_RECORDS_DRAW);
        scene->camera_checked=true;
    }
    for(unsigned i=0;i<scene->draw.machine_count;++i) {
        const fzx_records_machine_draw_t *m=&scene->draw.machines[i];
        unsigned index,offset,bytes;
        if(!fzx_records_model(m->character,((uint32_t)m->r<<16)|((uint32_t)m->g<<8)|m->b,
                              &index,&offset,&bytes))return fail(FZX_RECORDS_DRAW);
        if(scene->selected[i]!=index) {
            memcpy(previews+i*FZX_RECORDS_MODELS_MAX_VRAM_BYTES,scene->models+offset,bytes);
            scene->selected[i]=(uint16_t)index;
        }
    }
    if(!fzx_records_native(commands,capacity,&scene->draw,scene->vram,palettes,tints,level,
                            previews,scene->selected,count))return fail(FZX_RECORDS_DRAW);
    if(!fzx_records_guards())return fail(FZX_RECORDS_GUARD);
    fzx_records_command_count=*count;if(*count)fzx_records_command_start=(uint32_t)(uintptr_t)commands;
    fzx_records_busy=0;return true;
}
