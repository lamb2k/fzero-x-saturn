#include "fzx_finish_draw.h"
#include <limits.h>

bool fzx_finish_view(const int32_t m[18],sh2z_view_t *out) {
    if(!m||!out||m[3]||m[7]||m[11]||m[15]!=65536||m[16]<=0||m[17]<=0||
       m[16]>INT32_MAX/160||m[17]>INT32_MAX/120)return false;
    sh2z_view_t v={0};
    for(unsigned r=0;r<3;++r) {
        for(unsigned c=0;c<3;++c) {
            const int32_t n=m[c*4+r]/4;
            if(n < -32767||n>32767)return false;
            v.r[r][c]=(int16_t)(r==2?-n:n);
        }
        v.t[r]=m[12+r]/4;if(r==2)v.t[r]=-v.t[r];
    }
    v.focal_x=m[16]*160;v.focal_y=m[17]*120;
    /* SH2ZAM's pixel-center convention matches the other source presenters. */
    v.center_x=160;v.center_y=111;
    v.near_z=16*16384;v.far_z=4096*16384;v.clip[2]=319;v.clip[3]=223;
    return sh2z_view_init(out,&v);
}

bool fzx_finish_prepare(const fzx_finish_pack_t *p,fzx_finish_work_t *w) {
    return w&&fzx_finish_positions(p,w->positions,FZX_FINISH_VERTICES);
}

bool fzx_finish_draw(const fzx_finish_pack_t *p,const sh2z_view_t *view,fzx_finish_work_t *w,
                     fzx_finish_emit_t emit,void *context,unsigned *count) {
    if(!count)return false;
    *count=0;
    if(!p||!p->data||!view||!w||!emit||
       !sh2z_view_batch_divu(w->projected,view,w->positions,sizeof(w->positions[0]),FZX_FINISH_VERTICES))return false;
    for(unsigned i=0;i<FZX_FINISH_TRIANGLES;++i) {
        fzx_finish_triangle_t t;if(!fzx_finish_triangle(p,i,&t))return false;
        const sh2z_view_vertex_t *a=&w->projected[t.vertex[0]],*b=&w->projected[t.vertex[1]],*c=&w->projected[t.vertex[2]];
        if(a->outcode&b->outcode&c->outcode)continue;
        const sh2z_view_vertex_t *vertices[3]={a,b,c};
        for(unsigned j=0;j<3;++j) {
            const sh2z_view_vertex_t *v=vertices[j];
            if((v->outcode&(SH2Z_OUT_NEAR|SH2Z_OUT_FAR))||v->sx < -1024||v->sx>1023||v->sy < -1024||v->sy>1023)return false;
        }
        const int32_t area=(b->sx-a->sx)*(c->sy-a->sy)-(b->sy-a->sy)*(c->sx-a->sx);
        if(area>=0)continue;
        fzx_finish_quad_t q={0};q.triangle=(uint16_t)i;q.material=t.material;q.image=t.image;
        for(unsigned j=0;j<3;++j){q.x[j]=vertices[j]->sx;q.y[j]=vertices[j]->sy;}
        q.x[3]=q.x[2];q.y[3]=q.y[2];
        if(!emit(context,&q))return false;
        ++*count;
    }
    return true;
}
