/* The per-line back-screen sky colours of the VDP2 cloud path, moved verbatim
 * out of saturn/clouds_vdp2_native.c so HOST tools build the same code
 * (tools/e0_vdp2_host.c). Not a normal header: include it once, in one
 * translation unit, after defining or declaring what it uses:
 *   SKY_LINES                   the uint16_t[224] output (an lvalue expression)
 *   fzx_sky_colors[64], fzx_sky_breaks (with FZX_SKY_SIMPLIFY), FZX_SKY_MAX_COMMANDS
 *   fzx_sky_stats, fzx_clouds_status, fzx_clouds_sky_line_count
 * It defines lines_t, blend(), line_emit() and fzx_clouds_sky_lines(). */
#ifndef FZX_SKY_LINES_BODY_H
#define FZX_SKY_LINES_BODY_H
#include <stdint.h>
#include <string.h>
#include "fzx_sky.h"

typedef struct {uint8_t set[224];int center;} lines_t;
/* Engine builds only (FZX_ENGINE): the legacy recipes keep their original code, byte for byte. */
#if defined(FZX_ENGINE) && FZX_ENGINE
/* C division (truncating) of 32-bit values on the SH-2 64/32 divider; the quotient always fits. */
static int32_t sky_div(int32_t n,int32_t d) {
#if defined(__sh__)
    if(d<0){d=-d;n=-n;}
    if(d>0) {
        *(volatile uint32_t *)0xffffff00u=(uint32_t)d;
        *(volatile uint32_t *)0xffffff10u=n<0?0xffffffffu:0u;
        *(volatile uint32_t *)0xffffff14u=(uint32_t)n;
        return (int32_t)*(volatile uint32_t *)0xffffff14u;
    }
#endif
    return n/d;
}
#else
#define sky_div(n,d) ((n)/(d))
#endif
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
        const int32_t t=sky_div((cx-x0)*65536,x1-x0);
        y=p->y[i]+(int32_t)(((int64_t)(p->y[j]-p->y[i])*t+32768)>>16);c=blend(p->color[i],p->color[j],t);
        if(y<ya){ya=y;ca=c;}
        if(y>yb){yb=y;cb=c;}
    }
    if(ya>yb)return true;
    int32_t rgb[3],step[3];
    const int32_t span=yb>ya?yb-ya:1;
#if defined(FZX_ENGINE) && FZX_ENGINE
    for(unsigned k=0;k<3;++k){rgb[k]=((ca>>(k*5))&31)<<16;step[k]=sky_div((((cb>>(k*5))&31)<<16)-rgb[k],span);}
    /* The channels run between the two end colours (steps truncate toward zero, so no value leaves [0,31]): the rows
     * on screen, from the first of them, with the rounding term added once. */
    {
        const int32_t y0=ya<0?0:ya,y1=yb>223?223:yb;
        if(y0<=y1) {
            const int32_t skip=y0-ya;
            int32_t r=rgb[0]+step[0]*skip+32768,g=rgb[1]+step[1]*skip+32768,b=rgb[2]+step[2]*skip+32768;
            uint16_t *const out=SKY_LINES;uint8_t *const set=l->set;
            for(int32_t y=y0;y<=y1;++y) {
                out[y]=(uint16_t)(0x8000u|(uint32_t)(r>>16)|((uint32_t)(g>>16)<<5)|((uint32_t)(b>>16)<<10));
                set[y]=1;
                r+=step[0];g+=step[1];b+=step[2];
            }
        }
    }
#else
    for(unsigned k=0;k<3;++k){rgb[k]=((ca>>(k*5))&31)<<16;step[k]=((((cb>>(k*5))&31)<<16)-rgb[k])/span;}
    for(int32_t y=ya;y<=yb;++y) {
        if(y>=0 && y<224) {
            uint16_t c=0x8000;
            for(unsigned k=0;k<3;++k){int32_t v=(rgb[k]+32768)>>16;if(v<0)v=0;if(v>31)v=31;c|=(uint16_t)(v<<(k*5));}
            SKY_LINES[y]=c;l->set[y]=1;
        }
        for(unsigned k=0;k<3;++k)rgb[k]+=step[k];
    }
#endif
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
        if(last<0)for(int k=0;k<y;++k)SKY_LINES[k]=SKY_LINES[y];
        else for(int k=last+1;k<y;++k)SKY_LINES[k]=SKY_LINES[last];
        last=y;
    }
    if(last<0)for(int y=0;y<224;++y)SKY_LINES[y]=fzx_sky_colors[63];
    else for(int y=last+1;y<224;++y)SKY_LINES[y]=SKY_LINES[last];
    /* Outside the view the screen stays black, as with the VDP1 sky. */
    for(int y=0;y<224;++y)if(y<view->clip[1] || y>=view->clip[3])SKY_LINES[y]=0;
    fzx_clouds_sky_line_count=set;
    return true;
}
#endif
