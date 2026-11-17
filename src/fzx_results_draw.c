#include "fzx_results_draw.h"
#include <limits.h>

static int max(int a,int b){return a>b?a:b;}
static int min(int a,int b){return a<b?a:b;}

bool fzx_results_ui(const fzx_results_snapshot_t *f,const fzx_results_image_t *images,
                    unsigned image_count,const uint16_t (*font)[2],unsigned glyph_count,
                    uint32_t bytes,fzx_results_emit_t emit,void *context,unsigned *count) {
    if(!count)return false;
    *count=0;
    if(!f||!images||!image_count||!font||!glyph_count||!emit||!bytes||
       bytes>0x80000u||f->count>FZX_RESULTS_DRAW_MAX)return false;
    for(unsigned i=0;i<f->count;++i) {
        const fzx_results_draw_t *q=&f->draws[i];
        if(q->kind>FZX_RESULTS_RECT||q->x2<q->x||q->y2<q->y||q->w<0||q->h<0||
           q->x2-q->x!=q->w||q->y2-q->y!=q->h||q->clip_x<0||q->clip_y<0||
           q->clip_x2>320||q->clip_y2>240||q->clip_x2<q->clip_x||q->clip_y2<q->clip_y)return false;
        fzx_results_sprite_t s={0};s.kind=q->kind;s.r=q->r;s.g=q->g;s.b=q->b;s.a=q->a;
        if(q->y<INT16_MIN+8)return false;
        s.x=q->x;s.y=(int16_t)(q->y-8);
        s.clip[0]=(int16_t)max(q->x,q->clip_x);
        s.clip[1]=(int16_t)(max(max(q->y,q->clip_y),8)-8);
        s.clip[2]=(int16_t)min(q->x2,q->clip_x2);
        s.clip[3]=(int16_t)(max(8,min(min(q->y2,q->clip_y2),232))-8);
        if(q->kind!=FZX_RESULTS_RECT) {
            unsigned image=q->image;
            if(q->kind==FZX_RESULTS_GLYPH) {
                if(image>=glyph_count)return false;
                image=font[image][0];
            }
            if(image>=image_count||image>UINT16_MAX)return false;
            const fzx_results_image_t *t=&images[image];
            if(!t->width||t->width>504||t->width%8||!t->height||t->height>255||
               !t->source_width||t->source_width>t->width||t->clut>1||t->reserved||
               t->offset%8||t->offset>bytes||t->bytes>bytes-t->offset||
               t->bytes!=(uint32_t)t->width*t->height*(t->clut?1u:4u)/2u||
               q->sx<0||q->sy<0||q->sx+q->w>t->source_width||q->sy+q->h>t->height||
               q->x-q->sx<INT16_MIN)return false;
            s.image=(uint16_t)image;s.clut=t->clut;
            s.width=t->width;s.height=(uint16_t)q->h;
            s.x=(int16_t)(q->x-q->sx);
            s.offset=t->offset+(uint32_t)q->sy*t->width*(t->clut?1u:4u)/2u;
            if(s.offset%8)return false;
        } else {s.width=(uint16_t)q->w;s.height=(uint16_t)q->h;}
        if(!s.a||s.clip[0]>=s.clip[2]||s.clip[1]>=s.clip[3])continue;
        if(!emit(context,&s))return false;
        ++*count;
    }
    return true;
}
