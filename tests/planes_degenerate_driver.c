/* FZX_PLANES_DEGENERATE_UV: a synthetic scene whose cloud-layer UVs collapse to
 * one point (as the source's do when the eye is at cloud-plane height). Prints one
 * line per case; the test compares knob 0 and knob 1 builds. */
#include "fzx_planes.h"
#include <stdio.h>
#include <string.h>
static unsigned count;static uint32_t hash;
static bool emit(void *context,const fzx_plane_quad_t *q) {
    (void)context;const uint8_t *b=(const uint8_t *)q;
    for(unsigned i=0;i<sizeof(*q);++i)hash=(hash^b[i])*16777619u;
    ++count;return true;
}
static void scene(RefBackdrop *s) {
    memset(s,0,sizeof(*s));s->count=20;
    for(unsigned layer=0;layer<2;++layer)for(unsigned i=0;i<8;++i) {
        RefBackdropVertex *v=&s->vertices[(layer?12:0)+i];
        v->position[0]=(i&1)?200:-200;v->position[1]=layer?80:-40;
        unsigned row=i<4?i/2:1+(i-4)/2;
        v->position[2]=(int16_t)(500-(int)row*400);
        v->uv[0]=(i&1)?2048:0;v->uv[1]=(int16_t)(1024-(int)row*1024);
        memset(v->color,row==1?0:255,4);
    }
}
static void run(const char *name,const RefBackdrop *s) {
    static const uint16_t fog[3]={31,23,31},mean[3]={15,15,15};
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .focal={160*65536,160*65536},.center={160*65536,112*65536},.near_plane=4096,.far_plane=1000*4096,.clip={12,8,308,216}};
    fzx_plane_stats_t st;count=0;hash=2166136261u;
    bool ok=fzx_planes_draw(&view,s,fog,mean,640,emit,NULL,&st);
#if FZX_PLANES_DEGENERATE_UV
    unsigned skipped=fzx_plane_degenerate_layers;
#else
    unsigned skipped=0;
#endif
    printf("%s ok %u draws %u fault %u hash %08x skipped %u\n",name,ok,count,st.fault,hash,skipped);
}
int main(void) {
    RefBackdrop s;
    scene(&s);run("normal",&s);
    /* Cloud layer only: every UV the same point, as at the eye crossing the cloud plane. */
    scene(&s);for(unsigned i=12;i<20;++i){s.vertices[i].uv[0]=-31915;s.vertices[i].uv[1]=-1490;}
    run("cloud-collapsed",&s);
    /* Floor layer collapsed instead. */
    scene(&s);for(unsigned i=0;i<8;++i){s.vertices[i].uv[0]=100;s.vertices[i].uv[1]=100;}
    run("floor-collapsed",&s);
    return 0;
}
