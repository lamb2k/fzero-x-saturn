#include "fzx_view16.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned samples;

static fzx_view_t source_view(void) {
    fzx_view_t v={0};
    v.rotation[0][0]=v.rotation[1][1]=v.rotation[2][2]=1073741824;
    v.focal[0]=v.focal[1]=256*65536;
    v.center[0]=160*65536;v.center[1]=112*65536;
    v.near_plane=16*4096;v.far_plane=8192*4096;
    v.clip[0]=12;v.clip[1]=8;v.clip[2]=308;v.clip[3]=216;
    return v;
}

static void check_point(const fzx_view_t *source,const sh2z_view_t *view,
                        const sh2z_pos16_t *point,int expected_y) {
    const int32_t world[3]={point->x*4096,point->y*4096,point->z*4096};
    int32_t camera[3],screen[2];
    assert(fzx_view_transform(source,world,camera));
    assert(fzx_view_project(source,camera,screen));
    sh2z_view_vertex_t batch,divu,project,project_divu;
    assert(sh2z_view_batch_trunc(&batch,view,point,sizeof(*point),1));
    assert(sh2z_view_batch_trunc_divu(&divu,view,point,sizeof(*point),1));
    assert(!memcmp(&batch,&divu,sizeof(batch)));
    assert(batch.x==camera[0]*4 && batch.y==camera[1]*4 && batch.z==camera[2]*4);
    assert(sh2z_view_project_trunc(&project,view,&batch,1));
    assert(sh2z_view_project_trunc_divu(&project_divu,view,&batch,1));
    assert(!memcmp(&batch,&project,sizeof(batch)));
    assert(!memcmp(&batch,&project_divu,sizeof(batch)));
    assert(batch.sx==screen[0]/65536 && batch.sy==screen[1]/65536);
    if(expected_y!=INT_MAX)assert(batch.sy==expected_y);
    unsigned bits=0;
    if(batch.sx<source->clip[0])bits|=SH2Z_OUT_LEFT;
    if(batch.sx>=source->clip[2])bits|=SH2Z_OUT_RIGHT;
    if(batch.sy<source->clip[1])bits|=SH2Z_OUT_TOP;
    if(batch.sy>=source->clip[3])bits|=SH2Z_OUT_BOTTOM;
    assert(batch.outcode==bits);
    ++samples;
}

static void exact_projection(void) {
    static const int centers[][2]={{160,112},{0,0},{-64,-32}};
    static const int eyes[][3]={{0,0,0},{231,-97,503},{-407,83,-811}};
    static const int offsets[]={-511,-257,-129,-80,-1,0,1,80,129,257,511};
    static const int depths[]={64,127,128,255,256,257,512,1024,4096};
    for(unsigned c=0;c<sizeof(centers)/sizeof(centers[0]);++c)
    for(unsigned e=0;e<sizeof(eyes)/sizeof(eyes[0]);++e) {
        fzx_view_t source=source_view();int32_t eye[3];
        for(unsigned i=0;i<2;++i)source.center[i]=centers[c][i]*65536;
        for(unsigned i=0;i<3;++i) {
            eye[i]=eyes[e][i]*65536;source.translation[i]=-eyes[e][i]*4096;
        }
        sh2z_view_t view;assert(fzx_view16_from_source_trunc(&view,&source,eye));
        assert(view.center_x==centers[c][0] && view.center_y==centers[c][1]);
        for(unsigned i=0;i<4;++i)assert(view.clip[i]==source.clip[i]-(i>=2));
        for(unsigned z=0;z<sizeof(depths)/sizeof(depths[0]);++z)
        for(unsigned x=0;x<sizeof(offsets)/sizeof(offsets[0]);++x)
        for(unsigned y=0;y<sizeof(offsets)/sizeof(offsets[0]);++y) {
            const sh2z_pos16_t point={(int16_t)(eyes[e][0]+offsets[x]),
                (int16_t)(eyes[e][1]+offsets[y]),(int16_t)(eyes[e][2]+depths[z])};
            check_point(&source,&view,&point,INT_MAX);
        }
    }
    const fzx_view_t source=source_view();const int32_t eye[3]={0};
    sh2z_view_t view;assert(fzx_view16_from_source_trunc(&view,&source,eye));
    /* Both exact-pixel and fractional versions of the reported 208/192 rows. */
    const sh2z_pos16_t hand[]={{0,-96,256},{0,-80,256},{0,-97,257},{0,-81,257},
                              {-161,113,256},{-162,114,257}};
    const int expected[]={208,192,208,192,-1,-1};
    for(unsigned i=0;i<sizeof(hand)/sizeof(hand[0]);++i)
        check_point(&source,&view,&hand[i],expected[i]);
}

static void conversion(void) {
    fzx_view_t source=source_view();const int32_t eye[3]={3*65536,-5*65536,7*65536};
    /* A quarter-turn and half-unit eye verify the existing placement contract. */
    source.rotation[0][0]=source.rotation[2][2]=0;
    source.rotation[0][2]=-1073741824;source.rotation[2][0]=1073741824;
    sh2z_view_t view;assert(fzx_view16_from_source_trunc(&view,&source,eye));
    assert(view.r[0][2]==-16384 && view.r[1][1]==16384 && view.r[2][0]==16384);
    assert(view.t[0]==7*16384 && view.t[1]==5*16384 && view.t[2]==-3*16384);
    assert(view.near_z==source.near_plane*4 && view.far_z==source.far_plane*4);
    assert(view.focal_x==source.focal[0] && view.focal_y==source.focal[1]);
    const int32_t half_eye[3]={65536+32768,-65536-32768,32768};
    source=source_view();assert(fzx_view16_from_source_trunc(&view,&source,half_eye));
    assert(view.t[0]==-24576 && view.t[1]==24576 && view.t[2]==-8192);
    source.center[0]=INT32_MIN;source.center[1]=INT32_MIN;
    assert(fzx_view16_from_source_trunc(&view,&source,eye));
    assert(view.center_x==INT16_MIN && view.center_y==INT16_MIN);
    source.center[0]=32767*65536;source.center[1]=32767*65536;
    assert(fzx_view16_from_source_trunc(&view,&source,eye));
    assert(view.center_x==INT16_MAX && view.center_y==INT16_MAX);
}

static void rejection(void) {
    const fzx_view_t source=source_view();const int32_t eye[3]={0};
    sh2z_view_t out,sentinel;memset(&sentinel,0xa5,sizeof(sentinel));out=sentinel;
    assert(!fzx_view16_from_source_trunc(NULL,&source,eye));
    assert(!fzx_view16_from_source_trunc(&out,NULL,eye));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    assert(!fzx_view16_from_source_trunc(&out,&source,NULL));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
    for(unsigned i=0;i<14;++i) {
        fzx_view_t bad=source;
        switch(i) {
        case 0:bad.center[0]+=1;break;case 1:bad.center[1]-=1;break;
        case 2:bad.center[0]=-32768;break;case 3:bad.center[1]=32768;break;
        case 4:bad.center[0]=INT32_MAX;break;case 5:bad.near_plane=0;break;
        case 6:bad.far_plane=INT32_MAX;break;case 7:bad.far_plane=bad.near_plane;break;
        case 8:bad.clip[2]=bad.clip[0];break;case 9:bad.clip[3]=bad.clip[1];break;
        case 10:bad.focal[0]=0;break;case 11:bad.focal[1]=-65536;break;
        case 12:bad.rotation[0][0]=INT32_MAX;break;default:bad.near_plane=-1;break;
        }
        const fzx_view_t before=bad;
        assert(!fzx_view16_from_source_trunc(&out,&bad,eye));
        assert(!memcmp(&out,&sentinel,sizeof(out)));
        assert(!memcmp(&bad,&before,sizeof(bad)));
    }
    fzx_view_t bad=source;bad.rotation[0][1]=bad.rotation[0][2]=1073741824;
    const int32_t extreme[3]={INT32_MIN,INT32_MIN,INT32_MIN};
    assert(!fzx_view16_from_source_trunc(&out,&bad,extreme));
    assert(!memcmp(&out,&sentinel,sizeof(out)));
}

int main(void) {
    exact_projection();conversion();rejection();
    printf("view16 trunc %u exact source-projection samples pass\n",samples);
    return 0;
}
