#include "sky_native.h"
#include "sky_colors.h"
#include <mm/tlsf.h>
#include <string.h>
_Static_assert(!FZX_SKY_FIXTURE,"Original-race sky requires local source art");
RefBackdrop fzx_source_backdrop;
fzx_sky_stats_t fzx_sky_stats;
volatile uint32_t fzx_sky_status,fzx_sky_ticks,fzx_sky_count_max;
volatile uint32_t fzx_sky_busy;
volatile uint32_t fzx_sky_stack_start,fzx_sky_stack_end;
volatile uint32_t fzx_sky_heap_prefix,fzx_sky_heap_before,fzx_sky_heap_after;
extern uint32_t __end[];
static uint32_t heap_prefix_hash(uintptr_t begin,uintptr_t end) {
    uint32_t hash=2166136261u;
    for(const volatile uint8_t *p=(const volatile uint8_t *)begin;(uintptr_t)p<end;++p)
        hash=(hash^*p)*16777619u;
    return hash;
}
void fzx_sky_stack_paint(void) {
    uintptr_t sp;
    __asm__ volatile("mov r15,%0":"=r"(sp));
    /* Yaul initializes a user TLSF pool at __end before main. Preserve its
     * control structure and the first free block's header/list pointers.
     * The race uses the separate SDK private pool, but diagnostics must not
     * corrupt the user allocator even when no user allocation is requested. */
    const uintptr_t heap=(uintptr_t)__end;
    fzx_sky_heap_prefix=(uint32_t)(tlsf_size()+tlsf_pool_overhead()+tlsf_block_size_min());
    const uintptr_t start=(heap+fzx_sky_heap_prefix+3u)&~3u,end=(sp-1024u)&~3u;
    if(start>=end){fzx_sky_status=6;for(;;) {}}
    fzx_sky_stack_start=(uint32_t)start;fzx_sky_stack_end=(uint32_t)end;
    fzx_sky_heap_before=heap_prefix_hash(heap,start);
    /* One early fill only; no per-frame scanner distorts the cost samples.
     * Keep the active frame/interrupt margin intact above this range. */
    for(volatile uint32_t *p=(volatile uint32_t *)start;(uintptr_t)p<end;++p)*p=0xa55aa55au;
    fzx_sky_heap_after=heap_prefix_hash(heap,start);
    if(fzx_sky_heap_before!=fzx_sky_heap_after){fzx_sky_status=6;for(;;) {}}
}
static bool ready;
typedef struct {vdp1_cmdt_t *commands;uint16_t *shades;unsigned count;} submit_t;

void fzx_sky_restart(void){ready=false;fzx_sky_status=0;}
bool fzx_sky_publish(void) {
    fzx_sky_busy=1;
    /* Sky is independent of cloud velocity and scroll. These explicit zero
     * inputs neither consume game RNG nor claim cloud lifecycle integration. */
    if(!ready){if(ref_backdrop_init(0.0f,0.0f)){fzx_sky_status=2;return false;}ready=true;}
    ref_backdrop_tick(&fzx_source_backdrop,0,1);
    if(fzx_source_backdrop.faults || fzx_source_backdrop.count!=20){fzx_sky_status=3;return false;}
    fzx_sky_status=1;fzx_sky_busy=0;return true;
}
static bool emit(void *context,const fzx_sky_polygon_t *p) {
    submit_t *s=context;vdp1_cmdt_t *cmd=&s->commands[s->count];
    uint16_t *colors=&s->shades[s->count*4u];
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,
        .cc_mode=VDP1_CMDT_CC_GOURAUD};
    memset(cmd,0,sizeof(*cmd));vdp1_cmdt_polygon_set(cmd);vdp1_cmdt_draw_mode_set(cmd,mode);
    const rgb1555_t base={.raw=0xc210};vdp1_cmdt_color_set(cmd,base);
    cmd->cmd_xa=p->x[0];cmd->cmd_ya=p->y[0];cmd->cmd_xb=p->x[1];cmd->cmd_yb=p->y[1];
    cmd->cmd_xc=p->x[2];cmd->cmd_yc=p->y[2];cmd->cmd_xd=p->x[3];cmd->cmd_yd=p->y[3];
    memcpy(colors,p->color,8);
    cmd->cmd_grda=(uint16_t)(((uintptr_t)colors&0x7ffffu)>>3);
    ++s->count;return true;
}
bool fzx_sky_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                    void *shade_bank,uint32_t *count) {
    const uint16_t start=cpu_frt_count_get();
    if(!commands || !shade_bank || ((uintptr_t)shade_bank&7u) || !count || fzx_sky_status!=1)return false;
    if(capacity>FZX_SKY_MAX_COMMANDS)capacity=FZX_SKY_MAX_COMMANDS;
    fzx_sky_busy=1;
    submit_t submit={commands,shade_bank,0};
    if(!fzx_sky_draw_bands(view,&fzx_source_backdrop.vertices[8],fzx_sky_colors,
#if FZX_SKY_SIMPLIFY
                     fzx_sky_breaks,sizeof(fzx_sky_breaks),
#else
                     NULL,0,
#endif
                     capacity,
                     emit,&submit,&fzx_sky_stats)){fzx_sky_status=4;return false;}
    *count=submit.count;if(submit.count>fzx_sky_count_max)fzx_sky_count_max=submit.count;
    fzx_sky_ticks=(uint16_t)(cpu_frt_count_get()-start);fzx_sky_busy=0;return true;
}
