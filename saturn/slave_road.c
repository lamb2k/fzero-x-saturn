/* FZX_SLAVE_ROAD=1: the slave SH-2 runs fzx_road_stream_native() for the
 * master (generated/SLAVE-ROAD-PLAN.md). The job record is uncached; the
 * slave purges its cache before each job; the master never reads the road
 * commands (VDP1 list DMA reads memory) and purges the statistics it may read. */
#include "slave_road.h"

volatile uint32_t fzx_slave_road_jobs,fzx_slave_road_late;
volatile uint32_t fzx_slave_road_wait_sum,fzx_slave_road_wait_max;
volatile uint32_t fzx_slave_road_busy_sum,fzx_slave_road_busy_max;
volatile uint32_t fzx_slave_road_stack_unused,fzx_slave_road_status;

typedef struct {
    uint32_t seq,done,ok,count,ticks,frt_ready,ratio;
    fzx_slave_road_args_t args;
} job_t;
/* Linked at 0x2xxxxxxx (libyaul .uncached): both CPUs read memory. */
static volatile job_t job __uncached;

/* libyaul's slave stack (IP_SLAVE_STACK_ADDR 0x06001E00) grows down toward
 * 0x06001000; the master paints the lower 3 KB and measures what the slave
 * never touched. The top 512 bytes stay unpainted (the polling loop's frame). */
enum {STACK_LOW=0x06001000u,STACK_PAINT_END=0x06001C00u,PAINT=0x5a17c0deu,
      LATE_FIELDS=4u,TIMEOUT_FIELDS=120u};
#define UNCACHED32(a) ((volatile uint32_t *)(0x20000000u|(uintptr_t)(a)))

static void slave_entry(void){
    cpu_cache_purge();
    if(!job.frt_ready){cpu_frt_init(CPU_FRT_CLOCK_DIV_128);job.frt_ready=1;}
    const fzx_slave_road_args_t a=*(const fzx_slave_road_args_t *)&job.args;
    uint32_t n=0;
    const uint16_t t0=cpu_frt_count_get();
    const bool ok=fzx_road_stream_native(a.commands,a.capacity,a.view,a.eye,a.plan,a.pa,a.pb,a.pixels,a.shades,&n);
    job.ticks=(uint16_t)(cpu_frt_count_get()-t0);
    job.count=n;job.ok=ok;
    job.done=job.seq;
}

static uint32_t stack_unused(void){
    uintptr_t p=STACK_LOW;
    while(p<STACK_PAINT_END && *UNCACHED32(p)==PAINT)p+=4u;
    return (uint32_t)(p-STACK_LOW);
}

void fzx_slave_road_init(void){
    for(uintptr_t p=STACK_LOW;p<STACK_PAINT_END;p+=4u)*UNCACHED32(p)=PAINT;
    /* Slave ticks are DIV_128; scale to the master's divider (TCR CKS). */
    const uint8_t cks=*(volatile uint8_t *)0xfffffe92u&3u;
    job.ratio=cks==0?16u:cks==1?4u:1u;
    job.seq=0;job.done=0;job.frt_ready=0;
    fzx_slave_road_jobs=0;fzx_slave_road_late=0;
    fzx_slave_road_wait_sum=0;fzx_slave_road_wait_max=0;
    fzx_slave_road_busy_sum=0;fzx_slave_road_busy_max=0;
    fzx_slave_road_stack_unused=STACK_PAINT_END-STACK_LOW;
    cpu_dual_comm_mode_set(CPU_DUAL_ENTRY_POLLING);
    cpu_dual_slave_set(slave_entry);
    fzx_slave_road_status=1;
}

void fzx_slave_road_start(const fzx_slave_road_args_t *args){
    *(fzx_slave_road_args_t *)&job.args=*args;
    job.seq=job.seq+1u;
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
    const uint32_t waited=(uint16_t)(cpu_frt_count_get()-w0);
    const uint32_t busy=job.ticks*job.ratio;
    ++fzx_slave_road_jobs;
    fzx_slave_road_wait_sum+=waited;if(waited>fzx_slave_road_wait_max)fzx_slave_road_wait_max=waited;
    fzx_slave_road_busy_sum+=busy;if(busy>fzx_slave_road_busy_max)fzx_slave_road_busy_max=busy;
    cpu_cache_area_purge((void *)&fzx_road_stream_stats,sizeof(fzx_road_stream_stats));
    if(!(fzx_slave_road_jobs&15u)){
        const uint32_t u=stack_unused();
        if(u<fzx_slave_road_stack_unused)fzx_slave_road_stack_unused=u;
    }
    *count=job.count;
    if(!job.ok)fzx_slave_road_status=4;
    return job.ok!=0;
}
