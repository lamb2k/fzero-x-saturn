#include "fzx_vehicle.h"
#include <limits.h>
#include <string.h>

/* FZX_VEHICLE_POSE1 (default 0): build the pose with SH2ZAM pose-1
 * sh2z_pose_from_f32, field for field equal to the binary32 procedure below,
 * from a base view built once per camera. Stand-in marker: pending the pose-1
 * tag; until then the procedure below is the stand-in. */
#ifndef FZX_VEHICLE_POSE1
#define FZX_VEHICLE_POSE1 0
#endif
#if FZX_VEHICLE_POSE1
#include <stddef.h>
_Static_assert(sizeof(fzx_vehicle_pose_t)==sizeof(sh2z_pose_t)&&
               offsetof(fzx_vehicle_pose_t,eye)==offsetof(sh2z_pose_t,eye)&&
               offsetof(fzx_vehicle_pose_t,basis)==offsetof(sh2z_pose_t,basis)&&
               offsetof(fzx_vehicle_pose_t,eye_error)==offsetof(sh2z_pose_t,eye_error)&&
               offsetof(fzx_vehicle_pose_t,octant)==offsetof(sh2z_pose_t,octant)&&
               offsetof(fzx_vehicle_pose_t,cull_valid)==offsetof(sh2z_pose_t,cull_valid),
               "fzx_vehicle_pose_t must match sh2z_pose_t");
bool fzx_vehicle_pose(fzx_vehicle_pose_t *out,const fzx_view_t *view,
                     const RefCamera *camera,const RefMachine *state) {
    /* One base per camera: rebuilt only when the fzx view changes. */
    static fzx_view_t cached_view;static sh2z_view_t base;static bool base_valid;
    if(!out||!view||!camera||!state||camera->faults||state->faults||
       state->character||state->custom_type)return false;
    if(!base_valid||memcmp(&cached_view,view,sizeof(*view))) {
        const int32_t origin[3]={0,0,0};base_valid=false;
        if(!fzx_view16_from_source(&base,view,origin))return false;
        cached_view=*view;base_valid=true;
    }
    return sh2z_pose_from_f32((sh2z_pose_t *)(void *)out,&base,(const int32_t (*)[3])view->rotation,
        (const float (*)[3])state->model_basis,state->model_scale,state->model_pos,camera->eye,SH2Z_POSE_REVERSE_AXES);
}
#else
static bool finite_float(float x) {
    uint32_t bits;memcpy(&bits,&x,sizeof(bits));
    return (bits&0x7f800000u)!=0x7f800000u;
}
static bool fixed(float x,float scale,float bound,int32_t *out) {
    if(!finite_float(x)||x < -bound||x>bound)return false;
    const float product=x*scale;
    if(product<=-2147483648.0f||product>=2147483648.0f)return false;
    *out=(int32_t)product;return true;
}
static int64_t rounded(int64_t x,int64_t divisor) {
    return (x>=0?x+divisor/2:-(-x+divisor/2))/divisor;
}

bool fzx_vehicle_pose(fzx_vehicle_pose_t *out,const fzx_view_t *view,
                     const RefCamera *camera,const RefMachine *state) {
    if(!out||!view||!camera||!state||camera->faults||state->faults||
       state->character||state->custom_type)return false;
    fzx_vehicle_pose_t next={0};const int32_t origin[3]={0,0,0};
    if(!fzx_view16_from_source(&next.view,view,origin))return false;
    int32_t basis[3][3],model[3][3],delta[3],inverse_scale[3];
    for(unsigned i=0;i<3;++i) {
        if(!finite_float(camera->eye[i])||!finite_float(state->model_pos[i])||
           !fixed(state->model_pos[i]-camera->eye[i],65536.0f,4096.0f,&delta[i]))return false;
        const float scale=state->model_scale[2-i];
        if(!finite_float(scale)||scale<0.0625f||scale>1.0f||
           !fixed(1.0f/scale,65536.0f,16.0f,&inverse_scale[i]))return false;
        for(unsigned j=0;j<3;++j) {
            if(view->rotation[i][j]<-1100000000||view->rotation[i][j]>1100000000||
               !fixed(state->model_basis[2-j][i],1073741824.0f,1.00001f,&basis[i][j])||
               !fixed(state->model_basis[2-j][i]*state->model_scale[2-j],65536.0f,1.00001f,&model[i][j]))return false;
            next.basis[i][j]=(int16_t)rounded(basis[i][j],65536);
        }
    }
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<3;++j) {
            int64_t value=0;
            for(unsigned k=0;k<3;++k)value+=(int64_t)view->rotation[i][k]*model[k][j];
            const int64_t q=rounded(value,INT64_C(4294967296));
            if(q<INT16_MIN||q>INT16_MAX)return false;
            next.view.r[i][j]=(int16_t)q;
        }
        /* Anchor at the original camera eye before quantizing the model view.
         * This avoids cancellation of two large world-space translations. */
        int64_t value=0;
        for(unsigned k=0;k<3;++k)value+=(int64_t)view->rotation[i][k]*delta[k];
        const int64_t q=rounded(value,INT64_C(4294967296));
        if(q<INT32_MIN||q>INT32_MAX)return false;
        next.view.t[i]=(int32_t)q;
    }
    next.cull_valid=true;next.eye_error=2;
    /* Culling uses the inverse of the source orthonormal basis. Test its
     * actual quantized columns before using the transpose as the inverse.
     * The 512/Q30 residual, bounded delta and scale limits keep inverse,
     * conversion and integer-eye rounding errors below two model units. */
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        int64_t product=0;
        for(unsigned k=0;k<3;++k)product+=(int64_t)basis[k][i]*basis[k][j];
        const int64_t error=rounded(product,INT64_C(1073741824))-(i==j?INT64_C(1073741824):0);
        if(error< -512||error>512)next.cull_valid=false;
    }
    for(unsigned i=0;i<3;++i) {
        int64_t dot=0;
        for(unsigned k=0;k<3;++k)dot-=(int64_t)basis[k][i]*delta[k];
        const int64_t local=rounded(dot,INT64_C(1073741824));
        const int64_t eye=rounded(local*inverse_scale[i],INT64_C(4294967296));
        if(eye< -30000||eye>30000)next.cull_valid=false;
        if(eye<INT32_MIN||eye>INT32_MAX)return false;
        next.eye[i]=(int32_t)eye;
        if(eye>=0)next.octant|=(uint8_t)(1u<<i);
    }
    if(!sh2z_view_init(&next.view,&next.view))return false;
    *out=next;return true;
}
#endif
