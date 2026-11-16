/* Synthetic gradient and rolled geometry, not retail assets or camera traces. */
#include "fzx_sky.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifndef FZX_TEST_SKY_ADAPTED
#define FZX_TEST_SKY_ADAPTED 0
#endif

static fzx_sky_polygon_t polygons[512];
static unsigned written;
static uint32_t hash=2166136261u;
static bool collect(void *ctx,const fzx_sky_polygon_t *p) {
    if(ctx || written>=512)return false;
    polygons[written++]=*p;
    for(unsigned i=0;i<4;++i) {
        const uint16_t fields[3]={(uint16_t)p->x[i],(uint16_t)p->y[i],p->color[i]};
        for(unsigned j=0;j<3;++j)for(unsigned k=0;k<2;++k)
            hash=(hash^((fields[j]>>(8u*k))&255u))*16777619u;
    }
    return true;
}
static double cross(double ax,double ay,double bx,double by,double cx,double cy) {
    return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
}
static int shade(double x,double y,double out[3]) {
    for(unsigned p=0;p<written;++p)for(unsigned t=0;t<2;++t) {
        const unsigned ix[3]={0,t+1,t+2};const fzx_sky_polygon_t *q=&polygons[p];
        const double a=cross(q->x[ix[0]],q->y[ix[0]],q->x[ix[1]],q->y[ix[1]],q->x[ix[2]],q->y[ix[2]]);
        if(a==0)continue;
        double w[3];
        w[0]=cross(q->x[ix[1]],q->y[ix[1]],q->x[ix[2]],q->y[ix[2]],x,y)/a;
        w[1]=cross(q->x[ix[2]],q->y[ix[2]],q->x[ix[0]],q->y[ix[0]],x,y)/a;
        w[2]=1-w[0]-w[1];
        if(w[0]<-1e-8 || w[1]<-1e-8 || w[2]<-1e-8)continue;
        for(unsigned c=0;c<3;++c) {
            out[c]=0;
            for(unsigned j=0;j<3;++j)out[c]+=w[j]*((q->color[ix[j]]>>(c*5u))&31u);
        }
        return 1;
    }
    return 0;
}
int main(void) {
    uint16_t colors[64];
    for(unsigned i=0;i<64;++i)colors[i]=(uint16_t)(0x8000u|(i/2u)|((31u-i/2u)<<5)|((i/4u+8u)<<10));
    const int turns[4][2]={{1000,0},{800,600},{0,1000},{-600,800}};
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .focal={160*65536,160*65536},.center={160*65536,112*65536},
        .near_plane=4096,.far_plane=1000*4096,.clip={12,8,308,216}};
    unsigned maximum=0,checks=0;
    RefBackdropVertex v[4]={0};fzx_sky_stats_t stats;
    for(unsigned turn=0;turn<4;++turn) {
        /* Large quad covers the entire cropped screen even when rolled. */
        const int local[4][2]={{-300,300},{300,300},{-300,-300},{300,-300}};
        for(unsigned i=0;i<4;++i) {
            v[i].position[0]=(short)((local[i][0]*turns[turn][0]-local[i][1]*turns[turn][1])/1000);
            v[i].position[1]=(short)((local[i][0]*turns[turn][1]+local[i][1]*turns[turn][0])/1000);
            v[i].position[2]=100;v[i].uv[0]=(short)((i<2?-64:128)*32);
        }
        written=0;
        if(!fzx_sky_draw(&view,v,colors,512,collect,NULL,&stats) || !written || stats.polygons!=written) return 1;
        if(written>maximum)maximum=written;
        for(unsigned i=0;i<written;++i)for(unsigned j=0;j<4;++j)
            if(polygons[i].x[j]<12 || polygons[i].x[j]>=308 || polygons[i].y[j]<8 || polygons[i].y[j]>=216)return 2;
        for(int y=12;y<212;y+=3)for(int x=16;x<304;x+=3) {
            double actual[3];
            if(!shade(x,y,actual)){fprintf(stderr,"hole %u %d %d\n",turn,x,y);return 3;}
            const double wx=(x-160)/1.6,wy=(112-y)/1.6;
            const double local_y=(-wx*turns[turn][1]+wy*turns[turn][0])/1000.0;
            double s=32-local_y*0.32;
            if(s<0)s=0;
            if(s>63)s=63;
            const unsigned a=(unsigned)s,b=a<63?a+1:a;
            for(unsigned c=0;c<3;++c) {
                const double want=((colors[a]>>(c*5u))&31u)*(1-(s-a))+((colors[b]>>(c*5u))&31u)*(s-a);
                if(fabs(actual[c]-want)>(FZX_TEST_SKY_ADAPTED?2.01:1.01)) {
                    fprintf(stderr,"shade %u %d %d %u %.4f %.4f\n",turn,x,y,c,actual[c],want);return 4;
                }
            }
            ++checks;
        }
    }
    /* Unequal-depth source diagonal crosses the left scissor. Opposite edge
     * traversal must produce one shared endpoint, not adjacent scan lines. */
    const int skew[4][3]={{-169,-19,179},{256,100,229},{-169,-200,179},{256,-104,229}};
    for(unsigned i=0;i<4;++i) {
        for(unsigned j=0;j<3;++j)v[i].position[j]=(short)skew[i][j];
        v[i].uv[0]=(short)(i==0?-826:-1356);
    }
    written=0;if(!fzx_sky_draw(&view,v,colors,512,collect,NULL,&stats))return 9;
    unsigned shared=0;int shared_y=-1;
    for(unsigned i=0;i<written;++i)for(unsigned j=0;j<4;++j)
        if(polygons[i].x[j]==12 && polygons[i].y[j]>=129 && polygons[i].y[j]<=130) {
            if(shared_y>=0 && shared_y!=polygons[i].y[j])return 10;
            shared_y=polygons[i].y[j];++shared;
        }
    if(shared<2)return 11;
    for(int y=129;y<=135;++y)for(int x=13;x<=20;++x) {
        double actual[3];if(!shade(x+0.5,y+0.5,actual))return 12;
    }
    written=0;if(fzx_sky_draw(&view,v,colors,1,collect,NULL,&stats)||stats.fault!=4)return 5;
    written=0;if(fzx_sky_draw(&view,v,colors,512,collect,&view,&stats)||stats.fault!=5)return 6;
    colors[7]&=0x7fff;if(fzx_sky_draw(&view,v,colors,512,collect,NULL,&stats)||stats.fault!=1)return 7;
    colors[7]|=0x8000;
    for(unsigned i=0;i<4;++i)v[i].position[2]=-100;
    written=0;if(!fzx_sky_draw(&view,v,colors,512,collect,NULL,&stats)||written||stats.clipped!=2)return 8;
    printf("sky %08x pixels=%u maximum=%u\n",hash,checks,maximum);return 0;
}
