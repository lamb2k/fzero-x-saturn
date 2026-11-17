#include "fzx_view16.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned samples;
static void projection_sweep(void) {
    for(int pose=0;pose<97;++pose) {
        const double yaw=(pose-48)*0.0123,pitch=(pose%13-6)*0.037;
        const double sy=sin(yaw),cy=cos(yaw),sp=sin(pitch),cp=cos(pitch);
        const double r[3][3]={{cy,0,-sy},{sp*sy,cp,sp*cy},{cp*sy,-sp,cp*cy}};
        const int32_t eye[3]={(pose-48)*91*65536,((pose%7)-3)*53*65536,((pose%17)-8)*519*65536};
        fzx_view_t old={0};
        for(unsigned i=0;i<3;++i) {
            double t=0;
            for(unsigned j=0;j<3;++j){old.rotation[i][j]=(int32_t)(r[i][j]*1073741824.0);t-=r[i][j]*eye[j]/65536.0;}
            old.translation[i]=(int32_t)(t*4096);
        }
        old.focal[0]=237*65536;old.focal[1]=251*65536;
        old.center[0]=160*65536+(pose%3)*32767;old.center[1]=112*65536+(pose%5)*16383;
        old.near_plane=16*4096;old.far_plane=8192*4096;
        old.clip[0]=12;old.clip[1]=8;old.clip[2]=308;old.clip[3]=216;
        sh2z_view_t view;assert(fzx_view16_from_source(&view,&old,eye));
        assert(view.clip[2]==307 && view.clip[3]==215);
        for(int depth=64;depth<=4096;depth*=2)for(int x=-4;x<=4;x++)for(int y=-3;y<=3;y++) {
            const double local[3]={depth*x*.071,depth*y*.091,depth};
            sh2z_pos16_t point;int16_t p[3];
            for(unsigned i=0;i<3;i++){double n=eye[i]/65536.0;for(unsigned j=0;j<3;j++)n+=r[j][i]*local[j];p[i]=(int16_t)lround(n);}
            point=(sh2z_pos16_t){p[0],p[1],p[2]};
            const int32_t w[3]={p[0]*4096,p[1]*4096,p[2]*4096};int32_t q[3],s[2];
            assert(fzx_view_transform(&old,w,q) && fzx_view_project(&old,q,s));
            sh2z_view_vertex_t got;assert(sh2z_view_batch(&got,&view,&point,sizeof(point),1));
            const int dx=got.sx-s[0]/65536,dy=got.sy-s[1]/65536;
            assert(dx>=-1 && dx<=1 && dy>=-1 && dy<=1);++samples;
        }
    }
}
static void rejection(void) {
    fzx_view_t v={0};const int32_t eye[3]={0};sh2z_view_t result,sentinel;
    memset(&sentinel,0xa5,sizeof(sentinel));result=sentinel;
    v.rotation[0][0]=v.rotation[1][1]=v.rotation[2][2]=1073741824;
    v.focal[0]=v.focal[1]=200*65536;v.center[0]=160*65536;v.center[1]=112*65536;
    v.near_plane=16*4096;v.far_plane=8192*4096;v.clip[2]=320;v.clip[3]=224;
    assert(!fzx_view16_from_source(NULL,&v,eye));assert(!fzx_view16_from_source(&result,NULL,eye));
    assert(!fzx_view16_from_source(&result,&v,NULL));
    for(unsigned i=0;i<8;i++) {
        fzx_view_t bad=v;
        switch(i){case 0:bad.near_plane=0;break;case 1:bad.far_plane=INT32_MAX;break;
        case 2:bad.center[0]=INT32_MAX;break;case 3:bad.center[1]=INT32_MIN;break;
        case 4:bad.clip[2]=bad.clip[0];break;case 5:bad.focal[1]=0;break;
        case 6:bad.rotation[0][0]=INT32_MAX;break;default:bad.far_plane=bad.near_plane;break;}
        assert(!fzx_view16_from_source(&result,&bad,eye));assert(!memcmp(&result,&sentinel,sizeof(result)));
    }
    const int32_t extreme[3]={INT32_MIN,INT32_MIN,INT32_MIN};
    v.rotation[0][1]=v.rotation[0][2]=1073741824;
    assert(!fzx_view16_from_source(&result,&v,extreme));assert(!memcmp(&result,&sentinel,sizeof(result)));
}
int main(void){projection_sweep();rejection();printf("view16 %u projected samples pass\n",samples);return 0;}
