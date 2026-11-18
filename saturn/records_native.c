#include "records_native.h"
#if FZX_DEFER_PROJECTION
#include "sim.h"
#endif
#if FZX_MENU_TABLES_LOW /* the table lives in reserved low work RAM (menu_tables_low.h) */
#define fzx_records_images fzx_records_images_rom_layout
#endif
#include "records_identity.h"
#if FZX_MENU_TABLES_LOW
#undef fzx_records_images
#include "menu_tables_low.h"
_Static_assert(sizeof(fzx_records_images_rom_layout)==FZX_MENU_TABLES_RECORDS_BYTES,"fzx_records_images matches MENUTAB.PAK");
#define fzx_records_images FZX_MENU_TABLE(fzx_records_images_rom_layout,FZX_MENU_TABLES_RECORDS_ADDR)
#endif
#include "records_models_identity.h"
#include "results_identity.h"
#include <string.h>

typedef struct {
    vdp1_cmdt_t *commands;unsigned capacity,count,sprites,palette_count;
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
    if(w->count+2>w->capacity||w->sprites>=FZX_RECORDS_DRAW_MAX)return false;
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
            if(w->palette_count>=FZX_RECORDS_PALETTE_BYTES/32u)return false;
            volatile uint16_t *table=(volatile uint16_t *)(w->palettes+w->palette_count*32u);
            for(unsigned i=0;i<16;++i) {
                const uint16_t v=fzx_records_images[s->image].palette[i];
                table[i]=v?(uint16_t)(0x8000u|((v&31u)*s->r/255u)|
                    ((((v>>5)&31u)*s->g/255u)<<5)|((((v>>10)&31u)*s->b/255u)<<10)):0;
            }
            c->cmd_colr=(uint16_t)(((w->palettes+w->palette_count*32u)&0x7ffffu)>>3);
            ++w->palette_count;
        }
    }
    ++w->sprites;return true;
}

static bool vram(const void *p,unsigned bytes) {
    const uintptr_t address=(uintptr_t)p;
    return address>=(uintptr_t)VDP1_VRAM(0)&&address<(uintptr_t)VDP1_VRAM(0)+0x80000u&&!(address&7u)&&
           bytes<=0x80000u-(address&0x7ffffu);
}
bool fzx_records_model(unsigned character,uint32_t rgb,unsigned *index,unsigned *offset,unsigned *bytes) {
    if(character>=30||!index||!offset||!bytes)return false;
    for(unsigned skin=0;skin<4;++skin) {
        const FzxRecordsModelAsset *a=&fzx_records_models[character][skin];
        if(a->body_rgb24!=rgb)continue;
        const unsigned bank=FZX_RECORDS_MODELS_BYTES-FZX_RECORDS_MODELS_BANK_OFFSET;
        if(a->pixel_offset>bank||a->pixel_bytes>bank-a->pixel_offset||
           a->palette_bytes>bank-a->pixel_offset-a->pixel_bytes||
           a->pixel_bytes>FZX_RECORDS_MODELS_MAX_VRAM_BYTES||
           a->palette_bytes>FZX_RECORDS_MODELS_MAX_VRAM_BYTES-a->pixel_bytes||
           a->palette_offset!=a->pixel_offset+a->pixel_bytes)return false;
        *index=character*4u+skin;*offset=FZX_RECORDS_MODELS_BANK_OFFSET+a->pixel_offset;
        *bytes=a->pixel_bytes+a->palette_bytes;return true;
    }
    return false;
}
bool fzx_records_model_camera(void) {
    int32_t m[18];
#if FZX_DEFER_PROJECTION
    ref_camera_flush(1); /* the generated matrix bridge reads gCameras[1] directly */
#endif
    if(!fzx_records_machine_matrix(m))return false;
    for(unsigned i=0;i<18;++i)if(m[i]!=fzx_records_models_matrix_q16[i])return false;
    return true;
}
static bool spinner(vdp1_cmdt_t *c,const fzx_records_draw_snapshot_t *p,ui_context_t *w) {
    int32_t m[18];sh2z_view_t init={0},view;sh2z_view_vertex_t v[4];
    const fzx_records_spinner_draw_t *s=&p->spinner;
    if(s->image>=FZX_RECORDS_UI_IMAGES||!fzx_records_spinner_matrix(p,m)||
       m[3]||m[7]||m[11]||m[15]!=65536||m[16]<=0||m[16]>INT32_MAX/320||
       m[17]<=0||m[17]>INT32_MAX/240)return false;
    const fzx_results_image_t *a=&fzx_records_images[s->image];
    if(a->width!=s->width||a->height!=s->height)return false;
    if(a->clut&&w->palette_count>=FZX_RECORDS_PALETTE_BYTES/32u)return false;
    for(unsigned r=0;r<3;++r) {
        for(unsigned col=0;col<3;++col) {
            const int32_t n=m[col*4+r]/4;
            if(n < -32767||n>32767)return false;
            init.r[r][col]=(int16_t)(r==2?-n:n);
        }
        init.t[r]=m[12+r]/4;if(r==2)init.t[r]=-init.t[r];
    }
    init.focal_x=m[16]*320;init.focal_y=m[17]*240;
    init.center_x=s->x;init.center_y=(int16_t)(s->y-8);
    init.near_z=16*16384;init.far_z=8129*16384;init.clip[2]=319;init.clip[3]=223;
    const int16_t x=(int16_t)(s->width/2),y=(int16_t)(s->height/2);
    const sh2z_pos16_t positions[4]={{-x,y,0},{x,y,0},{-x,-y,0},{x,-y,0}};
    if(!sh2z_view_init(&view,&init)||!sh2z_view_batch_trunc_divu(v,&view,positions,sizeof(positions[0]),4))return false;
    for(unsigned i=0;i<4;++i)if((v[i].outcode&(SH2Z_OUT_NEAR|SH2Z_OUT_FAR))||
        v[i].sx < -1024||v[i].sx>1023||v[i].sy < -1024||v[i].sy>1023)return false;
    memset(c,0,sizeof(*c));vdp1_cmdt_distorted_sprite_set(c);
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.user_clipping_mode=2,
        .color_mode=a->clut?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(w->pixels+a->offset));
    if(a->clut) {
        /* Losslessly packed Font1 glyphs include the END selector. The source
         * spinner uses the original texture without a primitive-color tint. */
        const uintptr_t address=w->palettes+w->palette_count*32u;
        volatile uint16_t *table=(volatile uint16_t *)address;
        for(unsigned i=0;i<16;++i)table[i]=a->palette[i];
        c->cmd_colr=(uint16_t)((address&0x7ffffu)>>3);
        ++w->palette_count;
    }
    vdp1_cmdt_char_size_set(c,a->width,a->height);
    /* Source's two both-sided triangles share this quad. Saturn uses affine
     * point sampling; rotation and source camera are retained. */
    c->cmd_xa=v[0].sx;c->cmd_ya=v[0].sy;c->cmd_xb=v[1].sx;c->cmd_yb=v[1].sy;
    c->cmd_xc=v[3].sx;c->cmd_yc=v[3].sy;c->cmd_xd=v[2].sx;c->cmd_yd=v[2].sy;
    return true;
}
bool fzx_records_native(vdp1_cmdt_t *c,unsigned capacity,const fzx_records_draw_snapshot_t *f,
    const void *pixels,void *palettes,const void *tints,unsigned tint_level,
    const void *previews,const uint16_t selected[6],unsigned *count) {
    if(!count)return false;*count=0;
    if(!c||!f||!selected||!vram(pixels,FZX_RECORDS_UI_BYTES)||
       !vram(palettes,FZX_RECORDS_PALETTE_BYTES)||!vram(tints,FZX_RESULTS_TINT_BANK_BYTES)||
       !vram(previews,FZX_RECORDS_MODELS_SIX_VRAM_BYTES)||tint_level>15)return false;
    const uintptr_t starts[4]={(uintptr_t)pixels,(uintptr_t)palettes,(uintptr_t)tints,(uintptr_t)previews};
    const unsigned sizes[4]={FZX_RECORDS_UI_BYTES,FZX_RECORDS_PALETTE_BYTES,
        FZX_RESULTS_TINT_BANK_BYTES,FZX_RECORDS_MODELS_SIX_VRAM_BYTES};
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)
        if(starts[i]<starts[j]+sizes[j]&&starts[j]<starts[i]+sizes[i])return false;
    ui_context_t w={.commands=c,.capacity=capacity,.pixels=(uintptr_t)pixels,
        .palettes=(uintptr_t)palettes,.tints=(uintptr_t)tints,.tint_level=tint_level};unsigned sprites;
    if(!fzx_records_ui(f,fzx_records_images,FZX_RECORDS_UI_IMAGES,FZX_RECORDS_UI_BYTES,
                       ui_emit,&w,&sprites))return false;
    if(f->spinner.visible||f->machine_count) {
        if(w.count>=capacity)return false;clip(c+w.count++,0,0,319,223);
    }
    if(f->spinner.visible) {
        if(w.count>=capacity||!spinner(c+w.count++,f,&w))return false;
    }
    for(unsigned i=0;i<f->machine_count;++i) {
        const fzx_records_machine_draw_t *q=&f->machines[i];
        if(selected[i]>=120||w.count>=capacity)return false;
        const FzxRecordsModelAsset *a=&fzx_records_models[selected[i]/4][selected[i]%4];
        const uintptr_t texture=(uintptr_t)previews+i*FZX_RECORDS_MODELS_MAX_VRAM_BYTES;
        vdp1_cmdt_t *cmd=c+w.count++;memset(cmd,0,sizeof(*cmd));vdp1_cmdt_normal_sprite_set(cmd);
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.user_clipping_mode=2,
            .color_mode=a->format==1?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,
            .cc_mode=VDP1_CMDT_CC_REPLACE};
        vdp1_cmdt_draw_mode_set(cmd,mode);vdp1_cmdt_char_base_set(cmd,(vdp1_vram_t)texture);
        vdp1_cmdt_char_size_set(cmd,a->width,a->height);
        cmd->cmd_xa=(int16_t)(q->x+a->x);cmd->cmd_ya=(int16_t)(q->y+a->y-8);
        if(a->format==1)cmd->cmd_colr=(uint16_t)(((texture+a->pixel_bytes)&0x7ffffu)>>3);
    }
    *count=w.count;return true;
}
