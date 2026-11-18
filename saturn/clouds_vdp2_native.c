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
volatile uint32_t fzx_clouds_status,fzx_clouds_pending,fzx_clouds_commits;
volatile uint32_t fzx_clouds_degenerate,fzx_clouds_bands,fzx_clouds_sky_line_count;
static uint16_t sky_lines[224];
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
} saved;
static bool applied;
static unsigned idle_presents;
volatile uint32_t fzx_clouds_restores,fzx_clouds_applies;
static void saved_capture(void) {
    const vdp2_ioregs_t *r=vdp2_regs_get();
    saved.rpmd=r->rpmd;saved.rprctl=r->rprctl;saved.ktctl=r->ktctl;saved.ktaof=r->ktaof;
    saved.wpsy1=r->wpsy1;saved.wpey1=r->wpey1;saved.lwta1=r->lwta1;saved.wctlc=r->wctlc;saved.wctld=r->wctld;
    saved.bgon=r->bgon;saved.bktau=r->bktau;saved.bktal=r->bktal;
    const volatile uint16_t *cram=(const volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0,0,BAND_CRAM_FIRST);
    for(unsigned i=0;i<BAND_MAX;++i)saved.cram[i]=cram[i];
}
static void apply(void) {
    vdp2_ioregs_t *r=vdp2_regs_get();
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
    apply();
    fzx_clouds_status=1;return true;
}

typedef struct {uint8_t set[224];int center;} lines_t;
static uint16_t blend(uint16_t a,uint16_t b,int32_t t) {
    uint16_t out=0x8000;
    for(unsigned shift=0;shift<15;shift+=5) {
        const int32_t ca=(a>>shift)&31,cb=(b>>shift)&31;
        int32_t c=ca+(int32_t)(((int64_t)(cb-ca)*t+32768)>>16);
        if(c<0)c=0;
        if(c>31)c=31;
        out|=(uint16_t)(c<<shift);
    }
    return out;
}
static bool line_emit(void *context,const fzx_sky_polygon_t *p) {
    /* The polygon's span on the centre column: intersect its edges with
     * x = centre once, then step the colour down the lines (no divide per line). */
    lines_t *l=context;
    const int32_t cx=l->center;
    int32_t ya=INT32_MAX,yb=INT32_MIN;uint16_t ca=0,cb=0;
    for(unsigned i=0;i<4;++i) {
        const unsigned j=(i+1)&3;
        const int32_t x0=p->x[i],x1=p->x[j];
        if((cx<x0 && cx<x1) || (cx>x0 && cx>x1))continue;
        int32_t y;uint16_t c;
        if(x0==x1) {
            for(unsigned k=0;k<2;++k){const unsigned v=k?j:i;
                if(p->y[v]<ya){ya=p->y[v];ca=p->color[v];}
                if(p->y[v]>yb){yb=p->y[v];cb=p->color[v];}}
            continue;
        }
        const int32_t t=(int32_t)(((cx-x0)*65536)/(x1-x0));
        y=p->y[i]+(int32_t)(((int64_t)(p->y[j]-p->y[i])*t+32768)>>16);c=blend(p->color[i],p->color[j],t);
        if(y<ya){ya=y;ca=c;}
        if(y>yb){yb=y;cb=c;}
    }
    if(ya>yb)return true;
    int32_t rgb[3],step[3];
    const int32_t span=yb>ya?yb-ya:1;
    for(unsigned k=0;k<3;++k){rgb[k]=((ca>>(k*5))&31)<<16;step[k]=((((cb>>(k*5))&31)<<16)-rgb[k])/span;}
    for(int32_t y=ya;y<=yb;++y) {
        if(y>=0 && y<224) {
            uint16_t c=0x8000;
            for(unsigned k=0;k<3;++k){int32_t v=(rgb[k]+32768)>>16;if(v<0)v=0;if(v>31)v=31;c|=(uint16_t)(v<<(k*5));}
            sky_lines[y]=c;l->set[y]=1;
        }
        for(unsigned k=0;k<3;++k)rgb[k]+=step[k];
    }
    return true;
}
bool fzx_clouds_sky_lines(const fzx_view_t *view,const RefBackdrop *source) {
    if(!view || !source || fzx_clouds_status!=1)return false;
    lines_t l;memset(&l,0,sizeof(l));l.center=(view->clip[0]+view->clip[2])/2;
    if(!fzx_sky_draw_bands(view,&source->vertices[8],fzx_sky_colors,
#if FZX_SKY_SIMPLIFY
                     fzx_sky_breaks,sizeof(fzx_sky_breaks),
#else
                     NULL,0,
#endif
                     FZX_SKY_MAX_COMMANDS,line_emit,&l,&fzx_sky_stats)){fzx_clouds_status=4;return false;}
    /* Lines the sky does not reach (behind the floor) take the nearest sky line. */
    int last=-1;unsigned set=0;
    for(int y=0;y<224;++y)if(l.set[y]){
        ++set;
        if(last<0)for(int k=0;k<y;++k)sky_lines[k]=sky_lines[y];
        else for(int k=last+1;k<y;++k)sky_lines[k]=sky_lines[last];
        last=y;
    }
    if(last<0)for(int y=0;y<224;++y)sky_lines[y]=fzx_sky_colors[63];
    else for(int y=last+1;y<224;++y)sky_lines[y]=sky_lines[last];
    /* Outside the view the screen stays black, as with the VDP1 sky. */
    for(int y=0;y<224;++y)if(y<view->clip[1] || y>=view->clip[3])sky_lines[y]=0;
    fzx_clouds_sky_line_count=set;
    return true;
}

typedef struct {vdp1_cmdt_t *commands;unsigned capacity,count;} submit_t;
static bool emit(void *context,const fzx_floor_polygon_t *q) {
    submit_t *s=context;
    if(s->count>=s->capacity || band_count>=BAND_MAX || q->band<1 || q->band>7)return false;
    int y=(q->y[0]+q->y[1]+q->y[2]+q->y[3])/4;
    if(y<0)y=0;
    if(y>223)y=223;
    const uint16_t sky=sky_lines[y];
    vdp1_cmdt_t *c=&s->commands[s->count++];memset(c,0,sizeof(*c));vdp1_cmdt_polygon_set(c);
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2};
    vdp1_cmdt_draw_mode_set(c,mode);
    if(q->band==7)c->cmd_colr=(uint16_t)(sky|0x8000u); /* opaque sky: the clouds are invisible here */
    else {
        /* The nearest of the 64 resident sky samples. */
        unsigned best=0;int32_t best_d=INT32_MAX;
        for(unsigned i=0;i<64u;++i) {
            int32_t d=0;
            for(unsigned k=0;k<15;k+=5){const int32_t e=(int32_t)((sky>>k)&31u)-(int32_t)((fzx_sky_colors[i]>>k)&31u);d+=e*e;}
            if(d<best_d){best_d=d;best=i;}
        }
        c->cmd_colr=(uint16_t)(0x4000u|(q->band<<11)|(BAND_CRAM_FIRST+best));
        ++band_count;
    }
    c->cmd_xa=q->x[0];c->cmd_ya=q->y[0];c->cmd_xb=q->x[1];c->cmd_yb=q->y[1];
    c->cmd_xc=q->x[2];c->cmd_yc=q->y[2];c->cmd_xd=q->x[3];c->cmd_yd=q->y[3];return true;
}
bool fzx_clouds_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,uint32_t *count) {
    if(!commands || capacity<2 || !view || !source || !count || fzx_clouds_status!=1 || fzx_clouds_pending)return false;
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
    if(!fzx_clouds_rbg_prepare(view,source,&fzx_clouds_rbg,emit,&submit)) {
        fzx_plane_stats.fault=fzx_clouds_rbg.fault;fzx_clouds_status=3;return false;
    }
    memset(&fzx_plane_stats,0,sizeof(fzx_plane_stats));
    fzx_plane_stats.draws=submit.count;fzx_plane_stats.hash=fzx_clouds_rbg.hash;fzx_plane_busy=0;
    fzx_clouds_degenerate+=fzx_clouds_rbg.degenerate;fzx_clouds_bands=submit.count;
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
    if(!fzx_clouds_pending) {
        /* Not a race frame: after two in a row, hand VDP2 back unchanged. */
        if(applied && fzx_clouds_status==1 && ++idle_presents>=2)restore();
        return false;
    }
    idle_presents=0;
    if(!applied)apply();
    memcpy((void *)CLOUD_RP,&fzx_clouds_rbg.rp,sizeof(fzx_clouds_rbg.rp));
    memcpy((void *)CLOUD_WINDOW,fzx_clouds_rbg.window,sizeof(fzx_clouds_rbg.window));
    memcpy((void *)SKY_BACK,sky_lines,sizeof(sky_lines));
    fzx_clouds_pending=0;++fzx_clouds_commits;
    return true;
}
