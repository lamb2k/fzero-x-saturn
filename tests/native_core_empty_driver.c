#include "fzx.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    fzx_init();
    fzx_course_t *course=fzx_course_buffer();
    memset(course,0,sizeof(*course));course->count=4;
    const int xyz[4][3]={{-40,0,-40},{40,0,-40},{40,0,40},{-40,0,40}};
    for(unsigned i=0;i<4;++i) {
        course->points[i].position=(sh2z_vec3_t){xyz[i][0]*SH2Z_ONE,0,xyz[i][2]*SH2Z_ONE};
        course->points[i].radius_left=course->points[i].radius_right=8*SH2Z_ONE;
        course->points[i].tension=SH2Z_HALF;
    }
    if(!fzx_activate_course())return 1;
    fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
        .translation={0,0,100*SH2Z_ONE},.focal={180*SH2Z_ONE,180*SH2Z_ONE},
        .center={160*SH2Z_ONE,112*SH2Z_ONE},.near_plane=SH2Z_ONE,.far_plane=1000*SH2Z_ONE,
        .clip={12,16,308,224}};
    for(unsigned i=0;i<1024;++i) {
        fzx_pose_t pose={0};sh2z_vec3_t tangent;
        if(!fzx_spline_sample(course,i%4,(int32_t)((i%64)*1024),&pose.position,&tangent))return 2;
        pose.forward=sh2z_vec3_normalize(tangent);pose.up=(sh2z_vec3_t){0,SH2Z_ONE,0};
        pose.speed=200+i;pose.lap=1+i/384;
        fzx_set_pose(&pose);fzx_set_camera((i&1)?&view:NULL);
        const int32_t hint=fzx_steer_hint(4);
        const fzx_frame_t *frame=fzx_render();
        if(frame->count||frame->speed!=pose.speed||frame->lap!=pose.lap||frame->flags!=FZX_FRAME_IMPORTED||
           fzx_steer_hint(4)!=hint)return 3;
        if(fwrite(frame,16,1,stdout)!=1||fwrite(&hint,4,1,stdout)!=1)return 4;
    }
    return 0;
}
