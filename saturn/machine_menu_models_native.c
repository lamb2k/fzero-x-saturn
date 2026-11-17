#include "machine_menu_native.h"
#include "fzx_machine_menu_model.h"
#include "machine_models_identity.h"
#include "machine_menu_camera_identity.h"
#include "texture_cd.h"
#include <string.h>

/* Report: magic,status,model/chart/effect hashes,screen,character,skin,
 * emitted BASE/OUTLINE/SHADOW/REFLECTION counts,guard count,guard addresses
 * [13..28], chart pixels, Gouraud tables, selected effect pixels. */
volatile uint32_t fzx_machine_menu_model_report[32];
static fzx_mm_pack_t model_pack;
static fzx_mm_charts_t chart_pack;
static fzx_mm_work_t *model_work;
static const uint8_t *reflection_pixels;
static uintptr_t chart_pixels,gouraud_tables,effect_pixels;
static uint32_t gouraud_bytes;
static uint8_t loaded_screen,loaded_character,loaded_skin;
static int32_t source_cameras[2][18];
static volatile uint32_t *model_guards[16];
static unsigned model_guard_count;
static bool model_ready;
enum { MODEL_GUARD=0x4d37a9c1u, MODEL_GUARD_BYTES=32u };
_Static_assert(FZX_MACHINE_MODELS_MAX_VERTICES<=FZX_MM_MAX_VERTICES,"Menu model vertex scratch too small");
_Static_assert(FZX_MACHINE_MODELS_MAX_TRIANGLES<=FZX_MM_MAX_TRIANGLES,"Menu model triangle scratch too small");
_Static_assert(FZX_MACHINE_MODELS_MAX_REFLECTION_VERTICES<=FZX_MM_MAX_VERTICES,"Reflection scratch too small");
_Static_assert(FZX_MACHINE_MODELS_MAX_REFLECTION_TRIANGLES<=FZX_MM_MAX_TRIANGLES,"Reflection order scratch too small");
_Static_assert(FZX_MACHINE_MODELS_EFFECT_SELECTED_VRAM_BYTES==1568u,"Selected effect layout changed");

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint32_t fnv(const uint8_t *p,uint32_t bytes){uint32_t h=2166136261u;while(bytes--)h=(h^*p++)*16777619u;return h;}
static bool model_fail(uint32_t status){fzx_machine_menu_model_report[1]=status;return false;}

typedef struct { uintptr_t next,end; } model_arena_t;
static bool model_arena_init(model_arena_t *a,void *memory,uint32_t bytes) {
    const uintptr_t start=(uintptr_t)memory;
    if(!memory || (start&7u) || start+bytes<start)return false;
    *a=(model_arena_t){start,start+bytes};return true;
}
static void *reserve(model_arena_t *a,uint32_t bytes) {
    if(!bytes || bytes>UINT32_MAX-31u || model_guard_count+2>16)return NULL;
    const uint32_t padded=(bytes+31u)&~31u;
    if(a->next>a->end || padded>a->end-a->next || 2u*MODEL_GUARD_BYTES>a->end-a->next-padded)return NULL;
    volatile uint32_t *before=(volatile uint32_t *)a->next;
    void *data=(void *)(a->next+MODEL_GUARD_BYTES);
    volatile uint32_t *after=(volatile uint32_t *)((uintptr_t)data+padded);
    model_guards[model_guard_count++]=before;model_guards[model_guard_count++]=after;
    for(unsigned i=0;i<MODEL_GUARD_BYTES/4u;++i)before[i]=after[i]=MODEL_GUARD;
    a->next=(uintptr_t)after+MODEL_GUARD_BYTES;
    return data;
}

bool fzx_machine_menu_model_guards(void) {
    if(!model_ready || !model_pack.data || !chart_pack.data || !model_work || !model_guard_count)return false;
    for(unsigned g=0;g<model_guard_count;++g)for(unsigned i=0;i<MODEL_GUARD_BYTES/4u;++i)
        if(model_guards[g][i]!=MODEL_GUARD)return false;
    return true;
}

static bool copy_pixels(void *dest,const uint8_t *source,uint32_t bytes) {
    if(!dest || !source || ((uintptr_t)dest&3u) || (bytes&3u))return false;
    volatile uint32_t *out=dest;
    for(uint32_t i=0;i<bytes/4u;++i) {
        uint32_t word;memcpy(&word,source+i*4u,4);out[i]=word;
        if(out[i]!=word)return false;
    }
    return true;
}

static bool effect_open(const uint8_t *p,unsigned character,const uint8_t **palette,
                         const uint8_t **shadow,const uint8_t **reflection) {
    const uint32_t bytes=FZX_MACHINE_MODELS_EFFECT_BYTES;
    if(!p || character>=30 || bytes<544 || memcmp(p,"FZXME01\0",8) ||
       be32(p+8)!=1 || be32(p+12)!=bytes || be32(p+16)!=30 || be32(p+20)!=64 ||
       be32(p+24)!=544 || be32(p+28)!=bytes-544 || be32(p+40)!=16 || be32(p+44)!=16 ||
       be32(p+48)!=0 || be32(p+52)!=32 || be32(p+56)!=64 || be32(p+60) ||
       fnv(p+64,bytes-64)!=be32(p+32))return false;
    const uint8_t *blob=p+544;const uint32_t blob_bytes=bytes-544,reflection_offset=be32(p+36);
    if((reflection_offset&7u) || reflection_offset>blob_bytes || 512u>blob_bytes-reflection_offset || blob_bytes<32)return false;
    for(unsigned c=0;c<30;++c) {
        const uint8_t *d=p+64+c*16;const uint32_t offset=be32(d);
        if((offset&7u) || offset>blob_bytes || 1024u>blob_bytes-offset || be32(d+8) || be32(d+12) ||
           be32(d+4)!=be32(model_pack.data+64+c*96+56))return false;
    }
    *palette=blob;*shadow=blob+be32(p+64+character*16);*reflection=blob+reflection_offset;
    return true;
}

bool fzx_machine_menu_model_load(void *memory,uint32_t bytes,void *pixels,uint32_t pixel_bytes,
                                uint8_t screen,uint8_t character,uint8_t skin) {
    model_pack=(fzx_mm_pack_t){0};chart_pack=(fzx_mm_charts_t){0};model_work=NULL;
    reflection_pixels=NULL;model_guard_count=0;model_ready=false;
    memset((void *)fzx_machine_menu_model_report,0,sizeof(fzx_machine_menu_model_report));
    fzx_machine_menu_model_report[0]=0x4d4d444cu;fzx_machine_menu_model_report[1]=10;
    if(screen>1 || character>=30 || skin>=4 || ((uintptr_t)memory&31u))return model_fail(2);
    model_arena_t cpu,vram;if(!model_arena_init(&cpu,memory,bytes) || !model_arena_init(&vram,pixels,pixel_bytes))return model_fail(2);
    const uint32_t chart_bytes=screen?fzx_machine_models_settings_identity[character][skin][0]:FZX_MACHINE_MODELS_MINI_BYTES;
    const uint32_t chart_hash=screen?fzx_machine_models_settings_identity[character][skin][1]:FZX_MACHINE_MODELS_MINI_HASH;
    const uint32_t blob_bytes=screen?fzx_machine_models_settings_identity[character][skin][2]:FZX_MACHINE_MODELS_MINI_VRAM_BYTES;
    uint8_t *model_data=reserve(&cpu,FZX_MACHINE_MODELS_BYTES),*chart_data=reserve(&cpu,chart_bytes);
    uint8_t *effect_data=screen?reserve(&cpu,FZX_MACHINE_MODELS_EFFECT_BYTES):NULL;
    model_work=reserve(&cpu,sizeof(*model_work));
    const uint32_t cd_bytes=fzx_texture_cd_scratch_bytes();void *cd=reserve(&cpu,cd_bytes);
    if(!model_data || !chart_data || !model_work || !cd || (screen&&!effect_data))return model_fail(3);
    if(!fzx_texture_cd_load("MMODELS.PAK",FZX_MACHINE_MODELS_BYTES,FZX_MACHINE_MODELS_HASH,model_data,cd,cd_bytes) ||
       !fzx_mm_open(&model_pack,model_data,FZX_MACHINE_MODELS_BYTES,FZX_MACHINE_MODELS_HASH))return model_fail(4);
    char settings_name[]="MS000.PAK";
    settings_name[2]=(char)('0'+character/10u);settings_name[3]=(char)('0'+character%10u);settings_name[4]=(char)('0'+skin);
    const char *chart_name=screen?settings_name:"MMINI.PAK";
    if(!fzx_texture_cd_load(chart_name,chart_bytes,chart_hash,chart_data,cd,cd_bytes) ||
       !fzx_mm_charts_open(&chart_pack,chart_data,chart_bytes,chart_hash,&model_pack,screen,character,skin) ||
       chart_pack.blob_bytes!=blob_bytes)return model_fail(5);
    gouraud_bytes=(chart_pack.count+(screen?FZX_MACHINE_MODELS_MAX_REFLECTION_TRIANGLES:0u))*8u;
    chart_pixels=(uintptr_t)reserve(&vram,chart_pack.blob_bytes);
    gouraud_tables=(uintptr_t)reserve(&vram,gouraud_bytes);
    effect_pixels=screen?(uintptr_t)reserve(&vram,FZX_MACHINE_MODELS_EFFECT_SELECTED_VRAM_BYTES):0;
    if(!chart_pixels || !gouraud_tables || (screen&&!effect_pixels))return model_fail(6);
    if(!copy_pixels((void *)chart_pixels,chart_pack.blob,chart_pack.blob_bytes))return model_fail(7);
    for(uint32_t i=0;i<gouraud_bytes/4u;++i)((volatile uint32_t *)gouraud_tables)[i]=0;
    if(screen) {
        const uint8_t *palette,*shadow;
        if(!fzx_texture_cd_load("MEFFECT.PAK",FZX_MACHINE_MODELS_EFFECT_BYTES,FZX_MACHINE_MODELS_EFFECT_HASH,
                                effect_data,cd,cd_bytes) ||
           !effect_open(effect_data,character,&palette,&shadow,&reflection_pixels) ||
           !copy_pixels((void *)effect_pixels,palette,32) ||
           !copy_pixels((void *)(effect_pixels+32),shadow,1024) ||
           !copy_pixels((void *)(effect_pixels+1056),reflection_pixels,512))return model_fail(8);
    }
    memset(model_work,0,sizeof(*model_work));
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<18;++j)source_cameras[i][j]=fzx_machine_menu_cameras[i][j];
    loaded_screen=screen;loaded_character=character;loaded_skin=skin;
    fzx_machine_menu_model_report[2]=FZX_MACHINE_MODELS_HASH;fzx_machine_menu_model_report[3]=chart_hash;
    fzx_machine_menu_model_report[4]=screen?FZX_MACHINE_MODELS_EFFECT_HASH:0;
    fzx_machine_menu_model_report[5]=screen;fzx_machine_menu_model_report[6]=character;fzx_machine_menu_model_report[7]=skin;
    fzx_machine_menu_model_report[12]=model_guard_count;
    for(unsigned i=0;i<model_guard_count;++i)fzx_machine_menu_model_report[13+i]=(uint32_t)(uintptr_t)model_guards[i];
    fzx_machine_menu_model_report[29]=(uint32_t)chart_pixels;fzx_machine_menu_model_report[30]=(uint32_t)gouraud_tables;
    fzx_machine_menu_model_report[31]=(uint32_t)effect_pixels;
    model_ready=true;
    if(!fzx_machine_menu_model_guards()){model_ready=false;return model_fail(9);}
    fzx_machine_menu_model_report[1]=1;return true;
}

static uint16_t lit_color(uint16_t color,const unsigned shade[3]) {
    uint16_t result=0x8000u;
    for(unsigned c=0;c<3;++c)result|=(uint16_t)((((color>>(c*5))&31u)*shade[c]/255u)<<(c*5));
    return result;
}
static void mean_shade(const fzx_mm_quad_t *q,unsigned mean[3]) {
    for(unsigned c=0;c<3;++c)mean[c]=((unsigned)q->shade[0][c]+q->shade[1][c]+q->shade[2][c])/3u;
}
static uint16_t residual(const uint8_t shade[3],const unsigned mean[3],const unsigned base[3]) {
    uint16_t out=0;
    for(unsigned c=0;c<3;++c) {
        int value=16+(int)(base[c]*shade[c]/255u)-(int)(base[c]*mean[c]/255u);
        if(value<0)value=0;
        if(value>31)value=31;
        out|=(uint16_t)((unsigned)value<<(c*5));
    }
    return out;
}

static bool light_chart(const fzx_mm_quad_t *q,uint32_t pixel_offset,uint32_t palette_offset,unsigned mode) {
    unsigned mean[3],representative[3]={0},samples=0;mean_shade(q,mean);
    const unsigned count=(unsigned)chart_pack.width*chart_pack.height;
    const unsigned bytes=count*(mode==4?1u:4u)/2u;
    if((mode!=4 && mode!=16) || pixel_offset>chart_pack.blob_bytes || bytes>chart_pack.blob_bytes-pixel_offset ||
       (mode==4 && (palette_offset>chart_pack.blob_bytes || 32u>chart_pack.blob_bytes-palette_offset)))return false;
    if(mode==4) {
        const uint8_t *palette=chart_pack.blob+palette_offset,*pixels=chart_pack.blob+pixel_offset;
        volatile uint16_t *out=(volatile uint16_t *)(chart_pixels+palette_offset);
        for(unsigned p=0;p<16;++p){const uint16_t v=be16(palette+p*2u);out[p]=v?lit_color(v,mean):0;}
        for(unsigned p=0;p<count;++p) {
            const unsigned index=(pixels[p/2]>>(p&1?0:4))&15u;const uint16_t v=be16(palette+index*2u);
            if(!v)continue;
            for(unsigned c=0;c<3;++c)representative[c]+=(v>>(c*5))&31u;
            ++samples;
        }
    } else {
        const uint8_t *pixels=chart_pack.blob+pixel_offset;
        volatile uint16_t *out=(volatile uint16_t *)(chart_pixels+pixel_offset);
        /* Offline RGB charts are unique per triangle: mutable illumination
         * must never alias another triangle submitted earlier in this frame. */
        for(unsigned p=0;p<count;++p) {
            const uint16_t v=be16(pixels+p*2u);out[p]=v?lit_color(v,mean):0;if(!v)continue;
            for(unsigned c=0;c<3;++c)representative[c]+=(v>>(c*5))&31u;
            ++samples;
        }
    }
    for(unsigned c=0;c<3;++c)representative[c]=samples?representative[c]/samples:0;
    const uintptr_t offset=(uintptr_t)q->triangle*8u;
    if(offset+8u>gouraud_bytes)return false;
    volatile uint16_t *g=(volatile uint16_t *)(gouraud_tables+offset);
    for(unsigned i=0;i<4;++i)g[i]=residual(q->shade[i],mean,representative);
    return true;
}

typedef struct { vdp1_cmdt_t *commands;unsigned next,capacity; } output_t;
static bool emit_model(void *context,const fzx_mm_quad_t *q) {
    output_t *o=context;if(!q || o->next>=o->capacity || q->kind>FZX_MM_REFLECTION || q->character>=30)return false;
    vdp1_cmdt_t *c=o->commands+o->next;memset(c,0,sizeof(*c));
    if(q->kind==FZX_MM_BASE) {
        if(q->triangle>=chart_pack.count)return false;
        const uint8_t *d=chart_pack.data+64+q->triangle*16u;
        const uint32_t pixel_offset=be32(d),palette_offset=be32(d+4),format=be32(d+8);
        if(!light_chart(q,pixel_offset,palette_offset,format))return false;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,
            .color_mode=format==4?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_GOURAUD};
        vdp1_cmdt_distorted_sprite_set(c);vdp1_cmdt_draw_mode_set(c,mode);
        vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(chart_pixels+pixel_offset));
        vdp1_cmdt_char_size_set(c,chart_pack.width,chart_pack.height);
        if(format==4)c->cmd_colr=(uint16_t)(((chart_pixels+palette_offset)&0x7ffffu)>>3);
        c->cmd_grda=(uint16_t)(((gouraud_tables+q->triangle*8u)&0x7ffffu)>>3);
    } else if(q->kind==FZX_MM_SHADOW) {
        if(loaded_screen!=FZX_MACHINE_SCREEN_SETTINGS || q->character!=loaded_character || !effect_pixels)return false;
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.color_mode=VDP1_CMDT_CM_CLUT_16,
            .cc_mode=VDP1_CMDT_CC_HALF_TRANSPARENT};
        vdp1_cmdt_distorted_sprite_set(c);vdp1_cmdt_draw_mode_set(c,mode);
        vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(effect_pixels+32));vdp1_cmdt_char_size_set(c,32,64);
        c->cmd_colr=(uint16_t)((effect_pixels&0x7ffffu)>>3);
    } else if(q->kind==FZX_MM_OUTLINE) {
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,
            .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
        const rgb1555_t black={.raw=0x8000u};
        vdp1_cmdt_polygon_set(c);vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_color_set(c,black);
    } else {
        if(loaded_screen!=FZX_MACHINE_SCREEN_SETTINGS || q->triangle>=FZX_MACHINE_MODELS_MAX_REFLECTION_TRIANGLES)return false;
        /* Original reflection RGB sampled by the source normal lookup. Mesh
         * plus half transparency approximates source primitive alpha60. */
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.mesh_enable=1,
            .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_GOURAUD|VDP1_CMDT_CC_HALF_TRANSPARENT};
        unsigned mean[3],base[3]={31,31,31};mean_shade(q,mean);
        const rgb1555_t color={.raw=lit_color(0xffffu,mean)};
        const uintptr_t offset=(chart_pack.count+q->triangle)*8u;
        if(offset+8u>gouraud_bytes)return false;
        volatile uint16_t *g=(volatile uint16_t *)(gouraud_tables+offset);
        for(unsigned i=0;i<4;++i)g[i]=residual(q->shade[i],mean,base);
        vdp1_cmdt_polygon_set(c);vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_color_set(c,color);
        c->cmd_grda=(uint16_t)(((gouraud_tables+offset)&0x7ffffu)>>3);
    }
    c->cmd_xa=q->x[0];c->cmd_ya=q->y[0];c->cmd_xb=q->x[1];c->cmd_yb=q->y[1];
    c->cmd_xc=q->x[2];c->cmd_yc=q->y[2];c->cmd_xd=q->x[3];c->cmd_yd=q->y[3];
    ++o->next;++fzx_machine_menu_model_report[8+q->kind];return true;
}

int fzx_machine_menu_model_draw(vdp1_cmdt_t *commands,unsigned first,unsigned capacity,
                                const fzx_machine_menu_snapshot_t *frame) {
    for(unsigned i=8;i<12;++i)fzx_machine_menu_model_report[i]=0;
    if(!commands || !frame || first>capacity || !fzx_machine_menu_model_guards() || frame->screen!=loaded_screen ||
       (loaded_screen && (frame->character!=loaded_character || frame->skin!=loaded_skin))) {
        model_fail(11);return -1;
    }
    output_t output={commands,first,capacity};unsigned count=0;
    if(!fzx_mm_render(&model_pack,&chart_pack,source_cameras,fzx_machine_models_slot_to_character,
        reflection_pixels,frame,model_work,emit_model,&output,&count) || count!=output.next-first) {
        model_fail(12);return -1;
    }
    if(!fzx_machine_menu_model_guards()){model_fail(9);return -1;}
    fzx_machine_menu_model_report[1]=1;return (int)output.next;
}
