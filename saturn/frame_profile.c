#include <yaul.h>
#include <string.h>
#include "frame_profile.h"
#include "frame_clock.h"
#define CAPACITY 4096u
#if FZX_SIMPIPE_DBUF
/* Sim-pipeline checkpoint 3 carves its slots ahead of the profile, so the
 * frame and road rows share one runtime capacity (at most CAPACITY) that
 * fits what is left below 0x00300000 with its guards. */
static uint32_t row_capacity;
#define ROW_CAPACITY row_capacity
#include "simpipe.h"
#define LOW_TOP FZX_SIMPIPE_TOP_RESERVE_BASE
#else
#define LOW_TOP 0x00300000u
#define ROW_CAPACITY CAPACITY
#endif
#define GUARD 0x39c5a71eu
typedef struct {
    uint32_t input_begin,input_end,time[8],present,field_begin,field_present,
             field_ticks,steps,flags;
} row_t;
_Static_assert(sizeof(row_t)==64,"debugger row ABI");
/* 'FZFP', version, status, rows, count, capacity, row bytes, allocation end,
 * clock faults, max gap, video Hz, CCR, presentations, restarts, reserved. */
volatile uint32_t fzx_frame_profile[16];
#if FZX_SCENE_PHASES
#include "fzx_scene_phase.h"
#include "fzx_road_stream_phase.h"
/* Diagnostic only: per-section FRT ticks summed over the race. */
volatile uint32_t fzx_scp_ticks[FZX_SCP_LAYERS][FZX_SCP_PHASES],fzx_scp_calls[FZX_SCP_LAYERS];
volatile uint32_t fzx_scp_road[FZX_RSP_PHASES],fzx_scp_road_calls;
#endif
static fzx_frame_clock_t clock_state;
static row_t *rows;
#if FZX_SOURCE_ROAD_STREAM
/* Parallel rows preserve the established 64-byte frame ABI. */
typedef struct { uint32_t ticks,commands; } road_row_t;
volatile uint32_t fzx_frame_road_profile[8];
static road_row_t *road_rows;
#endif
static row_t *current;
static row_t *volatile pending;
static bool ready,armed,start_next;
/* Per-subsystem calls, total ticks lo/hi, commands lo/hi, min/max commands,
 * and maximum ticks. Capture only whole subsystem boundaries, never primitives. */
volatile uint32_t fzx_frame_subsystems[7][8];
static uint32_t scene_start;
#if FZX_ROAD_STREAM_PHASES
#include "road_stream_native.h"
#include "fzx_road_stream_phase.h"
/* Default-off C3 phase rows, parallel to frame rows after the road rows and
 * their guard. 'C3PH', version, rows, capacity, stride, rows written, frames
 * seen, allocation end, phases, counters, frames beyond capacity, reserved,
 * then per-phase 64-bit tick totals (lo, hi) over every frame seen. */
#define PHASE_CAPACITY 1536u
typedef struct {
    uint32_t ticks[FZX_RSP_PHASES];
    uint16_t counts[16];
} phase_row_t;
_Static_assert(sizeof(phase_row_t)==4*FZX_RSP_PHASES+32,"phase row ABI");
volatile uint32_t fzx_road_phase_profile[12+2*FZX_RSP_PHASES];
fzx_rsp_state_t fzx_rsp;
static phase_row_t *phase_rows;
static uint32_t phase_capacity;
static void phase_store(void){
    fzx_rsp_set(FZX_RSP_OTHER);
    volatile uint32_t *h=fzx_road_phase_profile;
    for(unsigned i=0;i<FZX_RSP_PHASES;++i){
        const uint32_t previous=h[12+2*i];h[12+2*i]+=fzx_rsp.ticks[i];
        if(h[12+2*i]<previous)++h[13+2*i];
    }
    ++h[6];
    const uint32_t n=(uint32_t)(current-rows);
    if(n>=phase_capacity){++h[10];return;}
    phase_row_t *r=phase_rows+n;
    for(unsigned i=0;i<FZX_RSP_PHASES;++i)r->ticks[i]=fzx_rsp.ticks[i];
    for(unsigned i=0;i<FZX_RSPC_COUNTERS;++i)r->counts[i]=fzx_rsp.counts[i];
    const fzx_road_stream_draw_stats_t *s=&fzx_road_stream_stats;
    const uint32_t extra[16-FZX_RSPC_COUNTERS]={s->positions,s->quads,s->culled,s->clipped,s->commands,s->chunks,s->fog_updates};
    for(unsigned i=FZX_RSPC_COUNTERS;i<16;++i)r->counts[i]=(uint16_t)extra[i-FZX_RSPC_COUNTERS];
    if(n+1>h[5])h[5]=n+1;
}
#endif
static uint32_t stamp(uint32_t *field) {
    uint32_t a,b,base,anchor,f;uint16_t raw;
    do {
        a=clock_state.sequence;base=clock_state.epoch;anchor=clock_state.anchor;
        f=clock_state.field;raw=cpu_frt_count_get();b=clock_state.sequence;
    }while((a&1u)||a!=b);
    if(field)*field=f;
    return base+(uint16_t)(raw-anchor);
}
bool fzx_fp_init(uint32_t low_end,uint32_t hz) {
    uintptr_t start=(low_end+95u)&~(uintptr_t)31u;
#if FZX_SIMPIPE_DBUF
    {
        uint32_t per_row=sizeof(row_t);
#if FZX_SOURCE_ROAD_STREAM
        per_row+=sizeof(road_row_t);
#endif
        if(start+32u>=LOW_TOP)return false;
        uint32_t fit=(uint32_t)((LOW_TOP-32u-start)/per_row);
        row_capacity=fit<CAPACITY?fit:CAPACITY;
        if(!row_capacity)return false;
    }
#endif
    uintptr_t end=start+ROW_CAPACITY*sizeof(row_t);
#if FZX_SOURCE_ROAD_STREAM
    const uintptr_t road_start=end;
    end+=ROW_CAPACITY*sizeof(road_row_t);
#endif
    if(start<0x00200040u || end+32u>LOW_TOP)return false;
    rows=(row_t *)start;
#if FZX_SOURCE_ROAD_STREAM
    road_rows=(road_row_t *)road_start;
    /* C3PF, version, rows, capacity, stride, count, allocation end, reserved. */
    const uint32_t report[8]={0x43335046u,1,(uint32_t)road_start,ROW_CAPACITY,
        sizeof(road_row_t),0,(uint32_t)end,0};
    for(unsigned i=0;i<8;++i)fzx_frame_road_profile[i]=report[i];
#endif
    for(unsigned i=0;i<8;++i){((uint32_t *)start)[(int)i-8]=GUARD;((uint32_t *)end)[i]=GUARD;}
#if FZX_ROAD_STREAM_PHASES
    {
        const uintptr_t phase_start=end+32u;
        if(phase_start+32u>0x00300000u)return false;
        const uintptr_t room=0x00300000u-32u-phase_start;
        phase_capacity=room/sizeof(phase_row_t);
        if(phase_capacity>PHASE_CAPACITY)phase_capacity=PHASE_CAPACITY;
        phase_rows=(phase_row_t *)phase_start;
        const uintptr_t phase_end=phase_start+phase_capacity*sizeof(phase_row_t);
        memset((void *)fzx_road_phase_profile,0,sizeof(fzx_road_phase_profile));
        const uint32_t report[12]={0x43335048u,2,(uint32_t)phase_start,phase_capacity,
            sizeof(phase_row_t),0,0,(uint32_t)phase_end,FZX_RSP_PHASES,16,0,0};
        for(unsigned i=0;i<12;++i)fzx_road_phase_profile[i]=report[i];
        for(unsigned i=0;i<8;++i)((uint32_t *)phase_end)[i]=GUARD;
    }
#endif
    memset((void *)fzx_frame_profile,0,sizeof(fzx_frame_profile));
    fzx_frame_profile[0]=0x465a4650u;fzx_frame_profile[1]=2;fzx_frame_profile[2]=1;
    fzx_frame_profile[3]=start;fzx_frame_profile[5]=ROW_CAPACITY;
    fzx_frame_profile[6]=sizeof(row_t);fzx_frame_profile[7]=end;
    fzx_frame_profile[10]=hz;fzx_frame_profile[11]=*(volatile uint8_t *)0xfffffe92u;
    fzx_frame_profile[14]=(uint32_t)(uintptr_t)fzx_frame_subsystems;
    fzx_frame_profile[15]=7;
    /* Arm the clock on the next interrupt after the FRT has been initialized. */
    ready=true;return true;
}
void fzx_fp_vblank(uint16_t ticks,uint32_t field) {
    if(!ready)return;
    if(!clock_state.sequence){clock_state.anchor=ticks;clock_state.field=field;clock_state.sequence=2;return;}
    fzx_frame_clock_sample(&clock_state,ticks,field);
    fzx_frame_profile[8]=clock_state.faults;fzx_frame_profile[9]=clock_state.max_gap;
}
void fzx_fp_restart(void) {
    /* Called at the source restart tick. Discard startup and the restart frame;
     * capture begins at the next loop, without resetting the running clock. */
    armed=false;start_next=true;pending=NULL;current=NULL;++fzx_frame_profile[13];
}
void fzx_fp_begin(uint32_t input) {
    current=NULL;
    if(start_next){start_next=false;armed=true;fzx_frame_profile[4]=0;fzx_frame_profile[12]=0;memset((void *)fzx_frame_subsystems,0,sizeof(fzx_frame_subsystems));}
#if FZX_ROAD_STREAM_PHASES
    if(armed&&!fzx_frame_profile[4]){fzx_road_phase_profile[5]=0;fzx_road_phase_profile[6]=0;fzx_road_phase_profile[10]=0;
        for(unsigned i=12;i<12+2*FZX_RSP_PHASES;++i)fzx_road_phase_profile[i]=0;}
#endif
    if(!armed)return;
    uint32_t n=fzx_frame_profile[4];
    if(n>=ROW_CAPACITY){fzx_frame_profile[2]=2;armed=false;return;}
    current=rows+n;memset(current,0,sizeof(*current));current->input_begin=input;
#if FZX_SOURCE_ROAD_STREAM
    road_rows[n].ticks=0;road_rows[n].commands=0;fzx_frame_road_profile[5]=n+1;
#endif
    current->time[0]=stamp(&current->field_begin);fzx_frame_profile[4]=n+1;
}
void fzx_fp_mark(unsigned phase) {
    if(current && phase>0 && phase<6)current->time[phase]=stamp(NULL);
}
void fzx_fp_submit(uint32_t input) {
    if(!current)return;
    if(pending){fzx_frame_profile[2]=3;return;}
    current->input_end=input;current->steps=input-current->input_begin;
    /* The prior draw has completed and the caller submits the new VDP1 list
     * only after this returns, so no presentation can consume this row yet. */
    current->time[6]=stamp(NULL);current->flags|=1u;pending=current;
}
void fzx_fp_return(void) {
    /* Separate field from the interrupt writer: no read/modify/write race. */
    if(current)current->time[7]=stamp(NULL);
}
void fzx_fp_present(void) {
    row_t *p=pending;if(!p)return;
    p->present=stamp(&p->field_present);
    p->field_ticks=clock_state.max_gap;p->flags=3u;
    pending=NULL;++fzx_frame_profile[12];
}

void fzx_fp_scene_begin(void) {
    if(current)scene_start=stamp(NULL);
}
void fzx_fp_scene_end(unsigned subsystem,uint32_t commands) {
    if(!current || subsystem>=7)return;
    uint32_t elapsed=stamp(NULL)-scene_start;
#if FZX_SOURCE_ROAD_STREAM
    if(subsystem==4){
#if FZX_ROAD_STREAM_PHASES
        phase_store();
#endif
        road_row_t *r=road_rows+(current-rows);
        r->ticks+=elapsed;r->commands+=commands;
    }
#endif
    volatile uint32_t *s=fzx_frame_subsystems[subsystem];
    if(!s[0] || commands<s[5])s[5]=commands;
    ++s[0];
    uint32_t previous=s[1];s[1]+=elapsed;if(s[1]<previous)++s[2];
    previous=s[3];s[3]+=commands;if(s[3]<previous)++s[4];
    if(commands>s[6])s[6]=commands;
    if(elapsed>s[7])s[7]=elapsed;
}
