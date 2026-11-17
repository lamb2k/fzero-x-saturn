#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Actual scene lifecycle, controlled collaborators. No source simulation,
 * VDP1 raster, asset-loader or physical-memory qualification is implied. */
#include "records_scene_under_test.c"

#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
enum {NONE,BEGIN,INPUT,STEP,SNAPSHOT,COMMIT,DRAW,POST_GUARD};
static unsigned failure,begins,inputs,steps,snapshots,commits,draws,order;
static bool workspace_ok=true;
static uint32_t rng_calls,next_phase;

bool fzx_race_workspace_guards(void){return workspace_ok;}
bool fzx_records_begin(fzx_records_state_t *s,const fzx_results_state_t *r){
    ++begins;if(failure==BEGIN)return false;
    memset(s,0,sizeof(*s));s->initialized=1;s->snapshot.frame=r->input.frame;
    s->snapshot.records=r->records;return true;
}
bool ref_records_input(fzx_records_input_t *out,uint32_t frame,bool connected){
    ++inputs;if(failure==INPUT)return false;
    *out=(fzx_records_input_t){.frame=frame,.connected=connected,.rng_seed1=123,.rng_mask1=456};
    order=1;return true;
}
bool fzx_records_step(fzx_records_state_t *s,const fzx_records_input_t *in){
    ++steps;if(failure==STEP||order!=1)return false;order=2;
    s->snapshot.input=*in;s->snapshot.frame=in->frame;++s->snapshot.tick;
    s->snapshot.rng_calls=rng_calls;s->snapshot.rng_seed1=789;s->snapshot.rng_mask1=987;
    s->snapshot.phase=(uint8_t)next_phase;memcpy(s->snapshot.records.recordNames[0],"NEW",4);
    return true;
}
bool fzx_records_snapshot(const fzx_records_state_t *s,fzx_records_snapshot_t *out){
    ++snapshots;if(failure==SNAPSHOT||order!=2)return false;order=3;*out=s->snapshot;return true;
}
bool ref_records_commit_rng(const fzx_records_input_t *in,const fzx_records_snapshot_t *p){
    ++commits;if(failure==COMMIT||order!=3||!p->rng_calls||p->frame!=in->frame||
        p->rng_seed1!=789||p->rng_mask1!=987)return false;
    order=4;return true;
}
bool fzx_records_draw_capture(const fzx_records_snapshot_t *p,fzx_records_draw_snapshot_t *out){
    ++draws;if(failure==DRAW||order!=(p->rng_calls?4u:3u))return false;
    memset(out,0,sizeof(*out));out->frame=p->frame;out->tick=p->tick;
    if(failure==POST_GUARD)*(uint32_t *)(uintptr_t)fzx_records_low_end=0;
    return true;
}
static void seed(void){
    scene=(scene_t *)(uintptr_t)0x00200020u;memset(scene,0,sizeof(*scene));
    fzx_records_low_start=0x00200020u;
    fzx_records_low_end=(uint32_t)(((uintptr_t)scene+sizeof(*scene)+31u)&~(uintptr_t)31u);
    fzx_records_cart_start=0x04000020u;fzx_records_cart_end=0x04000120u;
    guards(fzx_records_low_start,fzx_records_low_end);guards(fzx_records_cart_start,fzx_records_cart_end);
    fzx_source_records=&scene->snapshot;fzx_source_records_draw=&scene->draw;
    fzx_records_status=FZX_RECORDS_READY;fzx_records_active=0;fzx_records_busy=0;
    fzx_records_command_start=0;fzx_records_command_count=0;
    failure=NONE;begins=inputs=steps=snapshots=commits=draws=order=0;
    workspace_ok=true;rng_calls=100;next_phase=FZX_RECORDS_NAME_ENTRY;
}
static fzx_results_state_t result(void){
    fzx_results_state_t r={0};r.phase=FZX_RESULTS_RECORDS_REQUEST;r.input.frame=12322;
    memcpy(r.records.recordNames[0],"OLD",4);return r;
}
int main(void){
    CHECK(mmap((void *)0x00200000u,0x100000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    CHECK(mmap((void *)0x04000000u,4096u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    seed();fzx_results_state_t r=result();r.phase=FZX_RESULTS_TIMES;
    CHECK(fzx_records_tick(&r,12321,true));CHECK(!begins&&!inputs&&!fzx_records_active);
    r.phase=FZX_RESULTS_RECORDS_REQUEST;
    CHECK(fzx_records_tick(&r,12322,true));
    CHECK(begins==1&&inputs==1&&steps==1&&snapshots==1&&commits==1&&draws==1);
    CHECK(fzx_source_records->frame==12322&&fzx_source_records_draw->frame==12322);
    CHECK(fzx_source_records_input.connected&&fzx_records_active&&!fzx_records_busy);
    CHECK(!memcmp(r.records.recordNames[0],"NEW",4));
    rng_calls=0;CHECK(fzx_records_tick(&r,12323,false));
    CHECK(begins==1&&steps==2&&commits==1&&!fzx_source_records_input.connected);
    next_phase=FZX_RECORDS_FINISH_MENU_READY;CHECK(fzx_records_tick(&r,12324,true));
    const scene_t held=*scene;
    for(unsigned i=0;i<20;++i)CHECK(fzx_records_tick(&r,12325+i,true));
    CHECK(!memcmp(scene,&held,sizeof(*scene))&&steps==3&&inputs==3&&commits==1&&draws==3);
    scene->borrowed=true;scene->camera_checked=true;scene->tint_level=6;
    for(unsigned i=0;i<6;++i)scene->selected[i]=(uint16_t)i;
    fzx_records_command_start=123;fzx_records_command_count=99;
    CHECK(fzx_records_restart());CHECK(!fzx_records_active&&!fzx_records_busy);
    CHECK(!scene->state.initialized&&!scene->snapshot.tick&&!scene->draw.tick);
    CHECK(!scene->borrowed&&!scene->camera_checked&&scene->tint_level==UINT32_MAX);
    for(unsigned i=0;i<6;++i)CHECK(scene->selected[i]==UINT16_MAX);
    CHECK(!fzx_records_command_start&&!fzx_records_command_count);
    CHECK(!memcmp(r.records.recordNames[0],"NEW",4));
    next_phase=FZX_RECORDS_NAME_ENTRY;CHECK(fzx_records_tick(&r,44,true));
    CHECK(begins==2&&scene->snapshot.tick==1&&scene->snapshot.frame==44);
    for(unsigned f=BEGIN;f<=POST_GUARD;++f){
        seed();r=result();failure=f;fzx_records_command_count=99;fzx_records_command_start=123;
        CHECK(!fzx_records_tick(&r,12322,true));
        CHECK(fzx_records_status==(f==DRAW?FZX_RECORDS_DRAW:f==POST_GUARD?FZX_RECORDS_GUARD:FZX_RECORDS_STATE));
        CHECK(!fzx_records_busy&&!fzx_records_command_count&&!fzx_records_command_start);
        if(f<=SNAPSHOT)CHECK(!commits);if(f==COMMIT)CHECK(!draws);
        if(f<=DRAW)CHECK(!memcmp(r.records.recordNames[0],"OLD",4));
    }
    for(unsigned which=0;which<3;++which){
        seed();r=result();
        if(which==0)*(uint32_t *)(uintptr_t)fzx_records_low_end=0;
        else if(which==1)*(uint32_t *)(uintptr_t)(fzx_records_cart_start-GUARD_BYTES)=0;
        else workspace_ok=false;
        CHECK(!fzx_records_tick(&r,12322,true)&&fzx_records_status==FZX_RECORDS_GUARD&&!inputs);
        seed();if(which==0)*(uint32_t *)(uintptr_t)fzx_records_low_end=0;
        else if(which==1)*(uint32_t *)(uintptr_t)(fzx_records_cart_start-GUARD_BYTES)=0;
        else workspace_ok=false;
        CHECK(!fzx_records_restart()&&fzx_records_status==FZX_RECORDS_GUARD);
    }
    seed();CHECK(!fzx_records_tick(NULL,0,true));
    puts("RECORDS_SCENE_LIFECYCLE_PASS same-frame RNG terminal records restart failures guards");return 0;
}
