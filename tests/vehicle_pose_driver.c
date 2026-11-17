#include "fzx_vehicle.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int exercise(void) {
    unsigned samples=0;double max_error=0;
    for(unsigned turn=0;turn<64;++turn) {
        const double angle=(double)turn*0.098174770424681;
        const float c=(float)cos(angle),s=(float)sin(angle);
        RefCamera camera={0};camera.eye[0]=30000.0f;camera.eye[1]=-20000.0f;camera.eye[2]=10000.0f;
        RefMachine state={0};state.model_pos[0]=30020.0f;state.model_pos[1]=-19990.0f;state.model_pos[2]=10400.0f;
        state.model_basis[0][0]=c;state.model_basis[0][2]=s;state.model_basis[1][1]=1;
        state.model_basis[2][0]=-s;state.model_basis[2][2]=c;
        for(unsigned i=0;i<3;++i)state.model_scale[i]=0.1f+(float)i*0.025f;
        const fzx_view_t view={.rotation={{1073741824,0,0},{0,1073741824,0},{0,0,1073741824}},
            .focal={160*65536,160*65536},.center={160*65536,112*65536},
            .near_plane=16*4096,.far_plane=2000*4096,.clip={12,16,308,224}};
        fzx_vehicle_pose_t pose;
        if(!fzx_vehicle_pose(&pose,&view,&camera,&state)||!pose.cull_valid||pose.eye_error!=2)return 1;
        for(unsigned axis=0;axis<3;++axis) {
            double eye=0;
            for(unsigned k=0;k<3;++k)eye+=((double)camera.eye[k]-state.model_pos[k])*state.model_basis[2-axis][k];
            eye/=state.model_scale[2-axis];
            if(fabs(eye-pose.eye[axis])>pose.eye_error)return 2;
        }
        sh2z_pos16_t positions[32];sh2z_view_vertex_t projected[32];
        for(unsigned i=0;i<32;++i)positions[i]=(sh2z_pos16_t){(int16_t)((int)i*13-200),(int16_t)((int)(i%5)*20-40),(int16_t)((int)(i%7)*31-90)};
        if(!sh2z_view_batch_divu(projected,&pose.view,positions,sizeof(*positions),32))return 3;
        for(unsigned i=0;i<32;++i) {
            const int16_t point[3]={positions[i].x,positions[i].y,positions[i].z};
            double xyz[3];
            for(unsigned k=0;k<3;++k) {
                xyz[k]=(double)state.model_pos[k]-camera.eye[k];
                for(unsigned j=0;j<3;++j)xyz[k]+=point[j]*(double)state.model_basis[2-j][k]*state.model_scale[2-j];
            }
            const double x=160+160*xyz[0]/xyz[2],y=112-160*xyz[1]/xyz[2];
            const double error=fmax(fabs(projected[i].sx-trunc(x)),fabs(projected[i].sy-trunc(y)));
            if(error>max_error)max_error=error;
            if(error>1)return 4;
            ++samples;
        }
        const fzx_vehicle_pose_t saved=pose;state.model_scale[0]=0;
        if(fzx_vehicle_pose(&pose,&view,&camera,&state)||memcmp(&saved,&pose,sizeof(pose)))return 5;
    }
    printf("vehicle pose %u projected samples PASS max %.0f px\n",samples,max_error);return 0;
}
int main(void){return exercise();}
