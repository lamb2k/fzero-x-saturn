#include "clouds_vdp2_native.h"
#include "sky_native.h"
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
static uint16_t band_colors[BAND_MAX];
static unsigned band_count;
extern volatile uint32_t fzx_input_count;

static uint32_t vram_offset(uintptr_t address){return (uint32_t)(address-VDP2_VRAM_ADDR(0,0));}

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
    const vdp2_scrn_cell_format_t format={.scroll_screen=VDP2_SCRN_RBG0_PB,
        .ccc=VDP2_SCRN_CCC_RGB_32768,.char_size=VDP2_SCRN_CHAR_SIZE_1X1,.pnd_size=2,
        .aux_mode=VDP2_SCRN_AUX_MODE_0,.plane_size=VDP2_SCRN_PLANE_SIZE_1X1,.cpd_base=CLOUD_CHARS};
    const vdp2_scrn_rotation_map_t map_b={.single=true,.plane=CLOUD_MAP};
    vdp2_scrn_rotation_cell_format_set(&format,&map_b);
    vdp2_ioregs_t *r=vdp2_regs_get();
    /* Mode 3: the rotation-parameter window (W0, the floor span) selects A. */
    r->rpmd=3;
    r->rprctl=(uint16_t)((r->rprctl&0x00ffu)|((r->rprctl&0x0007u)<<8));
    r->ktctl=(uint16_t)((r->ktctl&0x00ffu)|((r->ktctl&0x001fu)<<8));
    r->ktaof=(uint16_t)((r->ktaof&0x00ffu)|((r->ktaof&0x0007u)<<8));
    r->wpsy1=0;r->wpey1=223;r->lwta1=0x80000000u|(vram_offset(CLOUD_WINDOW)>>1);
    /* RBG0 shows inside W0 or W1 (AND of the two outside areas). */
    r->wctlc=(uint16_t)((r->wctlc&0xff00u)|0x8fu);
    r->wctld=(uint16_t)((r->wctld&0xff00u)|0x03u);
    /* Back screen, one colour per line. */
    r->bktau=(uint16_t)(0x8000u|((vram_offset(SKY_BACK)>>17)&7u));
    r->bktal=(uint16_t)((vram_offset(SKY_BACK)>>1)&0xffffu);
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
    lines_t *l=context;
    int ymin=224,ymax=-1;
    for(unsigned i=0;i<4;++i){if(p->y[i]<ymin)ymin=p->y[i];if(p->y[i]>ymax)ymax=p->y[i];}
    if(ymin<0)ymin=0;
    if(ymax>223)ymax=223;
    for(int y=ymin;y<=ymax;++y) {
        int32_t xl=INT32_MAX,xr=INT32_MIN;uint16_t cl=0,cr=0;
        for(unsigned i=0;i<4;++i) {
            const unsigned j=(i+1)&3;
            const int32_t y0=p->y[i],y1=p->y[j];
            if(y0==y1){
                if(y0!=y)continue;
                const int32_t xs[2]={p->x[i],p->x[j]};const uint16_t cs[2]={p->color[i],p->color[j]};
                for(unsigned k=0;k<2;++k){if(xs[k]*65536<xl){xl=xs[k]*65536;cl=cs[k];}if(xs[k]*65536>xr){xr=xs[k]*65536;cr=cs[k];}}
                continue;
            }
            const int32_t lo=y0<y1?y0:y1,hi=y0<y1?y1:y0;
            if(y<lo || y>hi)continue;
            const int32_t t=(int32_t)(((int64_t)(y-y0)*65536)/(y1-y0));
            const int32_t x=p->x[i]*65536+(int32_t)(((int64_t)(p->x[j]-p->x[i])*t));
            const uint16_t c=blend(p->color[i],p->color[j],t);
            if(x<xl){xl=x;cl=c;}
            if(x>xr){xr=x;cr=c;}
        }
        if(xl>xr)continue;
        const int32_t cx=l->center*65536;
        if(cx<xl-65536 || cx>xr+65536)continue;
        const int32_t t=xr>xl?(int32_t)(((int64_t)(cx-xl)*65536)/(xr-xl)):0;
        sky_lines[y]=blend(cl,cr,t<0?0:t>65536?65536:t);l->set[y]=1;
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
        band_colors[band_count]=sky;
        c->cmd_colr=(uint16_t)(0x4000u|(q->band<<11)|(BAND_CRAM_FIRST+band_count));
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
    submit_t submit={commands+1,capacity-1,0};band_count=0;
    if(!fzx_clouds_rbg_prepare(view,source,&fzx_clouds_rbg,emit,&submit)){fzx_clouds_status=3;return false;}
    fzx_clouds_degenerate+=fzx_clouds_rbg.degenerate;fzx_clouds_bands=submit.count;
    *count=submit.count+1;fzx_clouds_pending=fzx_input_count?fzx_input_count:1;return true;
}
void fzx_clouds_present(void) {
    if(!fzx_clouds_pending)return;
    memcpy((void *)CLOUD_RP,&fzx_clouds_rbg.rp,sizeof(fzx_clouds_rbg.rp));
    memcpy((void *)CLOUD_WINDOW,fzx_clouds_rbg.window,sizeof(fzx_clouds_rbg.window));
    memcpy((void *)SKY_BACK,sky_lines,sizeof(sky_lines));
    volatile uint16_t *cram=(volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0,0,BAND_CRAM_FIRST);
    for(unsigned i=0;i<band_count;++i)cram[i]=band_colors[i];
    fzx_clouds_pending=0;++fzx_clouds_commits;
}
