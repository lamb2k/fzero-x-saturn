#include "fzx_countdown.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"check line %u\n",(unsigned)__LINE__);return 1;}}while(0)

int main(void) {
    unsigned samples=0;
    /* Exhaust the source timeline, using event intervals independent of the
     * implementation's branch ordering. Redraws must be pure queries. */
    for(int timer=0;timer<=460;++timer) {
        fzx_countdown_phase_t a,b;
        CHECK(fzx_countdown_phase(timer,&a));CHECK(fzx_countdown_phase(timer,&b));
        unsigned panel=0;
        const int low[4]={161,101,41,1},high[4]={220,160,100,40};
        for(unsigned i=0;i<4;++i)if(timer>=low[i]&&timer<=high[i])panel=i+1;
        bool mouth=false;const int ml[4]={210,150,90,1},mh[4]={220,160,100,40};
        for(unsigned i=0;i<4;++i)mouth|=timer>=ml[i]&&timer<=mh[i];
        CHECK(a.visible==(timer!=0)&&a.panel==panel&&a.mouth_open==mouth);
        CHECK(a.visible==b.visible&&a.panel==b.panel&&a.mouth_open==b.mouth_open);
    }
    fzx_countdown_phase_t phase={true,true,255},before=phase;
    CHECK(!fzx_countdown_phase(-1,&phase)&&!fzx_countdown_phase(461,&phase));
    CHECK(phase.visible==before.visible&&phase.mouth_open==before.mouth_open&&phase.panel==before.panel);
    CHECK(!fzx_countdown_phase(1,NULL));
    const sh2z_pos16_t points[]={{-160,-60,-30},{160,-60,-30},{160,60,35},{-160,60,35},{0,0,0}};
    for(unsigned test=0;test<160;++test) {
        fzx_view_t view={0};RefCountdown state={.timer=200};
        const double angle=(double)test*0.023;
        view.rotation[0][0]=(int32_t)(cos(angle)*1073741824.0);
        view.rotation[0][2]=(int32_t)(sin(angle)*1073741824.0);
        view.rotation[1][1]=1073741824;
        view.rotation[2][0]=-view.rotation[0][2];view.rotation[2][2]=view.rotation[0][0];
        view.focal[0]=240*65536;view.focal[1]=240*65536;
        view.center[0]=160*65536+(int32_t)(test%3)*16384;view.center[1]=112*65536;
        view.near_plane=16*4096;view.far_plane=8192*4096;
        view.clip[0]=12;view.clip[1]=8;view.clip[2]=308;view.clip[3]=216;
        state.matrix[0]=(int32_t)(cos(angle*0.7)*13107);state.matrix[5]=state.matrix[0];
        state.matrix[1]=(int32_t)(sin(angle*0.7)*13107);state.matrix[4]=-state.matrix[1];
        state.matrix[10]=13107;state.matrix[15]=65536;
        view.translation[0]=7*4096;view.translation[1]=-2*4096;view.translation[2]=3*4096;
        state.matrix[12]=(int32_t)(-sin(angle)*160*65536);
        state.matrix[14]=(int32_t)(cos(angle)*160*65536);
        state.matrix[13]=(int32_t)(test%7)*65536;
        sh2z_view_t native;sh2z_view_vertex_t vertices[5];
        CHECK(fzx_countdown_view(&native,&view,&state));
        CHECK(sh2z_view_batch(vertices,&native,points,sizeof(points[0]),5));
        for(unsigned p=0;p<5;++p) {
            const double local[3]={points[p].x,points[p].y,points[p].z};
            double world[3],camera[3];
            for(unsigned j=0;j<3;++j) {
                world[j]=(double)state.matrix[12+j]/65536;
                for(unsigned k=0;k<3;++k)world[j]+=local[k]*state.matrix[k*4+j]/65536;
            }
            for(unsigned j=0;j<3;++j) {
                camera[j]=(double)view.translation[j]/4096;
                for(unsigned k=0;k<3;++k)camera[j]+=(double)view.rotation[j][k]*world[k]/1073741824;
            }
            const int x=(int)((double)view.center[0]/65536+240*camera[0]/camera[2]);
            const int y=(int)((double)view.center[1]/65536-240*camera[1]/camera[2]);
            CHECK(camera[2]>16&&abs(x-vertices[p].sx)<=1&&abs(y-vertices[p].sy)<=1);++samples;
        }
        const sh2z_view_t saved=native;
        state.matrix[15]=0;
        CHECK(!fzx_countdown_view(&native,&view,&state)&&!memcmp(&native,&saved,sizeof(native)));
        state.matrix[15]=65536;state.timer=0;
        CHECK(!fzx_countdown_view(&native,&view,&state)&&!memcmp(&native,&saved,sizeof(native)));
        state.timer=200;state.faults=1;
        CHECK(!fzx_countdown_view(&native,&view,&state)&&!memcmp(&native,&saved,sizeof(native)));
        state.faults=0;state.matrix[3]=1;
        CHECK(!fzx_countdown_view(&native,&view,&state)&&!memcmp(&native,&saved,sizeof(native)));
    }
    printf("461 timeline states; %u projected samples; invalid state rejected\n",samples);return 0;
}
