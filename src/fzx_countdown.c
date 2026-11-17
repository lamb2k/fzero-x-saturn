#include "fzx_countdown.h"
#include <limits.h>

bool fzx_countdown_phase(int timer,fzx_countdown_phase_t *out) {
    if(!out || timer<0 || timer>460)return false;
    const fzx_countdown_phase_t next={timer!=0,
        timer!=0 && ((timer>=210 && timer<=220) || (timer>=150 && timer<=160) ||
                     (timer>=90 && timer<=100) || timer<=40),
        (uint8_t)(timer==0 || timer>220?0:timer>160?1:timer>100?2:timer>40?3:4)};
    *out=next;return true;
}

bool fzx_countdown_view(sh2z_view_t *out,const fzx_view_t *view,const RefCountdown *state) {
    if(!out || !view || !state || state->faults || state->timer<=0 || state->timer>460 ||
       state->matrix[3] || state->matrix[7] || state->matrix[11] || state->matrix[15]!=65536)return false;
    sh2z_view_t next;
    const int32_t origin[3]={0,0,0};
    if(!fzx_view16_from_source(&next,view,origin))return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
        if(view->rotation[i][j]<-1100000000 || view->rotation[i][j]>1100000000 ||
           state->matrix[i*4+j]<-65536 || state->matrix[i*4+j]>65536)return false;
    for(unsigned i=0;i<3;++i) {
        for(unsigned j=0;j<3;++j) {
            int64_t n=0;
            for(unsigned k=0;k<3;++k)n+=(int64_t)view->rotation[i][k]*state->matrix[j*4+k];
            const int64_t q=(n>=0?n+INT64_C(2147483648):-(-n+INT64_C(2147483648)))/INT64_C(4294967296);
            if(q<INT16_MIN || q>INT16_MAX)return false;
            next.r[i][j]=(int16_t)q;
        }
        /* Bounds above prove the three products/sum fit signed 64 bits.
         * Keep translation precision until the model/world origins cancel. */
        int64_t n=0;
        for(unsigned k=0;k<3;++k)n+=(int64_t)view->rotation[i][k]*state->matrix[12+k];
        n=n/INT64_C(4294967296)+(int64_t)view->translation[i]*4;
        if(n<INT32_MIN || n>INT32_MAX)return false;
        next.t[i]=(int32_t)n;
    }
    return sh2z_view_init(out,&next);
}
