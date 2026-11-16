#include "fzx_view.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"view gate failed line %d\n",__LINE__);return 1;} } while(0)
static unsigned rng=1234567;
static unsigned next(void){rng=rng*1664525u+1013904223u;return rng;}
int main(void) {
    float v[4][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    float p[4][4]={{1.25f,0,0,0},{0,1.7f,0,0},{0.125f,-0.0625f,-1.01f,-1},{0,0,-32,0}};
    const float vp[4]={160,120,160,120},sc[4]={12,16,308,224};
    fzx_view_t a;
    CHECK(fzx_view_from_source(&a,(const float(*)[4])v,(const float(*)[4])p,vp,sc,16,8192));
    CHECK(a.center[0]==140*65536 && a.center[1]==(int32_t)(104.5*65536));
    CHECK(a.focal[0]==200*65536 && a.focal[1]==204*65536);
    CHECK(a.clip[0]==12 && a.clip[1]==8 && a.clip[2]==308 && a.clip[3]==216);
    CHECK(a.near_plane==65536 && a.far_plane==512*65536);
    int32_t point[3]={0,0,-20*65536},q[3],xy[2];
    CHECK(fzx_view_transform(&a,point,q));CHECK(q[2]==20*65536);
    CHECK(fzx_view_project(&a,q,xy));CHECK(xy[0]==a.center[0] && xy[1]==a.center[1]);
    for(unsigned plane=0;plane<6;++plane)CHECK(fzx_view_plane(&a,q,plane)>0);
    q[2]=0;CHECK(!fzx_view_project(&a,q,xy));
    const int32_t outside[6][3]={{0,0,0},{0,0,513*65536},{-20*65536,0,20*65536},
        {20*65536,0,20*65536},{0,20*65536,20*65536},{0,-20*65536,20*65536}};
    for(unsigned i=0;i<6;++i)CHECK(fzx_view_plane(&a,outside[i],i)<0);
    for(unsigned i=0;i<10000;++i) {
        const int64_t aa=(int64_t)(next()%1000000000u),bb=(int64_t)(next()%1000000000u)+1;
        CHECK(fzx_view_fraction(aa,-bb)==aa*65536/(aa+bb));
        CHECK(fzx_view_fraction(-aa,bb)==aa*65536/(aa+bb));
    }
    CHECK(fzx_view_fraction(INT64_MIN,INT64_MAX)==32768);
    CHECK(fzx_view_fraction(INT64_MAX,INT64_MIN)==32767);
    CHECK(fzx_view_fraction(INT64_MAX,-1)==65535);
    CHECK(fzx_view_fraction(-1,INT64_MAX)==0);
    CHECK(fzx_view_fraction(0,-1)==0 && fzx_view_fraction(1,0)==65536);
    /* Entering near-plane edge with distances -1,+65536 used to retain the
     * outside endpoint when the fraction was truncated to sixteen bits. */
    const int32_t near_cross[4][3]={{-1000,0,65535},{1000,0,131072},
                                  {1000,1000,131072},{-1000,1000,65535}};
    int32_t clipped[10][3];
    int clipped_count=fzx_view_clip(&a,near_cross,4,clipped);
    CHECK(clipped_count>=3);
    for(int i=0;i<clipped_count;++i)CHECK(clipped[i][2]>=65536);
    unsigned crossing_count=0;
    for(unsigned sample=0;sample<12000;++sample) {
        const int32_t z=(int32_t)(next()%(600u*65536u))-20*65536;
        const int32_t x=(int32_t)(next()%(1000u*65536u))-500*65536;
        const int32_t y=(int32_t)(next()%(1000u*65536u))-500*65536;
        const int32_t dx=(int32_t)(next()%(100u*65536u))+1;
        const int32_t dy=(int32_t)(next()%(100u*65536u))+1;
        const int32_t dz=(int32_t)(next()%(100u*65536u));
        const int32_t quad[4][3]={{x-dx,y-dy,z-dz},{x+dx,y-dy,z},
                                {x+dx,y+dy,z+dz},{x-dx,y+dy,z}};
        clipped_count=fzx_view_clip(&a,quad,4,clipped);
        CHECK(clipped_count>=0 && clipped_count<=10);
        for(int i=0;i<clipped_count;++i) {
            for(unsigned plane=0;plane<6;++plane)CHECK(fzx_view_plane(&a,clipped[i],plane)>=0);
            CHECK(fzx_view_project(&a,clipped[i],xy));
            CHECK(xy[0]>=(int32_t)a.clip[0]*65536 && xy[0]<=(int32_t)a.clip[2]*65536);
            CHECK(xy[1]>=(int32_t)a.clip[1]*65536 && xy[1]<=(int32_t)a.clip[3]*65536);
        }
        if(clipped_count)++crossing_count;
    }
    CHECK(crossing_count>1000);
    /* Non-symmetric rotation at a distant world position catches transposition,
     * handedness, lost translation scale and Q16-only rotation cancellation. */
    unsigned checked=0;double worst=0;
    for(unsigned camera=0;camera<16;++camera) {
        const double angle=0.17+camera*0.37,c=cos(angle),s=sin(angle);
        memset(v,0,sizeof(v));v[0][0]=(float)c;v[2][0]=(float)s;
        v[1][1]=1;v[0][2]=(float)-s;v[2][2]=(float)c;v[3][3]=1;
        const double eye[3]={3000.125,-2100.0625,1900.25};
        for(unsigned j=0;j<3;++j)v[3][j]=(float)(-16*(eye[0]*v[0][j]+eye[1]*v[1][j]+eye[2]*v[2][j]));
        CHECK(fzx_view_from_source(&a,(const float(*)[4])v,(const float(*)[4])p,vp,sc,16,8192));
        for(unsigned sample=0;sample<1000;++sample) {
            const double z=1.1+(next()%50000)/100.0;
            const double x=z*((int)(next()%10000)-5000)/10000.0;
            const double y=z*((int)(next()%10000)-5000)/10000.0;
            const double world[3]={eye[0]+c*x+s*z,eye[1]+y,eye[2]+s*x-c*z};
            for(unsigned j=0;j<3;++j)point[j]=(int32_t)(world[j]*65536);
            CHECK(fzx_view_transform(&a,point,q));CHECK(fzx_view_project(&a,q,xy));
            double camera_point[3]={0};
            for(unsigned j=0;j<3;++j){
                camera_point[j]=v[3][j]/16.0;
                for(unsigned k=0;k<3;++k)camera_point[j]+=(double)point[k]/65536*v[k][j];
            }
            const double ndc_x=(camera_point[0]*p[0][0]+camera_point[2]*p[2][0])/-camera_point[2];
            const double ndc_y=(camera_point[1]*p[1][1]+camera_point[2]*p[2][1])/-camera_point[2];
            const double want[2]={vp[2]+vp[0]*ndc_x,vp[3]-8-vp[1]*ndc_y};
            for(unsigned j=0;j<2;++j){double error=fabs(xy[j]/65536.0-want[j]);if(error>worst)worst=error;CHECK(error<0.0625);}
            ++checked;
        }
    }
    fzx_view_t old=a;p[0][0]=NAN;
    CHECK(!fzx_view_from_source(&a,(const float(*)[4])v,(const float(*)[4])p,vp,sc,16,8192));
    CHECK(!memcmp(&a,&old,sizeof(a)));
    printf("view gate PASS %u distant-world projections, max error %.6f pixels; fraction/bounds controls\n",checked,worst);
    return 0;
}
