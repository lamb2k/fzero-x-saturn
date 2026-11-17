#include "fzx_view16.h"
#include <limits.h>

bool fzx_view16_from_source(sh2z_view_t *out,const fzx_view_t *view,
                            const int32_t eye[3]) {
    if(!out || !view || !eye || view->near_plane<=0 ||
       view->far_plane<=view->near_plane || view->far_plane>INT32_MAX/4 ||
       view->clip[0]>=view->clip[2] || view->clip[1]>=view->clip[3])return false;
    const int64_t x=view->center[0],y=view->center[1];
    const int32_t cx=(int32_t)((x>=0?x+32768:-(-x+32768))/65536);
    const int32_t cy=(int32_t)((y>=0?y+32768:-(-y+32768))/65536);
    if(cx<INT16_MIN || cx>INT16_MAX || cy<=INT16_MIN || cy>INT16_MAX)return false;
    sh2z_view_t next={0};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        const int64_t n=view->rotation[i][j];
        const int64_t q=(n>=0?n+32768:-(-n+32768))/65536;
        if(q<INT16_MIN || q>INT16_MAX)return false;
        next.r[i][j]=(int16_t)q;
    }
    next.focal_x=view->focal[0];next.focal_y=view->focal[1];
    next.center_x=(int16_t)cx;next.center_y=(int16_t)(cy-1);
    next.near_z=view->near_plane*4;next.far_z=view->far_plane*4;
    for(unsigned i=0;i<4;++i)next.clip[i]=(int16_t)(view->clip[i]-(i>=2));
    const sh2z_vec3_t position={eye[0],eye[1],eye[2]};
    /* Quantized rotation and translation must use the same eye. Scaling the
     * old translation independently makes error grow with world position. */
    if(!sh2z_view_place(&next,position))return false;
    *out=next;return true;
}
