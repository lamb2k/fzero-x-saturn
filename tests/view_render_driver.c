/* Exercise the actual polygon submission path with synthetic camera-space quads. */
#include "../src/fzx.c"
#include <stdio.h>
#include <math.h>
#define CHECK(x) do { if(!(x)){fprintf(stderr,"render view gate line %d\n",__LINE__);return 1;} } while(0)
int main(void) {
    fzx_view_t v={0};
    v.focal[0]=200*65536;v.focal[1]=204*65536;
    v.center[0]=140*65536;v.center[1]=104*65536;
    v.near_plane=65536;v.far_plane=512*65536;
    v.clip[0]=12;v.clip[1]=8;v.clip[2]=308;v.clip[3]=216;
    fzx_set_camera(&v);
    const sh2z_vec3_t broad[4]={{-200*65536,-200*65536,10*65536},
        {200*65536,-200*65536,10*65536},{200*65536,200*65536,10*65536},
        {-200*65536,200*65536,10*65536}};
    frame.count=0;frame.flags=0;
    draw_quad_inside(broad[0],broad[1],broad[2],broad[3],0xffff,0);
    CHECK(frame.count && !frame.flags);
    int lo_x=320,hi_x=0,lo_y=224,hi_y=0;
    for(unsigned i=0;i<frame.count;++i)for(unsigned j=0;j<4;++j) {
        const fzx_poly_t *p=&frame.polys[i];
        CHECK(p->x[j]>=12 && p->x[j]<308 && p->y[j]>=8 && p->y[j]<216);
        if(p->x[j]<lo_x)lo_x=p->x[j];
        if(p->x[j]>hi_x)hi_x=p->x[j];
        if(p->y[j]<lo_y)lo_y=p->y[j];
        if(p->y[j]>hi_y)hi_y=p->y[j];
    }
    CHECK(lo_x==12 && hi_x==307 && lo_y==8 && hi_y==215);
    frame.count=FZX_MAX_POLYGONS;
    draw_quad_inside(broad[0],broad[1],broad[2],broad[3],0xffff,0);
    CHECK(frame.flags&FZX_FRAME_OVERFLOW);
    /* A source view must transform the complete ring, including geometry that
     * was outside the old 72-ring forward window. Use an entirely synthetic loop. */
    fzx_course_t *course=fzx_course_buffer();
    memset(course,0,sizeof(*course));course->count=16;
    for(unsigned i=0;i<16;++i) {
        const double angle=(double)i*6.283185307179586/16;
        course->points[i].position=(sh2z_vec3_t){(int32_t)(cos(angle)*60*65536),0,(int32_t)(sin(angle)*60*65536)};
        course->points[i].radius_left=course->points[i].radius_right=4*65536;
        course->points[i].tension=SH2Z_HALF;
    }
    CHECK(fzx_activate_course());CHECK(ring_count>VISIBLE_RINGS);
    v.rotation[0][0]=1<<30;v.rotation[1][2]=1<<30;v.rotation[2][1]=-(1<<30);
    v.translation[2]=200*65536;
    fzx_set_camera(&v);
    memset(transformed,0,sizeof(transformed));
    const fzx_frame_t *rendered=fzx_render();
    CHECK(rendered->count && !(rendered->flags&FZX_FRAME_OVERFLOW));
    for(unsigned i=0;i<ring_count*4;++i) {
        const int32_t p[3]={track_mesh[i].x,track_mesh[i].y,track_mesh[i].z};int32_t q[3];
        CHECK(fzx_view_transform(&v,p,q));
        CHECK(transformed[i].x==q[0] && transformed[i].y==q[1] && transformed[i].z==q[2]);
    }
    puts("source view actual renderer scissor/overflow gate PASS");return 0;
}
