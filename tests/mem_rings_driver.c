/* FZX_MEM_NO_PROTO_RINGS checks (C-048), with the native core empty.
 * mode 'f': follow 1,024 poses round a square course and print every frame
 *           header (identical with and without the rings);
 * mode 't': the autopilot hint on a turn (rings build only);
 * mode 'r': request the autopilot hint (the no-rings build must reject it). */
#include "fzx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if FZX_MEM_NO_PROTO_RINGS
void fzx_autopilot_unsupported(void) { fputs("REJECTED\n", stderr); exit(42); }
#endif

static fzx_course_t *square(void) {
    fzx_course_t *course=fzx_course_buffer();
    memset(course,0,sizeof(*course));course->count=4;
    const int xyz[4][3]={{-40,0,-40},{40,0,-40},{40,0,40},{-40,0,40}};
    for(unsigned i=0;i<4;++i) {
        course->points[i].position=(sh2z_vec3_t){xyz[i][0]*SH2Z_ONE,0,xyz[i][2]*SH2Z_ONE};
        course->points[i].radius_left=course->points[i].radius_right=8*SH2Z_ONE;
        course->points[i].tension=SH2Z_HALF;
    }
    return course;
}

static void pose_at(const fzx_course_t *course,unsigned segment,int32_t t,fzx_pose_t *pose) {
    sh2z_vec3_t tangent;memset(pose,0,sizeof(*pose));
    if(!fzx_spline_sample(course,segment,t,&pose->position,&tangent))exit(2);
    pose->forward=sh2z_vec3_normalize(tangent);pose->up=(sh2z_vec3_t){0,SH2Z_ONE,0};
    pose->speed=200;pose->lap=1;
}

int main(int argc,char **argv) {
    const char mode=argc>1?argv[1][0]:'f';
    fzx_init();
    fzx_course_t *course=square();
    if(!fzx_activate_course())return 1;
    if(mode=='f') {
        for(unsigned i=0;i<1024;++i) {
            fzx_pose_t pose;pose_at(course,i%4,(int32_t)((i%64)*1024),&pose);
            pose.speed=200+i;pose.lap=1+i/384;
            fzx_set_pose(&pose);
            const fzx_frame_t *frame=fzx_render();
            if(fwrite(frame,16,1,stdout)!=1)return 4;
        }
        return 0;
    }
    /* Centred late on the first straight, heading for the corner. */
    fzx_pose_t pose;pose_at(course,0,(int32_t)(0.85*65536),&pose);
    fzx_set_pose(&pose);
    const int32_t near=fzx_steer_hint(1),far=fzx_steer_hint(24);
    printf("%d %d\n",(int)near,(int)far);
    return 0;
}
