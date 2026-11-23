/* FZX_SLAVE_ROAD=1: the slave SH-2 runs fzx_road_stream_native() for the
 * master (generated/SLAVE-ROAD-PLAN.md). The job record is uncached; the
 * slave purges its cache before each job; the master never reads the road
 * commands (VDP1 list DMA reads memory) and purges the statistics it may read. */
#include "slave_road.h"

volatile uint32_t fzx_slave_road_jobs,fzx_slave_road_late;
volatile uint32_t fzx_slave_road_wait_sum,fzx_slave_road_wait_max;
volatile uint32_t fzx_slave_road_busy_sum,fzx_slave_road_busy_max;
volatile uint32_t fzx_slave_road_stack_unused,fzx_slave_road_status;
volatile uint32_t fzx_slave_road_guard_trips,fzx_slave_road_guard_first_job,fzx_slave_road_sp;

typedef struct {
    uint32_t seq,done,ok,count,ticks,frt_ready,ratio,sp;
#if ENG_PICK_AHEAD
    uint32_t pick_only;   /* 1: the job is the early pick alone (no args) */
#endif
    fzx_slave_road_args_t args;
} job_t;
#if ENG_PICK_AHEAD
static volatile uint32_t pick_pending;   /* master: an early pick job was started and not yet seen done */
#endif
/* Linked at 0x2xxxxxxx (libyaul .uncached): both CPUs read memory. */
static volatile job_t job __uncached;

/* Slave stack reservation (Makefile: IP_SLAVE_STACK_ADDR 0x06004000 under the
 * knob): the 8 KB IP.BIN area 0x06002000..0x06004000, free after boot because
 * these builds put the master stack at 0x06100000 and the program at 0x06004000.
 * Above the BIOS area; outside the program image, low WRAM and the cartridge.
 * Eight guard words at the floor; the master paints the rest up to 512 bytes
 * below the top and measures what the slave never touched. */
enum {STACK_TOP=0x06004000u,STACK_FLOOR=0x06002000u,GUARD_BYTES=32u,
      STACK_PAINT_END=0x06003E00u,PAINT=0x5a17c0deu,GUARD=0x51a7e5afu,
      LATE_FIELDS=4u,TIMEOUT_FIELDS=120u};
#define UNCACHED32(a) ((volatile uint32_t *)(0x20000000u|(uintptr_t)(a)))
/* Compiler barrier: no memory access moves across it. The SH-2 itself retires
 * loads and stores in program order, and both CPUs see the job record uncached. */
#define BARRIER() __asm__ volatile("" ::: "memory")
#ifndef FZX_SLAVE_ROAD_STACK_PROBE
#define FZX_SLAVE_ROAD_STACK_PROBE 0
#endif

#if FZX_SLAVE_ROAD_STACK_PROBE
/* Negative control only: one artificially oversized slave frame (job 2). */
static void __attribute__((noinline)) stack_probe(void){
    volatile uint8_t big[FZX_SLAVE_ROAD_STACK_PROBE];
    for(unsigned i=0;i<sizeof(big);++i)big[i]=(uint8_t)i;
}
#endif

static void slave_entry(void){
    cpu_cache_purge();
    if(!job.frt_ready){cpu_frt_init(CPU_FRT_CLOCK_DIV_128);job.frt_ready=1;}
    BARRIER(); /* the payload is read after the notification */
#if ENG_PICK_AHEAD && defined(FZX_ENGINE)
    if(job.pick_only){
        extern bool fzx_engine_slave_pick_early(void);
        const bool pok=fzx_engine_slave_pick_early();
        job.ok=pok;
        BARRIER();
        job.done=job.seq;
        return;
    }
#endif
    const fzx_slave_road_args_t a=job.args;
    uint32_t n=0;
    job.sp=(uint32_t)(uintptr_t)&n;
#if FZX_SLAVE_ROAD_STACK_PROBE
    if(job.seq==2u)stack_probe();
#endif
    const uint16_t t0=cpu_frt_count_get();
#ifdef FZX_ENGINE
    /* Engine line: the slave picks the visible track, then builds it. */
#if ENG_PICK_AHEAD
    extern bool fzx_engine_slave_pick_for_job(bool pre);
    const bool ok=fzx_engine_slave_pick_for_job(a.pre!=0u)&&
#else
    extern bool fzx_engine_slave_pick(void);
    const bool ok=fzx_engine_slave_pick()&&
#endif
        fzx_road_stream_native(a.commands,a.capacity,a.view,a.eye,a.plan,a.pa,a.pb,a.pixels,a.shades,&n);
#else
    const bool ok=fzx_road_stream_native(a.commands,a.capacity,a.view,a.eye,a.plan,a.pa,a.pb,a.pixels,a.shades,&n);
#endif
    job.ticks=(uint16_t)(cpu_frt_count_get()-t0);
#if defined(FZX_ENGINE) && defined(FZX_E1_DRIVE) && FZX_E1_DRIVE
    /* Engine E1: the job's second part (sprite slot fills), timed by its owner. */
    extern void fzx_engine_slave_after(void);
    fzx_engine_slave_after();
#endif
    job.count=n;job.ok=ok;
    BARRIER(); /* road output, count and result are stored before done */
    job.done=job.seq;
}

static uint32_t stack_unused(void){
    uintptr_t p=STACK_FLOOR+GUARD_BYTES;
    while(p<STACK_PAINT_END && *UNCACHED32(p)==PAINT)p+=4u;
    return (uint32_t)(p-(STACK_FLOOR+GUARD_BYTES));
}

static bool guards_ok(void){
    for(unsigned i=0;i<GUARD_BYTES/4u;++i)if(*UNCACHED32(STACK_FLOOR+4u*i)!=GUARD)return false;
    return true;
}

void fzx_slave_road_init(void){
    /* The linked slave stack must be the reservation. */
    if((uintptr_t)cpu_dual_slave_stack_get()!=STACK_TOP){fzx_slave_road_status=8;return;}
    for(uintptr_t p=STACK_FLOOR;p<STACK_FLOOR+GUARD_BYTES;p+=4u)*UNCACHED32(p)=GUARD;
    for(uintptr_t p=STACK_FLOOR+GUARD_BYTES;p<STACK_PAINT_END;p+=4u)*UNCACHED32(p)=PAINT;
    /* Slave ticks are DIV_128; scale to the master's divider (TCR CKS). */
    const uint8_t cks=*(volatile uint8_t *)0xfffffe92u&3u;
    job.ratio=cks==0?16u:cks==1?4u:1u;
    job.seq=0;job.done=0;job.frt_ready=0;job.sp=0;
#if ENG_PICK_AHEAD
    job.pick_only=0;pick_pending=0;
#endif
    fzx_slave_road_jobs=0;fzx_slave_road_late=0;
    fzx_slave_road_wait_sum=0;fzx_slave_road_wait_max=0;
    fzx_slave_road_busy_sum=0;fzx_slave_road_busy_max=0;
    fzx_slave_road_guard_trips=0;fzx_slave_road_guard_first_job=0;fzx_slave_road_sp=0;
    fzx_slave_road_stack_unused=STACK_PAINT_END-(STACK_FLOOR+GUARD_BYTES);
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(slave_entry);
    fzx_slave_road_status=1;
}

#if ENG_PICK_AHEAD
void fzx_slave_road_pick_wait(void){
    if(!pick_pending)return;
    /* The pick takes a few ms; a slave that never answers is the same fault as a stuck road job. */
    for(uint32_t spins=0;job.done!=job.seq;++spins)
        if(spins>200000000u){fzx_slave_road_status=9;break;}
    pick_pending=0;
}
void fzx_slave_road_pick_start(void){
    fzx_slave_road_pick_wait();
    job.pick_only=1;
    BARRIER();
    job.seq=job.seq+1u;
    BARRIER();
    pick_pending=1;
    cpu_dual_slave_notify();
}
#endif
void fzx_slave_road_start(const fzx_slave_road_args_t *args){
#if ENG_PICK_AHEAD
    fzx_slave_road_pick_wait();
    job.pick_only=0;
#endif
    job.args=*args;
    BARRIER(); /* the complete payload is stored before the sequence number */
    job.seq=job.seq+1u;
    BARRIER(); /* and the sequence number before the notification */
    cpu_dual_slave_notify();
}

bool fzx_slave_road_finish(const volatile uint32_t *video_fields,uint32_t *count){
    const uint32_t seq=job.seq,f0=*video_fields;
    const uint16_t w0=cpu_frt_count_get();
    bool late=false;
    while(job.done!=seq){
        const uint32_t e=*video_fields-f0;
        if(e>LATE_FIELDS && !late){late=true;++fzx_slave_road_late;}
        if(e>TIMEOUT_FIELDS){fzx_slave_road_status=9;return false;}
    }
    BARRIER(); /* the slave's results are read after done */
    const uint32_t waited=(uint16_t)(cpu_frt_count_get()-w0);
    const uint32_t busy=job.ticks*job.ratio;
    ++fzx_slave_road_jobs;
    fzx_slave_road_wait_sum+=waited;if(waited>fzx_slave_road_wait_max)fzx_slave_road_wait_max=waited;
    fzx_slave_road_busy_sum+=busy;if(busy>fzx_slave_road_busy_max)fzx_slave_road_busy_max=busy;
    if(!fzx_slave_road_sp || job.sp<fzx_slave_road_sp)fzx_slave_road_sp=job.sp;
    cpu_cache_area_purge((void *)&fzx_road_stream_stats,sizeof(fzx_road_stream_stats));
    if(!guards_ok()){
        if(!fzx_slave_road_guard_trips)fzx_slave_road_guard_first_job=fzx_slave_road_jobs;
        ++fzx_slave_road_guard_trips;fzx_slave_road_status=7;
#if !FZX_SLAVE_ROAD_STACK_PROBE
        return false; /* fail-stop: the caller halts with the status */
#endif
    }
    if(!(fzx_slave_road_jobs&15u)){
        const uint32_t u=stack_unused();
        if(u<fzx_slave_road_stack_unused)fzx_slave_road_stack_unused=u;
    }
    *count=job.count;
    if(!job.ok){fzx_slave_road_status=4;return false;}
    return true;
}
