/* FZX_SIM_PIPELINE=1: slave SH-2 job channel (see simpipe_dual.h). The job
 * record is uncached; the slave purges its cache before each job and the
 * master purges its own after collecting one, so each CPU reads the other's
 * write-through stores from memory. */
#include "simpipe_dual.h"

volatile uint32_t fzx_simpipe_dual_jobs,fzx_simpipe_dual_late;
volatile uint32_t fzx_simpipe_dual_wait_sum,fzx_simpipe_dual_wait_max;
volatile uint32_t fzx_simpipe_dual_busy_sum,fzx_simpipe_dual_busy_max;
volatile uint32_t fzx_simpipe_dual_stack_unused,fzx_simpipe_dual_status;
volatile uint32_t fzx_simpipe_dual_guard_trips,fzx_simpipe_dual_sp;

typedef struct {
    uint32_t seq,done,ticks,frt_ready,ratio,sp,outstanding;
    fzx_simpipe_job_fn fn;
    void *arg;
} job_t;
/* Linked at 0x2xxxxxxx (libyaul .uncached): both CPUs read memory. */
static volatile job_t job __uncached;

/* The slave stack reservation of slave_road.c (Makefile: IP_SLAVE_STACK_ADDR
 * 0x06004000 under this knob): the 8 KB IP.BIN area, free after boot. Guard
 * words at the floor; the rest painted up to 512 bytes below the top. */
enum {STACK_TOP=0x06004000u,STACK_FLOOR=0x06002000u,GUARD_BYTES=32u,
      STACK_PAINT_END=0x06003E00u,PAINT=0x5a17c0deu,GUARD=0x51a7e5afu,
      LATE_FIELDS=4u,TIMEOUT_FIELDS=120u};
#define UNCACHED32(a) ((volatile uint32_t *)(0x20000000u|(uintptr_t)(a)))
/* Compiler barrier; the SH-2 retires loads and stores in program order. */
#define BARRIER() __asm__ volatile("" ::: "memory")

static void slave_entry(void){
    cpu_cache_purge();
    if(!job.frt_ready){cpu_frt_init(CPU_FRT_CLOCK_DIV_128);job.frt_ready=1;}
    BARRIER(); /* the job is read after the notification */
    const fzx_simpipe_job_fn fn=job.fn;void *const arg=job.arg;
    uint32_t marker=0;
    job.sp=(uint32_t)(uintptr_t)&marker;
    const uint16_t t0=cpu_frt_count_get();
    fn(arg);
    job.ticks=(uint16_t)(cpu_frt_count_get()-t0);
    BARRIER(); /* every result the job stored precedes done */
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

void fzx_simpipe_dual_init(void){
    if((uintptr_t)cpu_dual_slave_stack_get()!=STACK_TOP){fzx_simpipe_dual_status=8;return;}
    for(uintptr_t p=STACK_FLOOR;p<STACK_FLOOR+GUARD_BYTES;p+=4u)*UNCACHED32(p)=GUARD;
    for(uintptr_t p=STACK_FLOOR+GUARD_BYTES;p<STACK_PAINT_END;p+=4u)*UNCACHED32(p)=PAINT;
    /* FRT ratio: the master counts at /32, the slave at /128. */
    job.ratio=4u;
    job.seq=0;job.done=0;job.frt_ready=0;job.sp=0;job.outstanding=0;
    fzx_simpipe_dual_jobs=0;fzx_simpipe_dual_late=0;
    fzx_simpipe_dual_wait_sum=0;fzx_simpipe_dual_wait_max=0;
    fzx_simpipe_dual_busy_sum=0;fzx_simpipe_dual_busy_max=0;
    fzx_simpipe_dual_guard_trips=0;fzx_simpipe_dual_sp=0;
    fzx_simpipe_dual_stack_unused=STACK_PAINT_END-(STACK_FLOOR+GUARD_BYTES);
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(slave_entry);
    fzx_simpipe_dual_status=1;
}

bool fzx_simpipe_dual_start(fzx_simpipe_job_fn fn,void *arg){
    if(job.outstanding){fzx_simpipe_dual_status=10;return false;}
    job.fn=fn;job.arg=arg;
    BARRIER(); /* the complete job precedes the sequence number */
    job.seq=job.seq+1u;job.outstanding=1;
    BARRIER(); /* and the sequence number precedes the notification */
    cpu_dual_slave_notify();
    return true;
}

bool fzx_simpipe_dual_outstanding(void){return job.outstanding!=0;}

bool fzx_simpipe_dual_collect(const volatile uint32_t *video_fields){
    if(!job.outstanding)return true;
    const uint32_t seq=job.seq,f0=*video_fields;
    const uint16_t w0=cpu_frt_count_get();
    bool late=false;
    while(job.done!=seq){
        const uint32_t e=*video_fields-f0;
        if(e>LATE_FIELDS && !late){late=true;++fzx_simpipe_dual_late;}
        if(e>TIMEOUT_FIELDS){fzx_simpipe_dual_status=9;return false;}
    }
    BARRIER(); /* the slave's results are read after done */
    job.outstanding=0;
    cpu_cache_purge(); /* the master's cached lines of slave-written memory are stale */
    const uint32_t waited=(uint16_t)(cpu_frt_count_get()-w0);
    const uint32_t busy=job.ticks*job.ratio;
    ++fzx_simpipe_dual_jobs;
    fzx_simpipe_dual_wait_sum+=waited;if(waited>fzx_simpipe_dual_wait_max)fzx_simpipe_dual_wait_max=waited;
    fzx_simpipe_dual_busy_sum+=busy;if(busy>fzx_simpipe_dual_busy_max)fzx_simpipe_dual_busy_max=busy;
    if(!fzx_simpipe_dual_sp || job.sp<fzx_simpipe_dual_sp)fzx_simpipe_dual_sp=job.sp;
    if(!guards_ok()){++fzx_simpipe_dual_guard_trips;fzx_simpipe_dual_status=7;return false;}
    if(!(fzx_simpipe_dual_jobs&15u)){
        const uint32_t u=stack_unused();
        if(u<fzx_simpipe_dual_stack_unused)fzx_simpipe_dual_stack_unused=u;
    }
    return true;
}
