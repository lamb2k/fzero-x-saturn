#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Compile the real scene TU, including its static storage, against controlled
 * state/input/CD collaborators. This is a lifecycle API test, not source-math,
 * asset-loader, VDP1 raster, or physical-memory qualification. */
#include "results_scene_under_test.c"

#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
enum { FAIL_NONE,FAIL_INPUT,FAIL_STEP,FAIL_SNAPSHOT,FAIL_RESET,FAIL_CD,FAIL_POST_GUARD };
static unsigned failure,input_calls,step_calls,reset_calls,cd_calls;
static bool workspace_ok=true;
static uint8_t video[FZX_COUNTDOWN_TEXTURE_BYTES];

bool fzx_race_workspace_guards(void){return workspace_ok;}
bool ref_results_input(fzx_results_input_t *out,uint32_t frame,bool connected){
    ++input_calls;
    if(failure==FAIL_INPUT)return false;
    *out=(fzx_results_input_t){.frame=frame,.connected=connected};return true;
}
bool fzx_results_step(fzx_results_state_t *state,const fzx_results_input_t *input){
    ++step_calls;
    if(failure==FAIL_STEP)return false;
    /* Mirror only the documented terminal snapshot contract; no source
     * simulation or source pulse arithmetic is implemented in this mock. */
    if(state->phase==FZX_RESULTS_RECORDS_REQUEST)return true;
    state->input=*input;
    state->snapshot.tick=++state->ticks;
    state->snapshot.frame=input->frame;
    return true;
}
bool fzx_results_snapshot(const fzx_results_state_t *state,fzx_results_snapshot_t *out){
    if(failure==FAIL_SNAPSHOT)return false;
    *out=state->snapshot;
    if(failure==FAIL_POST_GUARD)*(uint32_t *)(uintptr_t)fzx_results_low_end=0;
    return true;
}
bool fzx_results_reset(fzx_results_state_t *state,const fzx_results_config_t *config,
                       const fzx_results_records_t *records){
    ++reset_calls;
    if(failure==FAIL_RESET)return false;
    /* Verify the scene passes its retained values across the reset boundary.
     * The real reset implementation's alias handling has a separate test. */
    const fzx_results_config_t saved_config=*config;
    fzx_results_records_t saved_records={0};
    if(records)saved_records=*records;
    memset(state,0,sizeof(*state));state->config=saved_config;
    state->records=saved_records;state->initialized=1;return true;
}
static uint8_t restored_byte(unsigned i){return (uint8_t)(0x39u+i*17u);}
bool fzx_texture_cd_load(const char *name,uint32_t bytes,uint32_t hash,
                         void *out,void *scratch,uint32_t scratch_bytes){
    (void)scratch;(void)scratch_bytes;++cd_calls;
    if(failure==FAIL_CD)return false;
    if(strcmp(name,"COUNTDTX.PAK")||bytes!=sizeof(video)||
       hash!=FZX_COUNTDOWN_TEXTURE_HASH||out!=video)return false;
    for(unsigned i=0;i<bytes;++i)((uint8_t *)out)[i]=restored_byte(i);
    return true;
}

static void seed(void){
    scene=(scene_t *)(uintptr_t)0x00200020u;memset(scene,0,sizeof(*scene));
    fzx_results_low_start=0x00200020u;
    fzx_results_low_end=(uint32_t)(((uintptr_t)scene+sizeof(*scene)+31u)&~(uintptr_t)31u);
    fzx_results_cart_start=0x04000020u;fzx_results_cart_end=0x04000120u;
    guard_write(fzx_results_low_start,fzx_results_low_end);
    guard_write(fzx_results_cart_start,fzx_results_cart_end);
    scene->state.initialized=1;memcpy(scene->state.config.name,"synthetic",10);
    scene->vram=video;fzx_source_results=&scene->snapshot;
    fzx_source_results_input=(fzx_results_input_t){0};
    fzx_results_status=FZX_RESULTS_READY;fzx_results_busy=0;
    fzx_results_command_start=0;fzx_results_command_count=0;
    fzx_results_texture_borrowed=0;fzx_results_tint_level=UINT32_MAX;
    failure=FAIL_NONE;workspace_ok=true;input_calls=step_calls=reset_calls=cd_calls=0;
}
static void ready(void){failure=FAIL_NONE;fzx_results_status=FZX_RESULTS_READY;}

int main(void){
    CHECK(mmap((void *)0x00200000u,0x100000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    CHECK(mmap((void *)0x04000000u,4096u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    seed();CHECK(fzx_results_guards());
    /* Two complete 32-phase animation cycles beyond the diagnostic log cap. */
    for(unsigned frame=1;frame<=12352u;++frame){
        CHECK(fzx_results_tick((frame&1u)!=0));
        CHECK(scene->frame==frame&&fzx_source_results_input.frame==frame);
        CHECK(fzx_source_results_input.connected==(frame&1u));
        CHECK(fzx_source_results->frame==frame&&fzx_source_results->tick==frame);
        CHECK(fzx_results_status==FZX_RESULTS_READY&&!fzx_results_busy);
    }
    const fzx_results_snapshot_t held=*fzx_source_results;
    for(unsigned i=0;i<64u;++i){
        fzx_results_snapshot_t copy;
        CHECK(fzx_results_snapshot(&scene->state,&copy)&&fzx_results_guards());
        CHECK(!memcmp(&copy,&held,sizeof(copy))&&scene->frame==12352u);
    }
    CHECK(input_calls==12352u&&step_calls==12352u&&cd_calls==0);
    scene->state.phase=FZX_RESULTS_RECORDS_REQUEST;
    for(unsigned frame=12353;frame<=12384u;++frame){
        CHECK(fzx_results_tick(true)&&scene->frame==frame);
        CHECK(fzx_source_results_input.frame==frame);
        CHECK(!memcmp(fzx_source_results,&held,sizeof(held)));
    }
    scene->state.phase=FZX_RESULTS_RACING;
    for(unsigned fault=FAIL_INPUT;fault<=FAIL_SNAPSHOT;++fault){
        const uint32_t before=scene->frame;
        failure=fault;fzx_results_command_start=64;fzx_results_command_count=4;
        CHECK(!fzx_results_tick(true)&&scene->frame==before);
        CHECK(fzx_results_status==FZX_RESULTS_STATE&&!fzx_results_busy);
        CHECK(!fzx_results_command_start&&!fzx_results_command_count);
        ready();CHECK(fzx_results_tick(true)&&scene->frame==before+1u);
        CHECK(fzx_source_results_input.frame==before+1u);
    }
    scene->frame=UINT32_MAX;
    const unsigned calls=input_calls;
    CHECK(!fzx_results_tick(true)&&scene->frame==UINT32_MAX&&input_calls==calls);
    CHECK(fzx_results_status==FZX_RESULTS_STATE&&!fzx_results_busy);

    seed();CHECK(fzx_results_tick(true));
    CHECK(fzx_results_restart()&&scene->frame==0&&cd_calls==0);
    CHECK(fzx_results_tick(true)&&fzx_source_results_input.frame==1);
    /* Distinctive synthetic records exercise all retained bytes, not just a
     * zero-initialized record. The mock does not certify record validity. */
    fzx_results_records_t retained;
    memset(&retained,0x5a,sizeof(retained));scene->state.records=retained;
    const fzx_results_config_t config=scene->state.config;
    memset(video,0xcc,sizeof(video));fzx_results_texture_borrowed=1;
    fzx_results_tint_level=6;fzx_results_command_start=64;fzx_results_command_count=4;
    CHECK(fzx_results_restart());
    CHECK(scene->frame==0&&!scene->published&&!fzx_results_texture_borrowed);
    CHECK(fzx_results_tint_level==UINT32_MAX&&!fzx_results_command_start&&!fzx_results_command_count);
    CHECK(cd_calls==1&&reset_calls==2&&!memcmp(&scene->state.records,&retained,sizeof(retained)));
    CHECK(!memcmp(&scene->state.config,&config,sizeof(config)));
    for(unsigned i=0;i<sizeof(video);++i)CHECK(video[i]==restored_byte(i));
    CHECK(fzx_results_tick(true)&&fzx_source_results_input.frame==1);
    CHECK(fzx_source_results->tick==1&&fzx_source_results->frame==1);

    /* A failed restore cannot publish a restarted countdown or reset state. */
    fzx_results_texture_borrowed=1;failure=FAIL_CD;
    const unsigned resets=reset_calls;
    CHECK(!fzx_results_restart()&&scene->frame==1&&reset_calls==resets);
    CHECK(fzx_results_status==FZX_RESULTS_ASSET&&fzx_results_texture_borrowed&&!fzx_results_busy);
    ready();failure=FAIL_RESET;fzx_results_texture_borrowed=0;
    CHECK(!fzx_results_restart()&&scene->frame==1&&fzx_results_status==FZX_RESULTS_STATE);

    seed();*(uint32_t *)(uintptr_t)fzx_results_low_end=0;
    CHECK(!fzx_results_restart()&&fzx_results_status==FZX_RESULTS_GUARD&&!fzx_results_busy);
    CHECK(cd_calls==0&&reset_calls==0);
    seed();*(uint32_t *)(uintptr_t)fzx_results_cart_end=0;
    CHECK(!fzx_results_tick(true)&&fzx_results_status==FZX_RESULTS_GUARD&&scene->frame==0);
    CHECK(input_calls==0);
    seed();failure=FAIL_POST_GUARD;
    CHECK(!fzx_results_tick(true)&&scene->frame==0&&fzx_results_status==FZX_RESULTS_GUARD);
    seed();failure=FAIL_POST_GUARD;
    CHECK(!fzx_results_restart()&&fzx_results_status==FZX_RESULTS_GUARD&&!fzx_results_busy);
    seed();workspace_ok=false;
    CHECK(!fzx_results_restart()&&fzx_results_status==FZX_RESULTS_GUARD);
    puts("RESULTS_SCENE_LIFECYCLE_PASS clock=12384 held=64 failures=guard,input,state,snapshot,restore,reset,overflow");
    return 0;
}
