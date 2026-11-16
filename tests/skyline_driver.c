#include "fzx_skyline.h"
#include <stdio.h>
#include <string.h>
static fzx_skyline_quad_t quads[6];static unsigned count;
static bool emit(void *context,const fzx_skyline_quad_t *q) {
    if(context || count>=6)return false;
    quads[count++]=*q;return true;
}
int main(void) {
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .focal={160*65536,160*65536},.center={160*65536,112*65536},
        .near_plane=4096,.far_plane=1000*4096,.clip={12,8,308,216}};
    RefSkyline s={.count=6};fzx_skyline_stats_t stats;
    for(unsigned i=0;i<4;++i) {
        s.vertices[i].position[0]=(i&1)?32:-32;
        s.vertices[i].position[1]=(i&2)?-32:32;
    }
    for(unsigned i=0;i<6;++i) {
        s.instances[i].visible=1;s.instances[i].sprite_id=i%4+1;
        s.instances[i].matrix[0]=s.instances[i].matrix[5]=s.instances[i].matrix[10]=s.instances[i].matrix[15]=65536;
        s.instances[i].matrix[14]=100*65536;
    }
    if(!fzx_skyline_draw(&view,&s,6,emit,NULL,&stats)||count!=6)return 1;
    for(unsigned i=0;i<6;++i)if(quads[i].texture!=i%4 || quads[i].x[0]!=109 || quads[i].x[1]!=211 ||
        quads[i].x[2]!=211 || quads[i].x[3]!=109 || quads[i].y[0]!=61 || quads[i].y[3]!=163)return 2;
    count=0;if(fzx_skyline_draw(&view,&s,5,emit,NULL,&stats)||stats.fault!=4)return 3;
    count=0;if(fzx_skyline_draw(&view,&s,6,emit,&s,&stats)||stats.fault!=5)return 4;
    s.instances[0].sprite_id=5;
    count=0;if(fzx_skyline_draw(&view,&s,6,emit,NULL,&stats)||stats.fault!=1)return 5;
    s.instances[0].sprite_id=1;
    for(unsigned i=0;i<6;++i)s.instances[i].matrix[12]=500*65536;
    count=0;if(!fzx_skyline_draw(&view,&s,6,emit,NULL,&stats)||count || stats.clipped!=6)return 6;
    /* Side crossings keep original UVs and rely on the hardware scissor. */
    for(unsigned i=0;i<6;++i)s.instances[i].matrix[12]=-80*65536;
    count=0;if(!fzx_skyline_draw(&view,&s,6,emit,NULL,&stats)||count!=6 || quads[0].x[0]>=12 || quads[0].x[1]<=12)return 7;
    for(unsigned i=0;i<6;++i)s.instances[i].matrix[14]=-100*65536;
    count=0;if(!fzx_skyline_draw(&view,&s,6,emit,NULL,&stats)||count || stats.clipped!=6)return 8;
    puts("skyline six placements, four bindings, projection/scissor/capacity controls pass");return 0;
}
