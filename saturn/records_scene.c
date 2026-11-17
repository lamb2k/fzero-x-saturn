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
#if FZX_SOURCE_FINISH_MENU
#include "fzx_finish_menu_bridge.h"
#include "finish_menu_identity.h"
fzx_finish_menu_snapshot_t *fzx_source_finish_menu;
fzx_finish_menu_draw_snapshot_t *fzx_source_finish_menu_draw;
volatile uint32_t fzx_finish_menu_pending,fzx_finish_menu_presented;
volatile uint32_t fzx_finish_menu_palette_commits,fzx_finish_menu_blanking;
volatile uint32_t fzx_finish_menu_command_start,fzx_finish_menu_command_count;
#endif

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
#if FZX_SOURCE_FINISH_MENU
    fzx_finish_menu_state_t menu_state;
    fzx_finish_menu_snapshot_t menu_snapshot;
    fzx_finish_menu_draw_snapshot_t menu_draw;
    fzx_finish_menu_palette_t menu_palette;
    const uint8_t *menu_ui;
    bool menu_active,menu_borrowed;
#endif
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
    const uintptr_t bank_end=(bank+FZX_RECORDS_UI_BYTES+FZX_RECORDS_MODELS_BYTES+
#if FZX_SOURCE_FINISH_MENU
        FZX_FINISH_MENU_BYTES+
#endif
        31u)&~(uintptr_t)31u;
    const uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u,log=(uintptr_t)fzx_input_log;
    const uintptr_t pixel=(uintptr_t)vram,tint=(uintptr_t)tints;
    const unsigned required=FZX_RECORDS_UI_BYTES+FZX_RECORDS_PALETTE_BYTES+
        FZX_RESULTS_TINT_BANK_BYTES+FZX_RECORDS_MODELS_SIX_VRAM_BYTES
#if FZX_SOURCE_FINISH_MENU
        +FZX_FINISH_MENU_BYTES+FZX_FINISH_MENU_CLUT_BYTES
#endif
        ;
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
#if FZX_SOURCE_FINISH_MENU
    scene->menu_ui=scene->models+FZX_RECORDS_MODELS_BYTES;
    if(!fzx_texture_cd_load("FINMENU.PAK",FZX_FINISH_MENU_BYTES,FZX_FINISH_MENU_HASH,
            (void *)scene->menu_ui,(void *)scratch,(uint32_t)(log-scratch-GUARD_BYTES))||
       !fzx_records_guards())return fail(FZX_RECORDS_ASSET);
    fzx_source_finish_menu=&scene->menu_snapshot;fzx_source_finish_menu_draw=&scene->menu_draw;
    fzx_finish_menu_pending=0;fzx_finish_menu_presented=0;fzx_finish_menu_palette_commits=0;
    fzx_finish_menu_command_start=0;fzx_finish_menu_command_count=0;fzx_finish_menu_blanking=0;
#endif
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
#if FZX_SOURCE_FINISH_MENU
    if(scene->snapshot.phase==FZX_RECORDS_FINISH_MENU_READY) {
        if(!scene->menu_active) {
            if(!fzx_finish_menu_begin(&scene->menu_state,&scene->snapshot,
                ref_finish_menu_no_recorded_ghost()))return fail(FZX_RECORDS_STATE);
            scene->menu_active=true;
        }
        if(!scene->menu_snapshot.action) {
            fzx_finish_menu_input_t input;
            if(!ref_finish_menu_input(&input,frame,connected)||
               !fzx_finish_menu_step(&scene->menu_state,&input)||
               !fzx_finish_menu_snapshot(&scene->menu_state,&scene->menu_snapshot))return fail(FZX_RECORDS_STATE);
            if(scene->menu_snapshot.rng1_calls&&
               !ref_finish_menu_commit_rng(&input,&scene->menu_snapshot))return fail(FZX_RECORDS_STATE);
            if(!fzx_finish_menu_draw_capture(&scene->menu_snapshot,&scene->menu_draw))return fail(FZX_RECORDS_DRAW);
        }
        /* The table remains behind the menu and uses current-frame highlight
         * colors. Advance only the draw clock on a copy of terminal state. */
        fzx_records_snapshot_t backdrop=scene->snapshot;backdrop.frame=frame;
        if(!fzx_records_draw_capture(&backdrop,&scene->draw))return fail(FZX_RECORDS_DRAW);
    }
#endif
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
#if FZX_SOURCE_FINISH_MENU
    memset(&scene->menu_state,0,sizeof(scene->menu_state));
    memset(&scene->menu_snapshot,0,sizeof(scene->menu_snapshot));
    memset(&scene->menu_draw,0,sizeof(scene->menu_draw));
    scene->menu_active=false;scene->menu_borrowed=false;fzx_finish_menu_pending=0;
    fzx_finish_menu_command_start=0;fzx_finish_menu_command_count=0;
#endif
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
#if FZX_SOURCE_FINISH_MENU
    if(scene->menu_active) {
        if(fzx_finish_menu_pending)return fail(FZX_RECORDS_DRAW);
        uint8_t *menu_pixels=previews+FZX_RECORDS_MODELS_SIX_VRAM_BYTES;
        uint8_t *menu_cluts=menu_pixels+FZX_FINISH_MENU_BYTES;
        if(!scene->menu_borrowed) {
            memcpy(menu_pixels,scene->menu_ui,FZX_FINISH_MENU_BYTES);scene->menu_borrowed=true;
        }
        unsigned added;
        if(!fzx_finish_menu_native(commands+*count,capacity-*count,&scene->menu_draw,
            menu_pixels,menu_cluts,&scene->menu_palette,&added))return fail(FZX_RECORDS_DRAW);
        fzx_finish_menu_command_start=(uint32_t)(uintptr_t)(commands+*count);
        fzx_finish_menu_command_count=added;*count+=added;
        /* The floor callback publishes this palette with the matching image,
         * even when no floor table is pending. Never upload during scanout. */
        fzx_finish_menu_pending=scene->menu_palette.tick;
    }
#endif
    if(!fzx_records_guards())return fail(FZX_RECORDS_GUARD);
    fzx_records_command_count=*count;if(*count)fzx_records_command_start=(uint32_t)(uintptr_t)commands;
    fzx_records_busy=0;return true;
}
#if FZX_SOURCE_FINISH_MENU
void fzx_records_finish_menu_present(void) {
    if(!fzx_finish_menu_pending)return;
    const volatile uint16_t *r=(const volatile uint16_t *)0x25f80000u;
    const uint32_t blank=vdp2_tvmd_vblank_in()?1u:0u;
    if(!scene||!scene->menu_active||!blank||!fzx_records_guards()||
       scene->menu_palette.tick!=fzx_finish_menu_pending||
       (r[0x0e/2]&0x3000u)!=0x1000u||r[0xe0/2]!=0x1520u||
       (r[0xf0/2]&0x0707u)!=0x0506u||(r[0xe6/2]&0x0070u)||
       !fzx_finish_menu_palette_upload(&scene->menu_palette,(volatile uint16_t *)0x25f00000u,2048u)) {
        fail(FZX_RECORDS_DRAW);fzx_finish_menu_pending=0;return;
    }
    fzx_finish_menu_blanking=blank|(vdp2_tvmd_vblank_in()?2u:0u);
    fzx_finish_menu_presented=fzx_finish_menu_pending;fzx_finish_menu_pending=0;
    ++fzx_finish_menu_palette_commits;
    if(fzx_finish_menu_blanking!=3)fail(FZX_RECORDS_DRAW);
}
#endif
