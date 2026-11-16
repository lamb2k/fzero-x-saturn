#include "fzx_skyline.h"
#include <limits.h>
#include <string.h>
bool fzx_skyline_draw(const fzx_view_t *view,const RefSkyline *scene,uint32_t capacity,
                     fzx_skyline_emit_fn emit,void *context,fzx_skyline_stats_t *stats) {
    if(!stats)return false;
    memset(stats,0,sizeof(*stats));
    if(!view || !scene || !emit || !capacity || scene->faults || scene->count!=REF_SKYLINE_COUNT ||
       view->near_plane<=0 || view->far_plane<=view->near_plane) {stats->fault=1;return false;}
    for(unsigned instance=0;instance<scene->count;++instance) {
        const RefSkylineInstance *s=&scene->instances[instance];
        if(!s->visible){++stats->clipped;continue;}
        if(s->sprite_id<1 || s->sprite_id>4 || s->visible!=1){stats->fault=1;return false;}
        int32_t points[4][3];
        for(unsigned i=0;i<4;++i) {
            int32_t world[3];
            for(unsigned axis=0;axis<3;++axis) {
                int64_t value=s->matrix[12+axis];
                for(unsigned j=0;j<3;++j)value+=(int64_t)scene->vertices[i].position[j]*s->matrix[j*4+axis];
                value/=16; /* source 16.16 matrix to native world at 1/16 scale */
                if(value<INT32_MIN || value>INT32_MAX){stats->fault=2;return false;}
                world[axis]=(int32_t)value;
            }
            if(!fzx_view_transform(view,world,points[i])){stats->fault=2;return false;}
        }
        bool hidden=false;
        for(unsigned plane=0;plane<6;++plane) {
            unsigned outside=0;
            for(unsigned i=0;i<4;++i)outside+=fzx_view_plane(view,points[i],plane)<0;
            if(outside==4){hidden=true;break;}
        }
        if(hidden){++stats->clipped;continue;}
        fzx_skyline_quad_t q={.texture=s->sprite_id-1};
        static const unsigned order[4]={0,1,3,2};
        for(unsigned i=0;i<4;++i) {
            int32_t xy[2];const int32_t *p=points[order[i]];
            if(p[2]<view->near_plane || !fzx_view_project(view,p,xy)){stats->fault=3;return false;}
            const int64_t x=((int64_t)xy[0]+32768)/65536,y=((int64_t)xy[1]+32768)/65536;
            if(x<-4096 || x>4095 || y<-4096 || y>4095){stats->fault=3;return false;}
            q.x[i]=(int16_t)x;q.y[i]=(int16_t)y;
        }
        if(stats->polygons>=capacity){stats->fault=4;return false;}
        if(!emit(context,&q)){stats->fault=5;return false;}
        ++stats->polygons;
    }
    return true;
}
