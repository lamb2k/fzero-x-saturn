#include "fzx_countdown_draw.h"

static unsigned u16(const uint8_t *p){return (unsigned)p[0]*256u+p[1];}
bool fzx_countdown_draw_prepare(fzx_countdown_draw_work_t *w,const fzx_countdown_pack_t *p){
    if(!w)return false;
    w->prepared=false;
    if(!fzx_countdown_positions(p,w->positions,FZX_COUNTDOWN_VERTICES))return false;
    w->pack=p;w->prepared=true;return true;
}
static int32_t area(const sh2z_view_guard_vertex_t *v,unsigned count){
    int32_t result=0;
    for(unsigned i=0;i<count;++i){const unsigned j=(i+1)%count;
        result+=(int32_t)v[i].v.sx*v[j].v.sy-(int32_t)v[i].v.sy*v[j].v.sx;}
    return result;
}
static bool front(const sh2z_view_guard_vertex_t v[4]){
    /* Face winding must precede pixel rounding: a thin authored front face
     * can otherwise become a zero-area or reversed integer-screen quad.
     * Uniformly bound view coordinates to 19 signed magnitude bits. Each
     * determinant term then fits 58 bits, and their sum fits signed 64 bits.
     * This also handles mirrored model/view transforms without normal rules. */
    int64_t largest=0;
    for(unsigned i=0;i<3;++i){
        const int32_t values[3]={v[i].v.x,v[i].v.y,v[i].v.z};
        for(unsigned k=0;k<3;++k){const int64_t value=values[k]<0?-(int64_t)values[k]:values[k];
            if(value>largest)largest=value;}
    }
    int32_t scale=1;
    while(largest>524287){largest/=2;scale*=2;}
    const int64_t x0=v[0].v.x/scale,y0=v[0].v.y/scale,z0=v[0].v.z/scale;
    const int64_t x1=v[1].v.x/scale,y1=v[1].v.y/scale,z1=v[1].v.z/scale;
    const int64_t x2=v[2].v.x/scale,y2=v[2].v.y/scale,z2=v[2].v.z/scale;
    return x0*(y1*z2-z1*y2)-y0*(x1*z2-z1*x2)+z0*(x1*y2-y1*x2)>0;
}
bool fzx_countdown_draw(fzx_countdown_draw_work_t *w,const fzx_view_t *view,
    const RefCountdown *state,fzx_countdown_command_t *commands,uint32_t capacity,
    uint32_t *count,fzx_countdown_draw_stats_t *stats){
    if(count)*count=0;
    if(!stats)return false;
    *stats=(fzx_countdown_draw_stats_t){0};stats->fault=FZX_COUNTDOWN_DRAW_INVALID;
    if(!count||!w||!w->prepared||!w->pack||!view||!state||state->faults||!commands||!capacity||
       view->clip[0]<-1024||view->clip[1]<-1024||view->clip[2]>1024||view->clip[3]>1024||
       !fzx_countdown_phase(state->timer,&stats->phase))return false;
    if(!stats->phase.visible){stats->fault=0;return true;}
    sh2z_view_t model_view;
    if(!fzx_countdown_view(&model_view,view,state))return false;
    stats->fault=FZX_COUNTDOWN_DRAW_PROJECT;
    if(!sh2z_view_batch_divu(w->projected,&model_view,w->positions,sizeof(w->positions[0]),FZX_COUNTDOWN_VERTICES))return false;
    for(unsigned quad=0;quad<FZX_COUNTDOWN_QUADS;++quad){
        ++stats->quads;stats->error_quad=quad;
        const uint8_t *q=w->pack->data+w->pack->quad_at+quad*16;
        unsigned all=63;bool safe=true;
        for(unsigned i=0;i<4;++i){
            const sh2z_view_vertex_t v=w->projected[u16(q+i*2)];
            w->input[i].v=v;all&=v.outcode;
            if((v.outcode&(SH2Z_OUT_NEAR|SH2Z_OUT_FAR))||v.sx<-1024||v.sx>1023||v.sy<-1024||v.sy>1023)safe=false;
        }
        if(all){++stats->culled;continue;}
        if(!front(w->input)){++stats->culled;continue;}
        sh2z_view_guard_vertex_t *corners=w->input;unsigned n=4;
        if(!safe){
            ++stats->guard_checks;stats->fault=FZX_COUNTDOWN_DRAW_CLIP;
            const int clipped=sh2z_view_clip_guard_divu(w->clipped,&model_view,model_view.clip,w->input,4,0);
            if(clipped<0)return false;
            if(clipped<3){++stats->culled;continue;}
            corners=w->clipped;n=(unsigned)clipped;
        }
        if(!safe&&!area(corners,n)){++stats->culled;continue;}
        fzx_countdown_image_t image;stats->fault=FZX_COUNTDOWN_DRAW_INVALID;
        if(!fzx_countdown_image(w->pack,quad,&stats->phase,&image))return false;
        if(!safe&&!image.solid){stats->fault=FZX_COUNTDOWN_DRAW_TEXTURE_CLIP;return false;}
        if(!safe)++stats->clipped_solids;
        for(unsigned first=1;first+1<n;first+=2){
            stats->fault=FZX_COUNTDOWN_DRAW_CAPACITY;
            if(stats->commands>=capacity)return false;
            fzx_countdown_command_t *out=commands+stats->commands;
            *out=(fzx_countdown_command_t){.texture=image.texture,.width=image.width,.height=image.height,
                .quad=(uint16_t)quad,.solid=(uint16_t)image.solid,.color=0x8000};
            const unsigned order[4]={0,first,first+1,first+2<n?first+2:n-1};
            for(unsigned i=0;i<4;++i){out->x[i]=corners[order[i]].v.sx;out->y[i]=corners[order[i]].v.sy;}
            ++stats->commands;
        }
    }
    stats->fault=0;stats->error_quad=0;*count=stats->commands;return true;
}
