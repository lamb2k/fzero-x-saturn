#include "results_native.h"
#include "results_identity.h"
#include <string.h>

typedef struct {
    vdp1_cmdt_t *commands;unsigned capacity,count,sprites;
    uintptr_t pixels,palettes,tints;unsigned tint_level;
} ui_context_t;

static uint16_t color(uint8_t r,uint8_t g,uint8_t b) {
    return (uint16_t)(0x8000u|(r>>3)|((g>>3)<<5)|((b>>3)<<10));
}
static void clip(vdp1_cmdt_t *c,int16_t left,int16_t top,int16_t right,int16_t bottom) {
    memset(c,0,sizeof(*c));vdp1_cmdt_user_clip_coord_set(c);
    c->cmd_xa=left;c->cmd_ya=top;c->cmd_xc=right;c->cmd_yc=bottom;
}
static bool ui_emit(void *opaque,const fzx_results_sprite_t *s) {
    ui_context_t *w=opaque;
    if(w->count+2>w->capacity||w->sprites>=FZX_RESULTS_DRAW_MAX)return false;
    clip(w->commands+w->count++,s->clip[0],s->clip[1],(int16_t)(s->clip[2]-1),(int16_t)(s->clip[3]-1));
    vdp1_cmdt_t *c=w->commands+w->count++;memset(c,0,sizeof(*c));
    const bool solid=s->kind==FZX_RESULTS_RECT;
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=solid,
        .user_clipping_mode=2,.color_mode=(!solid&&s->clut)?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,
        /* Source arbitrary alpha is quantized to Saturn's three levels. */
        .cc_mode=s->a<192?VDP1_CMDT_CC_HALF_TRANSPARENT:VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_draw_mode_set(c,mode);
    if(solid) {
        vdp1_cmdt_polygon_set(c);c->cmd_colr=color(s->r,s->g,s->b);
        c->cmd_xa=s->clip[0];c->cmd_ya=s->clip[1];
        c->cmd_xb=(int16_t)(s->clip[2]-1);c->cmd_yb=s->clip[1];
        c->cmd_xc=c->cmd_xb;c->cmd_yc=(int16_t)(s->clip[3]-1);
        c->cmd_xd=s->clip[0];c->cmd_yd=c->cmd_yc;
    } else {
        uintptr_t pixels=w->pixels+s->offset;
        if(!s->clut&&(s->r!=255||s->g!=255||s->b!=255)) {
            if(s->image>=8||fzx_results_tint_offsets[s->image]==UINT32_MAX||
               s->r!=255||s->g!=s->b||(s->g*15u+127u)/255u!=w->tint_level)return false;
            pixels=w->tints+fzx_results_tint_offsets[s->image]+s->offset-fzx_results_images[s->image].offset;
        }
        vdp1_cmdt_normal_sprite_set(c);c->cmd_xa=s->x;c->cmd_ya=s->y;
        vdp1_cmdt_char_base_set(c,(vdp1_vram_t)pixels);
        vdp1_cmdt_char_size_set(c,s->width,s->height);
        if(s->clut) {
            volatile uint16_t *table=(volatile uint16_t *)(w->palettes+w->sprites*32u);
            for(unsigned i=0;i<16;++i) {
                const uint16_t v=fzx_results_images[s->image].palette[i];
                table[i]=v?(uint16_t)(0x8000u|((v&31u)*s->r/255u)|
                    ((((v>>5)&31u)*s->g/255u)<<5)|((((v>>10)&31u)*s->b/255u)<<10)):0;
            }
            c->cmd_colr=(uint16_t)(((w->palettes+w->sprites*32u)&0x7ffffu)>>3);
        }
    }
    ++w->sprites;return true;
}

static bool vram(const void *p,unsigned bytes) {
    const uintptr_t address=(uintptr_t)p;
    return address>=(uintptr_t)VDP1_VRAM(0)&&address<(uintptr_t)VDP1_VRAM(0)+0x80000u&&!(address&7u)&&
           bytes<=0x80000u-(address&0x7ffffu);
}
bool fzx_results_ui_native(vdp1_cmdt_t *c,unsigned capacity,const fzx_results_snapshot_t *f,
                           const void *pixels,void *palettes,unsigned palette_bytes,
                           const void *tints,unsigned tint_level,unsigned *count) {
    if(!count)return false;
    *count=0;
    if(!c||!vram(pixels,FZX_RESULTS_UI_BYTES)||palette_bytes<FZX_RESULTS_PALETTE_BYTES||
       !vram(palettes,palette_bytes)||!vram(tints,FZX_RESULTS_TINT_BANK_BYTES)||tint_level>15)return false;
    const uintptr_t a=(uintptr_t)pixels,b=(uintptr_t)palettes;
    if(a<b+palette_bytes&&b<a+FZX_RESULTS_UI_BYTES)return false;
    const uintptr_t t=(uintptr_t)tints;
    if((a<t+FZX_RESULTS_TINT_BANK_BYTES&&t<a+FZX_RESULTS_UI_BYTES)||
       (b<t+FZX_RESULTS_TINT_BANK_BYTES&&t<b+palette_bytes))return false;
    ui_context_t w={c,capacity,0,0,a,b,t,tint_level};unsigned sprites;
    if(!fzx_results_ui(f,fzx_results_images,FZX_RESULTS_UI_IMAGES,fzx_results_font3,
                      FZX_RESULTS_FONT3_GLYPHS,FZX_RESULTS_UI_BYTES,ui_emit,&w,&sprites))return false;
    *count=w.count;return true;
}

typedef struct {
    vdp1_cmdt_t *commands;unsigned capacity,count;
    const fzx_finish_pack_t *pack;uintptr_t textures;
} finish_context_t;
static bool finish_emit(void *opaque,const fzx_finish_quad_t *q) {
    finish_context_t *w=opaque;if(w->count>=w->capacity)return false;
    vdp1_cmdt_t *c=w->commands+w->count++;memset(c,0,sizeof(*c));
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2,
        .color_mode=q->material?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_draw_mode_set(c,mode);
    if(q->material) {
        fzx_finish_image_t image;if(!fzx_finish_image(w->pack,q->image,&image))return false;
        vdp1_cmdt_distorted_sprite_set(c);
        vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(w->textures+image.texture));
        vdp1_cmdt_char_size_set(c,image.width,image.height);
        c->cmd_colr=(uint16_t)(((w->textures+w->pack->palette_at)&0x7ffffu)>>3);
    } else {vdp1_cmdt_polygon_set(c);c->cmd_colr=w->pack->solid_color;}
    c->cmd_xa=q->x[0];c->cmd_ya=q->y[0];c->cmd_xb=q->x[1];c->cmd_yb=q->y[1];
    c->cmd_xc=q->x[2];c->cmd_yc=q->y[2];c->cmd_xd=q->x[3];c->cmd_yd=q->y[3];
    return true;
}
bool fzx_finish_native(vdp1_cmdt_t *c,unsigned capacity,const fzx_finish_pack_t *p,
                       const sh2z_view_t *v,fzx_finish_work_t *work,const void *textures,unsigned *count) {
    if(!count)return false;
    *count=0;
    if(!c||!p||!capacity||!vram(textures,p->texture_bytes))return false;
    clip(c,0,0,319,223);finish_context_t w={c,capacity,1,p,(uintptr_t)textures};unsigned triangles;
    if(!fzx_finish_draw(p,v,work,finish_emit,&w,&triangles))return false;
    *count=w.count;return true;
}
