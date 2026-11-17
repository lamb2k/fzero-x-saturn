/* Synthetic caller qualification. No game data or source-derived fixtures. */
#include "sh2zam/view_experimental.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(c) do { if(!(c)) { fprintf(stderr,"view adoption line %u\n",(unsigned)__LINE__); return 1; } } while(0)
#ifndef FZX_TEST_EDGES
#define FZX_TEST_EDGES 1
#endif
static uint32_t rng=0x6b8b4567u;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint32_t hash(uint32_t h,const void *p,size_t n){const unsigned char *b=p;while(n--)h=(h^*b++)*16777619u;return h;}
static sh2z_view_t camera(void) {
    sh2z_view_t v={0};v.r[0][0]=v.r[1][1]=v.r[2][2]=16384;
    v.focal_x=v.focal_y=160*65536;v.center_x=160;v.center_y=112;
    v.near_z=16384;v.far_z=12000*16384;v.clip[2]=319;v.clip[3]=223;return v;
}
int main(void) {
    sh2z_view_t v=camera();uint32_t h=2166136261u;
    sh2z_pos16_t positions[64];sh2z_view_vertex_t reference[64];
    for(unsigned pose=0;pose<64;++pose) {
        const sh2z_vec3_t eye={(int32_t)(next()%201u)*65536,(int32_t)(next()%51u)*65536,0};
        CHECK(sh2z_view_place(&v,eye));
        for(unsigned i=0;i<64;++i)positions[i]=(sh2z_pos16_t){(int16_t)((int)(next()%20001u)-10000),
            (int16_t)((int)(next()%20001u)-10000),(int16_t)((int)(next()%14001u)-2000)};
        CHECK(sh2z_view_batch(reference,&v,positions,sizeof(*positions),64));h=hash(h,reference,sizeof(reference));
#if FZX_TEST_EDGES
        sh2z_view_vertex_t fast[64];CHECK(sh2z_view_batch_divu(fast,&v,positions,sizeof(*positions),64));
        CHECK(!memcmp(reference,fast,sizeof(fast)));
        CHECK(sh2z_view_project_divu(fast,&v,fast,64));CHECK(!memcmp(reference,fast,sizeof(fast)));
#endif
    }
    printf("synthetic batch 4096 fnv %08x\n",h);
#if FZX_TEST_EDGES
    v=camera();unsigned rounded=0;
    for(unsigned trial=0;trial<512;++trial) {
        sh2z_view_vertex_t a={0},b={0},plain[64],fast[64];
        a.x=(int32_t)(next()%1000001u)-500000;a.y=(int32_t)(next()%1000001u)-500000;a.z=16384+(int32_t)(next()%1000000u);
        b.x=(int32_t)(next()%1000001u)-500000;b.y=(int32_t)(next()%1000001u)-500000;b.z=16384+(int32_t)(next()%1000000u);
        CHECK(sh2z_view_edge_steps(plain,&v,&a,&b,7,13,997,64));
        CHECK(sh2z_view_edge_steps_divu(fast,&v,&a,&b,7,13,997,64));CHECK(!memcmp(plain,fast,sizeof(fast)));
        const int32_t av[3]={a.x,a.y,a.z},bv[3]={b.x,b.y,b.z};
        for(unsigned k=0;k<64;++k) {
            const int32_t got[3]={fast[k].x,fast[k].y,fast[k].z};
            for(unsigned axis=0;axis<3;++axis) {
                const int64_t delta=(int64_t)bv[axis]-av[axis];
                const int64_t old=av[axis]+delta*(7+(int64_t)k*13)/997;
                CHECK(llabs((int64_t)got[axis]-old)<=1);
                if(got[axis]!=old)++rounded;
            }
        }
        memset(fast,0xa5,sizeof(fast));memcpy(plain,fast,sizeof(fast));
        CHECK(!sh2z_view_edge_steps_divu(fast,&v,&a,&b,997,1,997,2));CHECK(!memcmp(fast,plain,sizeof(fast)));
    }
    /* Defined accumulated truncation: at k=3, 3*trunc(65536/3)/65536
     * is just below one. The old divide-at-each-point reached exactly one. */
    sh2z_view_vertex_t edge_a={.z=32768},edge_b={.x=1,.z=32768},edge_out[4];
    CHECK(sh2z_view_edge_steps_divu(edge_out,&v,&edge_a,&edge_b,0,1,3,4));
    CHECK(edge_out[3].x==0);++rounded;
    const unsigned expected[16]={0,3,3,4,3,6,4,5,3,4,6,5,4,5,5,4};
    for(unsigned mask=0;mask<16;++mask) {
        sh2z_view_vertex_t input[4]={{0}},plain[6];
        struct {uint32_t before;sh2z_view_vertex_t out[6];uint32_t after;} guarded;
        memset(&guarded,0xa5,sizeof(guarded));
        for(unsigned i=0;i<4;++i){input[i].x=(i<2?-1:1)*8192;input[i].y=((i==0||i==3)?-1:1)*8192;input[i].z=(mask&(1u<<i))?32768:8192;}
        CHECK(sh2z_view_project(input,&v,input,4));
        unsigned n=sh2z_view_clip_near(plain,&v,input),m=sh2z_view_clip_near_divu(guarded.out,&v,input);
        CHECK(n==expected[mask] && m==n && !memcmp(plain,guarded.out,n*sizeof(*plain)));
        CHECK(guarded.before==0xa5a5a5a5u && guarded.after==0xa5a5a5a5u);
        for(unsigned i=0;i<n;++i)CHECK(guarded.out[i].z>=v.near_z && !(guarded.out[i].outcode&SH2Z_OUT_NEAR));
    }
    /* Real command-format boundary: near clipping does not clip screen sides.
     * The visible edge from (-2840,112) to (160,12) crosses x=0. Clamping its
     * first endpoint to VDP1's -1024 limit moves that crossing by over 8 px.
     * This synthetic counterexample prevents treating near-only as a complete
     * replacement for the frozen renderer's guard-band clipper. */
    sh2z_pos16_t points[4]={{-3000,0,160},{0,100,160},{0,50,160},{-3000,-50,160}};
    sh2z_view_vertex_t input[4],out[6];
    CHECK(sh2z_view_batch_divu(input,&v,points,sizeof(*points),4));
    CHECK(sh2z_view_clip_near_divu(out,&v,input)==4);
    CHECK(out[0].sx==-2840 && out[1].sx==160);
    const double true_y=112.0-100.0*2840.0/3000.0,clamped_y=112.0-100.0*1024.0/1184.0;
    CHECK(fabs(true_y-clamped_y)>8.0);
    unsigned code=63;for(unsigned i=0;i<4;++i)code&=out[i].outcode;CHECK(code==0);
    /* Raising the near plane enough to hide the unsafe coordinates removes
     * this visible face altogether; it is not a geometry-preserving repair. */
    v.near_z=161*16384;CHECK(sh2z_view_clip_near_divu(out,&v,input)==0);
    printf("edges: 32768 points, %u one-unit rounding differences; all 16 near masks and six-output guards pass\n",rounded);
    puts("guard-band counterexample: near-only emits unsafe VDP1 coordinates; clamp exceeds 8 px; raised near loses visible face");
#endif
    return 0;
}
