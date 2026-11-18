#include "finish_menu_native.h"
#if FZX_MENU_TABLES_LOW /* the table lives in reserved low work RAM (menu_tables_low.h) */
#define fzx_finish_menu_images fzx_finish_menu_images_rom_layout
#endif
#include "finish_menu_identity.h"
#if FZX_MENU_TABLES_LOW
#undef fzx_finish_menu_images
#include "menu_tables_low.h"
_Static_assert(sizeof(fzx_finish_menu_images_rom_layout)==FZX_MENU_TABLES_FINISH_BYTES,"fzx_finish_menu_images matches MENUTAB.PAK");
#define fzx_finish_menu_images FZX_MENU_TABLE(fzx_finish_menu_images_rom_layout,FZX_MENU_TABLES_FINISH_ADDR)
#endif
#include <string.h>

static bool vram(const void *p,unsigned bytes) {
    const uintptr_t a=(uintptr_t)p,base=(uintptr_t)VDP1_VRAM(0);
    return a>=base&&a<base+0x80000u&&!(a&7u)&&bytes<=base+0x80000u-a;
}
static uint16_t tint(uint16_t v,uint8_t r,uint8_t g,uint8_t b) {
    return v?(uint16_t)(0x8000u|((v&31u)*r/255u)|
        ((((v>>5)&31u)*g/255u)<<5)|((((v>>10)&31u)*b/255u)<<10)):0;
}
static int maximum(int a,int b){return a>b?a:b;}
static int minimum(int a,int b){return a<b?a:b;}
bool fzx_finish_menu_native(vdp1_cmdt_t *commands,unsigned capacity,
    const fzx_finish_menu_draw_snapshot_t *f,const void *pixels,void *cluts,
    fzx_finish_menu_palette_t *palette,unsigned *count) {
    if(!count)return false;
    *count=0;
    if(!commands||!f||!palette||!f->tick||f->count>FZX_FINISH_MENU_DRAW_MAX||
       !vram(pixels,FZX_FINISH_MENU_BYTES)||!vram(cluts,FZX_FINISH_MENU_CLUT_BYTES))return false;
    const uintptr_t pixel=(uintptr_t)pixels,clut=(uintptr_t)cluts;
    if(pixel<clut+FZX_FINISH_MENU_CLUT_BYTES&&clut<pixel+FZX_FINISH_MENU_BYTES)return false;
    fzx_finish_menu_palette_t pending={0};pending.tick=f->tick;pending.frame=f->frame;
    unsigned n=0,glyphs=0;
    for(unsigned i=0;i<f->count;++i) {
        const fzx_results_draw_t *d=&f->draws[i];
        if(d->kind>FZX_RESULTS_RECT||d->x2<=d->x||d->y2<=d->y||
           d->clip_x<0||d->clip_x2>320||d->clip_y<0||d->clip_y2>240||
           d->clip_x2<=d->clip_x||d->clip_y2<=d->clip_y||d->sx||d->sy)return false;
        const int left=maximum(maximum(d->clip_x,d->x),0);
        const int top=maximum(maximum(d->clip_y,d->y),8)-8;
        const int right=minimum(minimum(d->clip_x2,d->x2),320);
        const int bottom=minimum(minimum(d->clip_y2,d->y2),232)-8;
        if(right<=left||bottom<=top||!d->a)continue;
        if(n+2>capacity)return false;
        vdp1_cmdt_t *c=commands+n++;memset(c,0,sizeof(*c));vdp1_cmdt_user_clip_coord_set(c);
        c->cmd_xa=(int16_t)left;c->cmd_ya=(int16_t)top;
        c->cmd_xc=(int16_t)(right-1);c->cmd_yc=(int16_t)(bottom-1);
        c=commands+n++;memset(c,0,sizeof(*c));
        const bool solid=d->kind==FZX_RESULTS_RECT;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=solid,
            .user_clipping_mode=2,.color_mode=solid?VDP1_CMDT_CM_RGB_32768:
                d->kind==FZX_RESULTS_IMAGE?VDP1_CMDT_CM_CB_256:VDP1_CMDT_CM_CLUT_16,
            .cc_mode=solid&&d->a<192?VDP1_CMDT_CC_HALF_TRANSPARENT:VDP1_CMDT_CC_REPLACE};
        vdp1_cmdt_draw_mode_set(c,mode);
        if(solid) {
            vdp1_cmdt_polygon_set(c);
            c->cmd_colr=(uint16_t)(0x8000u|(d->r>>3)|((d->g>>3)<<5)|((d->b>>3)<<10));
            c->cmd_xa=(int16_t)left;c->cmd_ya=(int16_t)top;
            c->cmd_xb=(int16_t)(right-1);c->cmd_yb=(int16_t)top;
            c->cmd_xc=c->cmd_xb;c->cmd_yc=(int16_t)(bottom-1);
            c->cmd_xd=(int16_t)left;c->cmd_yd=c->cmd_yc;
            continue;
        }
        if(d->a!=255||d->image>=FZX_FINISH_MENU_IMAGES)return false;
        const fzx_finish_menu_image_t *a=&fzx_finish_menu_images[d->image];
        if(!a->width||(a->width&7u)||a->width>504||!a->height||a->height>255||
           a->offset>FZX_FINISH_MENU_BYTES||a->bytes>FZX_FINISH_MENU_BYTES-a->offset||
           (a->offset&7u)||a->source_width!=d->w||a->height!=d->h)return false;
        vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(pixel+a->offset));
        vdp1_cmdt_char_size_set(c,a->width,a->height);
        if(d->kind==FZX_RESULTS_IMAGE) {
            if(d->image>=24||a->bpp!=8||a->palette_count!=32||a->bytes!=a->width*a->height||
               d->x2-d->x!=d->w||d->y2-d->y!=d->h)return false;
            const unsigned bank=d->r==255&&d->g==255&&d->b==255?0:
                d->r==128&&d->g==128&&d->b==128?2:1;
            for(unsigned j=0;j<32;++j) {
                const uint16_t value=tint(a->palette[j],d->r,d->g,d->b);
                if((pending.used&(1u<<bank))&&pending.words[bank][j]!=value)return false;
                pending.words[bank][j]=value;
            }
            pending.used|=(uint8_t)(1u<<bank);
            vdp1_cmdt_normal_sprite_set(c);c->cmd_colr=(uint16_t)((bank+1u)<<8);
            c->cmd_xa=d->x;c->cmd_ya=(int16_t)(d->y-8);
        } else {
            /* The admitted finish menu uses the square Font5 cursor. Other
             * scaled/padded glyph shapes need their own sampling comparison. */
            if(a->bpp!=4||a->palette_count!=16||a->width!=d->w||
               a->bytes!=a->width*a->height/2u||glyphs>=FZX_FINISH_MENU_DRAW_MAX)return false;
            volatile uint16_t *table=(volatile uint16_t *)(clut+glyphs*32u);
            for(unsigned j=0;j<16;++j)table[j]=tint(a->palette[j],d->r,d->g,d->b);
            c->cmd_colr=(uint16_t)(((clut+glyphs++*32u)&0x7ffffu)>>3);
            vdp1_cmdt_distorted_sprite_set(c);
            c->cmd_xa=d->x;c->cmd_ya=(int16_t)(d->y-8);
            c->cmd_xb=(int16_t)(d->x2-1);c->cmd_yb=c->cmd_ya;
            c->cmd_xc=c->cmd_xb;c->cmd_yc=(int16_t)(d->y2-9);
            c->cmd_xd=c->cmd_xa;c->cmd_yd=c->cmd_yc;
        }
    }
    *palette=pending;*count=n;return true;
}
bool fzx_finish_menu_palette_upload(const fzx_finish_menu_palette_t *p,
    volatile uint16_t *cram,unsigned words) {
    if(!p||!cram||((uintptr_t)cram&1u)||words<1024u||!p->tick||(p->used&~7u))return false;
    for(unsigned bank=0;bank<3;++bank)if(p->used&(1u<<bank))
        for(unsigned j=0;j<32;++j)cram[(bank+1u)*256u+j]=p->words[bank][j];
    return true;
}
