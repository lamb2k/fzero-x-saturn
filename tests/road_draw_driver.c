#include "fzx_road_draw.c"
#include <assert.h>
#include <stdio.h>
static uint16_t pixels[320*224];
static unsigned emitted;
static fzx_road_chart_t last;
static bool emit(void *ctx,const fzx_road_chart_t *c) {
    (void)ctx;last=*c;++emitted;return true;
}
static vertex_t vertex(int x,int y,int z,int red) {
    vertex_t v={{x*z/16,-y*z/16,z},{0,0,red,0,0,0}};return v;
}
int main(void) {
    fzx_view_t view={.focal={65536,65536},.near_plane=1,.far_plane=1048576,.clip={0,0,320,224}};
    RefRoadPlan plan={0};
    fzx_road_draw_stats_t stats={0};
    draw_t d={.view=&view,.plan=&plan,.atlas=pixels,.bytes=sizeof(pixels),.capacity=8,
              .emit=emit,.stats=&stats,.material={.primitive=true}};
    vertex_t p[MAX_VERTICES]={vertex(64,64,65536,0),vertex(64,448,131072,65535),vertex(448,64,262144,32768)};
    assert(chart(&d,p,3,7,2));assert(emitted==1 && stats.shaded_pixels>0);
    assert(last.x==4 && last.y==4 && last.width==24 && last.height==24);
    for(unsigned y=0;y<24;++y)for(unsigned x=0;x<24;++x)printf("%04x\n",pixels[y*24+x]);
    uint16_t first[24*24];memcpy(first,pixels,sizeof(first));
    p[3]=p[2];p[2]=p[1];stats=(fzx_road_draw_stats_t){0};
    assert(chart(&d,p,4,7,2));assert(!memcmp(first,pixels,sizeof(first)));
    /* A duplicate created by crossing at an exact near-plane endpoint. */
    vertex_t input[3]={vertex(64,64,65536,0),vertex(64,448,131072,0),vertex(448,64,32768,0)},clipped[MAX_VERTICES];
    view.near_plane=65536;
    int n=clip(&view,input,clipped);assert(n>=3);
    stats=(fzx_road_draw_stats_t){0};assert(chart(&d,clipped,(unsigned)n,0,0));
    assert(stats.shaded_pixels>0);
    /* Full int32 interpolation limits, including both exact endpoints. */
    assert(lerp(INT32_MIN,INT32_MAX,UINT64_C(4294967296))==INT32_MAX);
    assert(lerp(INT32_MAX,INT32_MIN,UINT64_C(4294967296))==INT32_MIN);
    assert(lerp(INT32_MIN,INT32_MAX,UINT64_C(2147483648))==-1);
    /* Area one: initial planes fit, bbox extrapolation does not. */
    p[0]=vertex(0,0,65536,0);p[1]=vertex(3416,2391,65536,0);p[2]=vertex(5119,3583,65536,0);
    p[0].a[0]=-262144;p[1].a[0]=-262144;p[2].a[0]=262136;
    stats=(fzx_road_draw_stats_t){0};assert(!chart(&d,p,3,0,0));
    assert(stats.charts==0 && stats.tested_pixels==0);
    projected_t q[3]={{.x=0,.y=0,.inv=65536,.a={-262144}},
                     {.x=5119,.y=3583,.inv=65536,.a={262136}},
                     {.x=3416,.y=2391,.inv=65536,.a={-262144}}};
    int64_t weights[3],out;
    for(unsigned i=0;i<3;++i)weights[i]=edge(&q[(i+1)%3],&q[(i+2)%3],5112,8);
    assert(!plane_step(q,1,weights,1,&out));
    assert(!plane_span_valid(INT64_MIN,0,0,1,1));
    assert(!plane_span_valid(0,INT64_MIN,0,1,1));
    /* Capacity faults must leave the chart unpublished. */
    p[0]=vertex(64,64,65536,0);p[1]=vertex(64,448,65536,0);p[2]=vertex(448,64,65536,0);
    stats=(fzx_road_draw_stats_t){0};d.bytes=8;
    assert(!chart(&d,p,3,0,0));assert(stats.fault==2 && !stats.charts);
    return 0;
}
