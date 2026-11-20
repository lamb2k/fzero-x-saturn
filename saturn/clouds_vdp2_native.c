#include "clouds_vdp2_native.h"
#include "sky_native.h"
#include "planes_native.h"
#include "sky_colors.h"
#include <string.h>

/* VDP2 layout (see generated/C5-CLOUDS-VDP2-PLAN.md). The floor owns A0 0..0xfff,
 * A1 0..0x5f (RP A) and 0x4000..0x7fff (map), all of B0 (coefficients) and the
 * W0 table at B1 0. Parameter B's table must sit at RP A + 0x80. */
#define CLOUD_CHARS VDP2_VRAM_ADDR(0,0x1000)
#define CLOUD_CHAR_NUMBER 0x80u
#define CLOUD_RP VDP2_VRAM_ADDR(1,0x80)
#define CLOUD_MAP VDP2_VRAM_ADDR(1,0x8000)
#define CLOUD_WINDOW VDP2_VRAM_ADDR(1,0xc000)
#define SKY_BACK VDP2_VRAM_ADDR(1,0xc400)
#define BAND_CRAM_FIRST 2u
#define BAND_MAX 64u

fzx_clouds_rbg_t fzx_clouds_rbg;
#if ENG_LOOK_CLOSE
extern volatile uint8_t fzx_look_closed; /* engine_main.c */
#endif
volatile uint32_t fzx_clouds_status,fzx_clouds_pending,fzx_clouds_commits;
volatile uint32_t fzx_clouds_degenerate,fzx_clouds_bands,fzx_clouds_sky_line_count;
static uint16_t sky_lines[224];
#if FZX_VDP1_DBANK
#include "sh2zam/vdp1_banks_experimental.h"
/* FZX_VDP1_DBANK: two banks of the staged tables (bank 1 in VDP1 VRAM, set by
 * main), queued in build order and uploaded oldest first; see floor_rbg_native.c. */
static fzx_clouds_rbg_t *clouds_stage[2]={&fzx_clouds_rbg,0};
static uint16_t *sky_stage[2]={sky_lines,0};
static unsigned clouds_bank;
static sh2z_vdp1_queue_t clouds_queue;
#if FZX_VDP1_DBANK
/* The VBlank-In present pops this queue inside the interrupt while the build
 * pushes from the main loop. sh2z_vdp1_queue_push is a read-modify-write of the
 * count and both slots, so a pop landing inside it loses an update: the count
 * drifts from the entries and a later build finds its bank "queued" (E1 fault 25
 * status 1 at tick 6095; E2's status 23). The push runs with interrupts masked.
 * FZX_DBANK_QUEUE_ATOMIC=0 is the control. */
#ifndef FZX_DBANK_QUEUE_ATOMIC
#define FZX_DBANK_QUEUE_ATOMIC 1
#endif
/* FZX_DBANK_QUEUE_WIDEN=N (control only): the same push, with N spins between
 * reading the count and storing it, so the race window is wide enough to hit in
 * seconds instead of once a race. */
#ifndef FZX_DBANK_QUEUE_WIDEN
#define FZX_DBANK_QUEUE_WIDEN 0
#endif
static bool queue_push(sh2z_vdp1_queue_t *q,uint32_t bank,uint32_t tag) {
#if FZX_DBANK_QUEUE_ATOMIC
    const uint8_t mask=cpu_intc_mask_get();cpu_intc_mask_set(15);
#endif
#if FZX_DBANK_QUEUE_WIDEN
    bool ok=false;
    if(tag && sh2z_vdp1_queue_free(q,bank)) {
        const uint32_t c=q->count;
        for(volatile uint32_t spin=FZX_DBANK_QUEUE_WIDEN;spin;--spin){}
        q->bank[c]=bank;q->tag[c]=tag;q->count=c+1u;ok=true;
    }
#else
    const bool ok=sh2z_vdp1_queue_push(q,bank,tag);
#endif
#if FZX_DBANK_QUEUE_ATOMIC
    cpu_intc_mask_set(mask);
#endif
    return ok;
}
#endif
void fzx_clouds_dbank(void *clouds_bank1,void *sky_bank1){clouds_stage[1]=clouds_bank1;sky_stage[1]=sky_bank1;sh2z_vdp1_queue_init(&clouds_queue);}
void fzx_clouds_dbank_select(unsigned bank){clouds_bank=bank&1u;}
#define SKY_LINES (sky_stage[clouds_bank])
#define CLOUDS_STAGE (*clouds_stage[clouds_bank])
#else
#define SKY_LINES sky_lines
#define CLOUDS_STAGE fzx_clouds_rbg
#endif
static uint8_t sky_keys[64];
#ifndef FZX_SKY_LINEAR_PICK
#define FZX_SKY_LINEAR_PICK 0
#endif
static unsigned band_count;
extern volatile uint32_t fzx_input_count;

static uint32_t vram_offset(uintptr_t address){return (uint32_t)(address-VDP2_VRAM_ADDR(0,0));}

/* Every VDP2 register and CRAM entry the cloud path changes is saved at load
 * and restored when the race scene stops (no cloud frame for two presented
 * frames), then re-applied by the next race frame. The other scenes then see
 * the VDP2 state the knob-off image has. */
static struct {
    uint16_t rpmd,rprctl,ktctl,ktaof,wpsy1,wpey1,wctlc,wctld,bgon,bktau,bktal;
    uint32_t lwta1;uint16_t cram[BAND_MAX];
    uint16_t mpofr_rb,pb_map[8];
} saved;
/* Rotation parameter B's map: MPOFR's RBMP field (bits 6..4) and the eight
 * MPABRB..MPOPRB words (0x60..0x6e), as the cloud cell format sets them. */
#define MPOFR_WORD (0x3eu/2u)
#define MPOFR_RBMP 0x0070u
#define PB_MAP_WORD (0x60u/2u)
static uint16_t cloud_mpofr_rb,cloud_pb_map[8];
static bool applied;
static unsigned idle_presents;
volatile uint32_t fzx_clouds_restores,fzx_clouds_applies;
static void saved_capture(void) {
    const vdp2_ioregs_t *r=vdp2_regs_get();
    saved.rpmd=r->rpmd;saved.rprctl=r->rprctl;saved.ktctl=r->ktctl;saved.ktaof=r->ktaof;
    saved.wpsy1=r->wpsy1;saved.wpey1=r->wpey1;saved.lwta1=r->lwta1;saved.wctlc=r->wctlc;saved.wctld=r->wctld;
    saved.bgon=r->bgon;saved.bktau=r->bktau;saved.bktal=r->bktal;
    const uint16_t *w=(const uint16_t *)r;
    saved.mpofr_rb=(uint16_t)(w[MPOFR_WORD]&MPOFR_RBMP);
    for(unsigned i=0;i<8u;++i)saved.pb_map[i]=w[PB_MAP_WORD+i];
    const volatile uint16_t *cram=(const volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0,0,BAND_CRAM_FIRST);
    for(unsigned i=0;i<BAND_MAX;++i)saved.cram[i]=cram[i];
}
static void apply(void) {
    vdp2_ioregs_t *r=vdp2_regs_get();
    uint16_t *w=(uint16_t *)r;
    w[MPOFR_WORD]=(uint16_t)((w[MPOFR_WORD]&~MPOFR_RBMP)|cloud_mpofr_rb);
    for(unsigned i=0;i<8u;++i)w[PB_MAP_WORD+i]=cloud_pb_map[i];
    /* Mode 3: the rotation-parameter window (W0, the floor span) selects A. */
    r->rpmd=3;
    r->rprctl=(uint16_t)((saved.rprctl&0x00ffu)|((saved.rprctl&0x0007u)<<8));
    r->ktctl=(uint16_t)((saved.ktctl&0x00ffu)|((saved.ktctl&0x001fu)<<8));
    r->ktaof=(uint16_t)((saved.ktaof&0x00ffu)|((saved.ktaof&0x0007u)<<8));
    r->wpsy1=0;r->wpey1=223;r->lwta1=0x80000000u|(vram_offset(CLOUD_WINDOW)>>1);
    /* RBG0 shows inside W0 or W1 (AND of the two outside areas). */
    r->wctlc=(uint16_t)((saved.wctlc&0xff00u)|0x8fu);
    r->wctld=(uint16_t)((saved.wctld&0xff00u)|0x03u);
    /* RBG0 transparency on (it was off): the floor tile is fully opaque, the
     * cloud tile's 0x0000 texels must show the back-screen sky. */
    r->bgon=(uint16_t)(r->bgon&~0x1000u);
    /* Back screen, one colour per line. */
    r->bktau=(uint16_t)(0x8000u|((vram_offset(SKY_BACK)>>17)&7u));
    r->bktal=(uint16_t)((vram_offset(SKY_BACK)>>1)&0xffffu);
    /* Band colours: the 64 source sky samples, written once (never per frame),
     * so CRAM stays stable through every presented frame. */
    volatile uint16_t *cram=(volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0,0,BAND_CRAM_FIRST);
    for(unsigned i=0;i<64u;++i)cram[i]=fzx_sky_colors[i];
    applied=true;++fzx_clouds_applies;
}
static void restore(void) {
    vdp2_ioregs_t *r=vdp2_regs_get();
    uint16_t *w=(uint16_t *)r;
    w[MPOFR_WORD]=(uint16_t)((w[MPOFR_WORD]&~MPOFR_RBMP)|saved.mpofr_rb);
    for(unsigned i=0;i<8u;++i)w[PB_MAP_WORD+i]=saved.pb_map[i];
    r->rpmd=saved.rpmd;r->rprctl=saved.rprctl;r->ktctl=saved.ktctl;r->ktaof=saved.ktaof;
    r->wpsy1=saved.wpsy1;r->wpey1=saved.wpey1;r->lwta1=saved.lwta1;
    r->wctlc=(uint16_t)((r->wctlc&0xff00u)|(saved.wctlc&0x00ffu));
    r->wctld=(uint16_t)((r->wctld&0xff00u)|(saved.wctld&0x00ffu));
    r->bgon=(uint16_t)((r->bgon&~0x1000u)|(saved.bgon&0x1000u));
    r->bktau=saved.bktau;r->bktal=saved.bktal;
    volatile uint16_t *cram=(volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0,0,BAND_CRAM_FIRST);
    for(unsigned i=0;i<BAND_MAX;++i)cram[i]=saved.cram[i];
    applied=false;++fzx_clouds_restores;
}

bool fzx_clouds_vdp2_load(const void *planes_vram) {
    if(!planes_vram){fzx_clouds_status=2;return false;}
    for(unsigned i=0;i<64u;++i) {
        const uint16_t c=fzx_sky_colors[i];
        sky_keys[i]=(uint8_t)((c&31u)+((c>>5)&31u)+((c>>10)&31u));
#if !FZX_SKY_LINEAR_PICK
        if(i && sky_keys[i]<sky_keys[i-1]){fzx_clouds_status=5;return false;}
#endif
    }
    /* Cloud-only pack, rebased: variant 0 (unfaded), level 0 = 64x32 RGB1555
     * row-major at offset 0. VDP2 RGB cells are 8x8 row-major, 128 bytes. */
    const volatile uint16_t *src=(const volatile uint16_t *)planes_vram;
    volatile uint16_t *chars=(volatile uint16_t *)CLOUD_CHARS;
    for(unsigned cy=0;cy<4;++cy)for(unsigned cx=0;cx<8;++cx)
        for(unsigned py=0;py<8;++py)for(unsigned px=0;px<8;++px)
            chars[(cy*8+cx)*64+py*8+px]=src[(cy*8+py)*64+cx*8+px];
    volatile uint32_t *map=(volatile uint32_t *)CLOUD_MAP;
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        map[y*64+x]=CLOUD_CHAR_NUMBER+((y&3u)*8u+(x&7u))*4u;
    memset((void *)CLOUD_RP,0,96);
    for(unsigned y=0;y<224;++y){((volatile uint16_t *)CLOUD_WINDOW)[y*2]=640;((volatile uint16_t *)CLOUD_WINDOW)[y*2+1]=0;}
    for(unsigned y=0;y<224;++y)((volatile uint16_t *)SKY_BACK)[y]=fzx_sky_colors[63];
    saved_capture();
    const vdp2_scrn_cell_format_t format={.scroll_screen=VDP2_SCRN_RBG0_PB,
        .ccc=VDP2_SCRN_CCC_RGB_32768,.char_size=VDP2_SCRN_CHAR_SIZE_1X1,.pnd_size=2,
        .aux_mode=VDP2_SCRN_AUX_MODE_0,.plane_size=VDP2_SCRN_PLANE_SIZE_1X1,.cpd_base=CLOUD_CHARS};
    const vdp2_scrn_rotation_map_t map_b={.single=true,.plane=CLOUD_MAP};
    vdp2_scrn_rotation_cell_format_set(&format,&map_b);
    /* Keep the cloud mapping the helper produced; apply/restore own these
     * words from here on (the helper is not called again). */
    {
        const uint16_t *w=(const uint16_t *)vdp2_regs_get();
        cloud_mpofr_rb=(uint16_t)(w[MPOFR_WORD]&MPOFR_RBMP);
        for(unsigned i=0;i<8u;++i)cloud_pb_map[i]=w[PB_MAP_WORD+i];
    }
    apply();
    fzx_clouds_status=1;return true;
}

#include "fzx_sky_lines_body.h"

typedef struct {vdp1_cmdt_t *commands;unsigned capacity,count;} submit_t;
static bool emit(void *context,const fzx_floor_polygon_t *q) {
    submit_t *s=context;
    if(s->count>=s->capacity || band_count>=BAND_MAX || q->band<1 || q->band>7)return false;
    int y=(q->y[0]+q->y[1]+q->y[2]+q->y[3])/4;
    if(y<0)y=0;
    if(y>223)y=223;
    const uint16_t sky=SKY_LINES[y];
    vdp1_cmdt_t *c=&s->commands[s->count++];memset(c,0,sizeof(*c));vdp1_cmdt_polygon_set(c);
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2};
    vdp1_cmdt_draw_mode_set(c,mode);
    if(q->band==7)c->cmd_colr=(uint16_t)(sky|0x8000u); /* opaque sky: the clouds are invisible here */
    else {
        /* The nearest resident sky sample: the source gradient's channel sum
         * rises monotonically (checked at load), so bisect on it. */
#if FZX_SKY_LINEAR_PICK
        /* A course whose gradient is not monotonic in brightness (a sunset): the
         * nearest sample by per-channel distance, over all 64. */
        unsigned best=0;uint32_t bd=~0u;
        for(unsigned i=0;i<64u;++i) {
            const uint16_t s=fzx_sky_colors[i];
            const int32_t dr=(int32_t)(sky&31u)-(int32_t)(s&31u),dg=(int32_t)((sky>>5)&31u)-(int32_t)((s>>5)&31u),
                          db=(int32_t)((sky>>10)&31u)-(int32_t)((s>>10)&31u);
            const uint32_t d=(uint32_t)((dr<0?-dr:dr)+(dg<0?-dg:dg)+(db<0?-db:db));
            if(d<bd){bd=d;best=i;}
        }
#else
        const uint32_t key=(sky&31u)+((sky>>5)&31u)+((sky>>10)&31u);
        unsigned lo=0,hi=63;
        while(lo<hi){const unsigned mid=(lo+hi)/2;if(sky_keys[mid]<key)lo=mid+1;else hi=mid;}
        const unsigned best=(lo>0 && key-sky_keys[lo-1]<sky_keys[lo]-key)?lo-1:lo;
#endif
        c->cmd_colr=(uint16_t)(0x4000u|(q->band<<11)|(BAND_CRAM_FIRST+best));
        ++band_count;
    }
    c->cmd_xa=q->x[0];c->cmd_ya=q->y[0];c->cmd_xb=q->x[1];c->cmd_yb=q->y[1];
    c->cmd_xc=q->x[2];c->cmd_yc=q->y[2];c->cmd_xd=q->x[3];c->cmd_yd=q->y[3];return true;
}
bool fzx_clouds_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,uint32_t *count) {
#if FZX_VDP1_DBANK
    if(!commands || capacity<2 || !view || !source || !count || fzx_clouds_status!=1 ||
       !clouds_stage[clouds_bank] || !sky_stage[clouds_bank] || !sh2z_vdp1_queue_free(&clouds_queue,clouds_bank))return false;
#else
    if(!commands || capacity<2 || !view || !source || !count || fzx_clouds_status!=1 || fzx_clouds_pending)return false;
#endif
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    /* The VDP1 sky used to cover the whole view each frame. Clear the view to
     * transparent instead (the floor's band-0 write), so no earlier frame or
     * screen shows through, then black side borders outside the user clip:
     * the back screen spans the full line. */
    unsigned borders=0;
    {
        vdp1_cmdt_t *c=&commands[1+borders++];memset(c,0,sizeof(*c));vdp1_cmdt_polygon_set(c);
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1};
        vdp1_cmdt_draw_mode_set(c,mode);c->cmd_colr=0;
        c->cmd_xa=(int16_t)view->clip[0];c->cmd_ya=(int16_t)view->clip[1];
        c->cmd_xb=(int16_t)(view->clip[2]-1);c->cmd_yb=(int16_t)view->clip[1];
        c->cmd_xc=(int16_t)(view->clip[2]-1);c->cmd_yc=(int16_t)(view->clip[3]-1);
        c->cmd_xd=(int16_t)view->clip[0];c->cmd_yd=(int16_t)(view->clip[3]-1);
    }
    const int16_t sides[2][2]={{0,(int16_t)(view->clip[0]-1)},{(int16_t)view->clip[2],319}};
    for(unsigned i=0;i<2;++i) {
        if(sides[i][1]<sides[i][0] || borders+2>=capacity)continue;
        vdp1_cmdt_t *c=&commands[1+borders++];memset(c,0,sizeof(*c));vdp1_cmdt_polygon_set(c);
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1};
        vdp1_cmdt_draw_mode_set(c,mode);c->cmd_colr=0x8000u;
        c->cmd_xa=sides[i][0];c->cmd_ya=0;c->cmd_xb=sides[i][1];c->cmd_yb=0;
        c->cmd_xc=sides[i][1];c->cmd_yc=223;c->cmd_xd=sides[i][0];c->cmd_yd=223;
    }
    submit_t submit={commands+1+borders,capacity-1-borders,0};band_count=0;
    /* The plane status words stay live for the capture harness: busy, and the
     * cloud stats in the plane stats block (draws = fade bands). */
    fzx_plane_busy=1;
#if ENG_LOOK_CLOSE
    /* Course look: inside a whole pipe span the clouds' stage is closed (every line
     * empty, no fade bands) instead of prepared; the sky lines stay. */
    if(fzx_look_closed&2u){for(unsigned y=0;y<224;++y){CLOUDS_STAGE.window[y][0]=640;CLOUDS_STAGE.window[y][1]=0;}CLOUDS_STAGE.visible_lines=0;CLOUDS_STAGE.degenerate=0;CLOUDS_STAGE.commands=0;}
    else
#endif
    if(!fzx_clouds_rbg_prepare(view,source,&CLOUDS_STAGE,emit,&submit)) {
        fzx_plane_stats.fault=CLOUDS_STAGE.fault;fzx_clouds_status=3;return false;
    }
    memset(&fzx_plane_stats,0,sizeof(fzx_plane_stats));
    fzx_plane_stats.draws=submit.count;fzx_plane_stats.hash=CLOUDS_STAGE.hash;fzx_plane_busy=0;
    fzx_clouds_degenerate+=CLOUDS_STAGE.degenerate;fzx_clouds_bands=submit.count;
#if FZX_VDP1_DBANK
    (void)queue_push(&clouds_queue,clouds_bank,fzx_input_count?fzx_input_count:1u);
#endif
    *count=submit.count+1+borders;fzx_clouds_pending=fzx_input_count?fzx_input_count:1;return true;
}
uint16_t fzx_clouds_upload_log[FZX_CLOUDS_UPLOAD_LOG];
volatile uint32_t fzx_clouds_upload_count,fzx_clouds_upload_max,fzx_clouds_upload_overruns;
void fzx_clouds_upload_record(uint16_t start,uint32_t began_in_blank) {
    const uint16_t ticks=(uint16_t)(cpu_frt_count_get()-start);
    const uint32_t ended_in_blank=vdp2_tvmd_vblank_in()?1u:0u;
    if(!began_in_blank || !ended_in_blank)++fzx_clouds_upload_overruns;
    if(ticks>fzx_clouds_upload_max)fzx_clouds_upload_max=ticks;
    if(fzx_clouds_upload_count<FZX_CLOUDS_UPLOAD_LOG)
        fzx_clouds_upload_log[fzx_clouds_upload_count]=(uint16_t)((ticks>0x3fffu?0x3fffu:ticks)|
            (began_in_blank?0x4000u:0u)|(ended_in_blank?0x8000u:0u));
    ++fzx_clouds_upload_count;
}
bool fzx_clouds_present(void) {
#if FZX_VDP1_DBANK
    if(!clouds_queue.count) {
#else
    if(!fzx_clouds_pending) {
#endif
        /* Not a race frame: after two in a row, hand VDP2 back unchanged. */
        if(applied && fzx_clouds_status==1 && ++idle_presents>=2)restore();
        return false;
    }
    idle_presents=0;
    if(!applied)apply();
#if FZX_VDP1_DBANK
    uint32_t bank=0,tick=0;(void)sh2z_vdp1_queue_pop(&clouds_queue,&bank,&tick);(void)tick;
    const fzx_clouds_rbg_t *stage=clouds_stage[bank];
    memcpy((void *)CLOUD_RP,&stage->rp,sizeof(stage->rp));
    memcpy((void *)CLOUD_WINDOW,stage->window,sizeof(stage->window));
    memcpy((void *)SKY_BACK,sky_stage[bank],sizeof(sky_lines));
    fzx_clouds_pending=sh2z_vdp1_queue_front(&clouds_queue);++fzx_clouds_commits;
#else
    memcpy((void *)CLOUD_RP,&fzx_clouds_rbg.rp,sizeof(fzx_clouds_rbg.rp));
    memcpy((void *)CLOUD_WINDOW,fzx_clouds_rbg.window,sizeof(fzx_clouds_rbg.window));
    memcpy((void *)SKY_BACK,sky_lines,sizeof(sky_lines));
    fzx_clouds_pending=0;++fzx_clouds_commits;
#endif
    return true;
}
