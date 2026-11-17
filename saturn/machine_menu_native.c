#include "machine_menu_native.h"
#include "texture_cd.h"
#include "machine_menu_identity.h"
#include <string.h>

volatile uint32_t fzx_machine_menu_status,fzx_machine_menu_report[24];
volatile uint32_t fzx_machine_menu_debug_buttons,fzx_machine_menu_debug_limit;
volatile uint32_t fzx_machine_menu_debug_reset,fzx_machine_menu_debug_unlock;
fzx_machine_menu_selection_t fzx_machine_menu_selection;
/* Frontend scratch is borrowed again by course selection. Keep only the
 * player's choices here so backing out does not discard them. */
static struct {
    bool valid;
    uint8_t character,skin;
    int8_t changed;
    float last_engine[30];
} menu_history;
#define REQUEST(name) (*(volatile uint32_t *)((uintptr_t)&(name)|CPU_CACHE_THROUGH))
enum { GUARD=0x637b2e91u, GUARD_BYTES=32u, PALETTE_BYTES=FZX_MACHINE_MENU_DRAW_MAX*32u };

typedef struct {
    uintptr_t pixels,character_pixels,palettes,models;
    uint32_t model_bytes;
    volatile uint32_t *guards[4];
    uint8_t bank,screen,character,skin;
} resident_t;

static uint16_t read_buttons(uint8_t *connected) {
    smpc_peripheral_digital_t pad={0};
    smpc_peripheral_process();smpc_peripheral_digital_port(1,&pad);
    const uint32_t debug=REQUEST(fzx_machine_menu_debug_buttons);
    if(debug&0x10000u){*connected=1;return (uint16_t)debug;}
    *connected=pad.connected?1:0;if(!*connected)return 0;
    uint16_t buttons=0;
    if(pad.pressed.raw&(PERIPHERAL_DIGITAL_A|PERIPHERAL_DIGITAL_C))buttons|=0x8000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_B)buttons|=0x4000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_START)buttons|=0x1000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_UP)buttons|=0x0800u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_DOWN)buttons|=0x0400u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_LEFT)buttons|=0x0200u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_RIGHT)buttons|=0x0100u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_L)buttons|=0x2000u;
    if(pad.pressed.raw&PERIPHERAL_DIGITAL_R)buttons|=0x0010u;
    return buttons;
}

static bool guarded(const volatile uint32_t *guard) {
    for(unsigned i=0;i<GUARD_BYTES/4u;++i)if(guard[i]!=GUARD)return false;
    return true;
}

static bool layout(resident_t *r,uint8_t screen) {
    vdp1_vram_partitions_t partition;vdp1_vram_partitions_get(&partition);
    const uint32_t bank_bytes=screen==FZX_MACHINE_SCREEN_SETTINGS?FZX_MACHINE_MENU_MAX_BANK_BYTES:0;
    const uint32_t used=FZX_MACHINE_MENU_BYTES+bank_bytes+PALETTE_BYTES+4u*GUARD_BYTES;
    if(used>=partition.texture_size){fzx_machine_menu_status=12;return false;}
    const uintptr_t base=(uintptr_t)partition.texture_base;
    r->pixels=base+GUARD_BYTES;
    r->character_pixels=r->pixels+FZX_MACHINE_MENU_BYTES+GUARD_BYTES;
    r->palettes=r->character_pixels+bank_bytes+GUARD_BYTES;
    r->models=r->palettes+PALETTE_BYTES+GUARD_BYTES;r->model_bytes=partition.texture_size-used;
    r->guards[0]=(volatile uint32_t *)base;
    r->guards[1]=(volatile uint32_t *)(r->pixels+FZX_MACHINE_MENU_BYTES);
    r->guards[2]=(volatile uint32_t *)(r->character_pixels+bank_bytes);
    r->guards[3]=(volatile uint32_t *)(r->palettes+PALETTE_BYTES);
    for(unsigned g=0;g<4;++g)for(unsigned i=0;i<GUARD_BYTES/4u;++i)r->guards[g][i]=GUARD;
    r->screen=screen;r->bank=0;r->character=255;r->skin=255;
    return true;
}

static uint16_t solid_color(const fzx_machine_menu_draw_t *q) {
    return (uint16_t)(0x8000u|((q->r*31u+127u)/255u)|
        (((q->g*31u+127u)/255u)<<5)|(((q->b*31u+127u)/255u)<<10));
}

static void polygon(vdp1_cmdt_t *c,int16_t x,int16_t y,int16_t x2,int16_t y2,uint16_t value) {
    memset(c,0,sizeof(*c));
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,
        .color_mode=VDP1_CMDT_CM_RGB_32768,.cc_mode=VDP1_CMDT_CC_REPLACE};
    const rgb1555_t color={.raw=value};
    vdp1_cmdt_polygon_set(c);vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_color_set(c,color);
    c->cmd_xa=x;c->cmd_ya=y;c->cmd_xb=x2;c->cmd_yb=y;
    c->cmd_xc=x2;c->cmd_yc=y2;c->cmd_xd=x;c->cmd_yd=y2;
}

static bool draw(vdp1_cmdt_t *commands,unsigned capacity,const resident_t *r,
                 const fzx_machine_menu_snapshot_t *frame,vdp1_cmdt_list_t *list,bool blank) {
    if(capacity<4 || (!blank && (!frame || frame->count>FZX_MACHINE_MENU_DRAW_MAX)))return false;
    memset(commands,0,2*sizeof(*commands));
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223),origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(commands);vdp1_cmdt_vtx_system_clip_coord_set(commands,clip);
    vdp1_cmdt_local_coord_set(commands+1);vdp1_cmdt_vtx_local_coord_set(commands+1,origin);
    polygon(commands+2,0,0,319,223,0);
    unsigned count=3;
    if(!blank)for(unsigned i=0;i<frame->count;++i) {
        const fzx_machine_menu_draw_t *q=&frame->draws[i];
        if(q->kind==FZX_MACHINE_DRAW_MODELS) {
            const int next=fzx_machine_menu_model_draw(commands,count,capacity-1,frame);
            if(next<(int)count || (unsigned)next>=capacity)return false;
            count=(unsigned)next;continue;
        }
        if(q->kind==FZX_MACHINE_DRAW_RECT) {
            if(q->x2<q->x || q->y2<q->y || q->a!=255)return false;
            int16_t bottom=q->y2;
            /* Exact run coalescing of adjacent equal original gradient rows. */
            while(i+1<frame->count) {
                const fzx_machine_menu_draw_t *n=&frame->draws[i+1];
                if(n->kind!=FZX_MACHINE_DRAW_RECT || n->x!=q->x || n->x2!=q->x2 ||
                   n->y!=(int)bottom+1 || n->y2<n->y || n->r!=q->r || n->g!=q->g ||
                   n->b!=q->b || n->a!=q->a)break;
                bottom=n->y2;++i;
            }
            if(count+1>=capacity)return false;
            polygon(commands+count++,q->x,(int16_t)(q->y-8),q->x2,(int16_t)(bottom-8),solid_color(q));
            continue;
        }
        unsigned image=q->image;
        if(q->kind==FZX_MACHINE_DRAW_GLYPH) {
            if(q->font==1 && image<sizeof(fzx_machine_font1)/sizeof(fzx_machine_font1[0]))image=fzx_machine_font1[image][0];
            else if(q->font==2 && image<sizeof(fzx_machine_font2)/sizeof(fzx_machine_font2[0]))image=fzx_machine_font2[image][0];
            else return false;
        } else if(q->kind!=FZX_MACHINE_DRAW_IMAGE)return false;
        if(image>=FZX_MACHINE_MENU_ATLAS_IMAGES || q->scale_x_q8>1024 || q->scale_y_q8>1024)return false;
        if(!q->a || !q->scale_x_q8 || !q->scale_y_q8)continue;
        const unsigned bank=fzx_machine_menu_images[image].bank;
        if(bank && bank!=r->bank)return false;
        const uintptr_t pixels=bank?r->character_pixels:r->pixels;
        const unsigned offset=fzx_machine_menu_images[image].offset,bytes=fzx_machine_menu_images[image].bytes;
        if(bank>=FZX_MACHINE_MENU_BANK_COUNT || offset>fzx_machine_menu_banks[bank].bytes ||
           bytes>fzx_machine_menu_banks[bank].bytes-offset)return false;
        const bool clut=fzx_machine_menu_images[image].clut!=0;
        if(!clut && (q->r!=255 || q->g!=255 || q->b!=255))return false;
        if(count+1>=capacity)return false;
        vdp1_cmdt_t *c=commands+count++;memset(c,0,sizeof(*c));
        const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,
            .color_mode=clut?VDP1_CMDT_CM_CLUT_16:VDP1_CMDT_CM_RGB_32768,
            /* Saturn adaptation: source cursor alpha becomes transparent,
             * half-transparent or opaque; source pulse cadence is unchanged. */
            .cc_mode=q->a<192?VDP1_CMDT_CC_HALF_TRANSPARENT:VDP1_CMDT_CC_REPLACE};
        const unsigned width=fzx_machine_menu_images[image].width,height=fzx_machine_menu_images[image].height;
        if(q->scale_x_q8==256 && q->scale_y_q8==256)vdp1_cmdt_normal_sprite_set(c);
        else {
            const unsigned sw=(width*q->scale_x_q8+128u)/256u,sh=(height*q->scale_y_q8+128u)/256u;
            if(!sw || !sh){--count;continue;}
            vdp1_cmdt_distorted_sprite_set(c);
            c->cmd_xb=(int16_t)(q->x+(int)sw-1);c->cmd_yb=(int16_t)(q->y-8);
            c->cmd_xc=c->cmd_xb;c->cmd_yc=(int16_t)(q->y-8+(int)sh-1);
            c->cmd_xd=q->x;c->cmd_yd=c->cmd_yc;
        }
        if(q->flip_x)c->cmd_ctrl|=0x0010u;
        vdp1_cmdt_draw_mode_set(c,mode);vdp1_cmdt_char_base_set(c,(vdp1_vram_t)(pixels+offset));
        vdp1_cmdt_char_size_set(c,width,height);c->cmd_xa=q->x;c->cmd_ya=(int16_t)(q->y-8);
        if(clut) {
            volatile uint16_t *table=(volatile uint16_t *)(r->palettes+i*32u);
            for(unsigned p=0;p<16;++p) {
                const uint16_t v=fzx_machine_menu_images[image].palette[p];
                table[p]=v?(uint16_t)(0x8000u|((v&31u)*q->r/255u)|
                    ((((v>>5)&31u)*q->g/255u)<<5)|((((v>>10)&31u)*q->b/255u)<<10)):0;
            }
            c->cmd_colr=(uint16_t)(((r->palettes+i*32u)&0x7ffffu)>>3);
        }
    }
    memset(commands+count,0,sizeof(*commands));vdp1_cmdt_end_set(commands+count);list->count=(uint16_t)(count+1);
    fzx_machine_menu_report[10]=count+1;
    vdp1_sync_cmdt_list_put(list,0);vdp1_sync_render();vdp1_sync();vdp2_sync();
    return true;
}

static bool blank_pages(vdp1_cmdt_t *commands,unsigned capacity,const resident_t *r,vdp1_cmdt_list_t *list) {
    vdp1_sync_wait();vdp2_sync_wait();
    for(unsigned i=0;i<2;++i) {
        if(!draw(commands,capacity,r,NULL,list,true)){fzx_machine_menu_status=7;return false;}
        vdp1_sync_wait();vdp2_sync_wait();
    }
    return true;
}

static void release_buttons(void) {
    for(;;) {
        uint8_t connected;if(!read_buttons(&connected))return;
        vdp2_sync();vdp2_sync_wait();
    }
}

static bool load_models(resident_t *r,const fzx_machine_menu_state_t *s,void *scratch,uint32_t bytes) {
    if(!fzx_machine_menu_model_load(scratch,bytes,(void *)r->models,r->model_bytes,s->screen,
                                    (uint8_t)s->racers[0].character,(uint8_t)s->racers[0].machineSkinIndex)) {
        fzx_machine_menu_status=8;return false;
    }
    r->character=(uint8_t)s->racers[0].character;r->skin=(uint8_t)s->racers[0].machineSkinIndex;
    return true;
}

static bool load_screen(resident_t *r,const fzx_machine_menu_state_t *s,void *scratch,uint32_t bytes) {
    fzx_machine_menu_status=10;
    if(!layout(r,s->screen))return false;
    if(s->screen==FZX_MACHINE_SCREEN_SETTINGS) {
        const unsigned character=(unsigned)s->racers[0].character,bank=character+1u;
        if(character>=30 || bank>=FZX_MACHINE_MENU_BANK_COUNT ||
           fzx_machine_menu_banks[bank].bytes>FZX_MACHINE_MENU_MAX_BANK_BYTES){fzx_machine_menu_status=4;return false;}
        char name[]="MCH00.PAK";name[3]=(char)('0'+character/10u);name[4]=(char)('0'+character%10u);
        if(!fzx_texture_cd_load(name,fzx_machine_menu_banks[bank].bytes,fzx_machine_menu_banks[bank].hash,
                                (void *)r->character_pixels,scratch,bytes)){fzx_machine_menu_status=4;return false;}
        r->bank=(uint8_t)bank;
    }
    return load_models(r,s,scratch,bytes);
}

static bool reset_source(fzx_machine_menu_state_t *s,fzx_machine_menu_config_t *config,uint8_t screen) {
    const int8_t changed=s->changed[0];
    config->frame=s->frame;config->character=(uint8_t)s->racers[0].character;
    config->skin=(uint8_t)s->racers[0].machineSkinIndex;
    memcpy(config->last_engine,s->last_engine,sizeof(config->last_engine));
    config->everything_unlocked=REQUEST(fzx_machine_menu_debug_unlock)?1:0;
    if(!fzx_machine_menu_reset(s,config,screen)){fzx_machine_menu_status=6;return false;}
    /* The original screen initializers do not reset D_800CD3AC. Keeping it
     * avoids selecting the default skin again when the same machine is confirmed. */
    s->changed[0]=changed;
    ++fzx_machine_menu_report[23];return true;
}

static void report(const fzx_machine_menu_state_t *s,const resident_t *r) {
    fzx_machine_menu_report[0]=0x465a584du;fzx_machine_menu_report[1]=s->ticks;
    fzx_machine_menu_report[2]=s->screen;fzx_machine_menu_report[3]=s->snapshot.phase;
    fzx_machine_menu_report[4]=(uint32_t)s->racers[0].character;fzx_machine_menu_report[5]=s->request;
    fzx_machine_menu_report[6]=s->config.course;fzx_machine_menu_report[7]=FZX_MACHINE_MENU_HASH;
    fzx_machine_menu_report[8]=(uint32_t)r->pixels;fzx_machine_menu_report[9]=(uint32_t)r->palettes;
    fzx_machine_menu_report[11]=s->connected;fzx_machine_menu_report[12]=(uint32_t)s->racers[0].machineSkinIndex;
    for(unsigned g=0;g<4;++g)fzx_machine_menu_report[13+g]=(uint32_t)(uintptr_t)r->guards[g];
    fzx_machine_menu_report[17]=(uint32_t)r->models;fzx_machine_menu_report[18]=r->model_bytes;
    fzx_machine_menu_report[19]=(uint32_t)(uintptr_t)s;fzx_machine_menu_report[20]=(uint32_t)(uintptr_t)&s->snapshot;
    fzx_machine_menu_report[21]=r->bank;fzx_machine_menu_report[22]=r->skin;
}

bool fzx_machine_menu_run(volatile const uint32_t *fields,vdp1_cmdt_t *commands,unsigned capacity,
                          void *scratch,uint32_t scratch_bytes,uint8_t course,bool *race_ready) {
    fzx_machine_menu_status=10;memset((void *)fzx_machine_menu_report,0,sizeof(fzx_machine_menu_report));
    memset(&fzx_machine_menu_selection,0,sizeof(fzx_machine_menu_selection));
    if(race_ready)*race_ready=false;
    if(!fields || !commands || capacity<FZX_MACHINE_MENU_DRAW_MAX+4 || !scratch || !race_ready ||
       ((uintptr_t)scratch&31u) || course>=24){fzx_machine_menu_status=11;return false;}
    const uint32_t state_bytes=(sizeof(fzx_machine_menu_state_t)+31u)&~31u;
    const uint32_t reserved=state_bytes+2u*GUARD_BYTES;
    if(scratch_bytes<reserved || scratch_bytes-reserved<fzx_texture_cd_scratch_bytes()) {
        fzx_machine_menu_status=11;return false;
    }
    volatile uint32_t *state_before=scratch,*state_after=(volatile uint32_t *)((uintptr_t)scratch+GUARD_BYTES+state_bytes);
    for(unsigned i=0;i<GUARD_BYTES/4u;++i)state_before[i]=state_after[i]=GUARD;
    fzx_machine_menu_state_t *s=(void *)((uintptr_t)scratch+GUARD_BYTES);
    void *work=(void *)((uintptr_t)scratch+reserved);const uint32_t work_bytes=scratch_bytes-reserved;
    resident_t resident={0};vdp1_cmdt_list_t list={.cmdts=commands};
    if(!blank_pages(commands,capacity,&resident,&list) || !layout(&resident,FZX_MACHINE_SCREEN_SELECT))return false;
    if(!fzx_texture_cd_load("MACHUI.PAK",FZX_MACHINE_MENU_BYTES,FZX_MACHINE_MENU_HASH,
                            (void *)resident.pixels,work,work_bytes)){fzx_machine_menu_status=4;return false;}
    fzx_machine_menu_config_t config={0};config.mode=1;config.course=course;
    config.everything_unlocked=REQUEST(fzx_machine_menu_debug_unlock)?1:0;
    if(menu_history.valid) {
        config.character=menu_history.character;config.skin=menu_history.skin;
        memcpy(config.last_engine,menu_history.last_engine,sizeof(config.last_engine));
    } else {
        for(unsigned i=0;i<30;++i)config.last_engine[i]=0.5f;
    }
    if(!fzx_machine_menu_reset(s,&config,FZX_MACHINE_SCREEN_SELECT)){fzx_machine_menu_status=6;return false;}
    if(menu_history.valid)s->changed[0]=menu_history.changed;
    if(!load_models(&resident,s,work,work_bytes))return false;
    release_buttons();
    const uint32_t hz=vdp2_tvmd_tv_standard_get()==VDP2_TVMD_TV_STANDARD_PAL?50u:60u;
    uint32_t previous=*fields,remainder=0;uint16_t last_buttons=0;
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,0,0,0));
    for(;;) {
        vdp1_sync_wait();vdp2_sync_wait();
        if(!guarded(state_before) || !guarded(state_after)){fzx_machine_menu_status=5;return false;}
        for(unsigned g=0;g<4;++g)if(!guarded(resident.guards[g])){fzx_machine_menu_status=5;return false;}
        if(!fzx_machine_menu_model_guards()){fzx_machine_menu_status=9;return false;}
        uint8_t connected;const uint16_t buttons=read_buttons(&connected);
        if(REQUEST(fzx_machine_menu_debug_reset) || (s->request==FZX_MACHINE_REQUEST_RACE &&
            (course!=0 || s->racers[0].character!=0) && (buttons&~last_buttons&0x4000u))) {
            release_buttons();
            if(!reset_source(s,&config,s->screen))return false;
            REQUEST(fzx_machine_menu_debug_reset)=0;last_buttons=0;remainder=0;previous=*fields;
            /* Same UI bank and model identity remain valid; source object rotation restarts. */
        } else {
            const uint32_t now=*fields;uint32_t elapsed=now-previous;previous=now;
            if(elapsed>5)elapsed=5;
            remainder+=elapsed*60u;
            while(remainder>=hz) {
                const uint32_t limit=REQUEST(fzx_machine_menu_debug_limit);
                if(!s->request && (!limit || s->ticks<limit) &&
                   !fzx_machine_menu_step(s,buttons,0,connected,false)){fzx_machine_menu_status=6;return false;}
                remainder-=hz;
            }
            last_buttons=buttons;
        }
        if(s->screen==FZX_MACHINE_SCREEN_SETTINGS &&
           (resident.character!=(uint8_t)s->racers[0].character || resident.skin!=(uint8_t)s->racers[0].machineSkinIndex)) {
            if(!blank_pages(commands,capacity,&resident,&list))return false;
            fzx_machine_menu_status=10;
            if(resident.character!=(uint8_t)s->racers[0].character) {
                if(!load_screen(&resident,s,work,work_bytes))return false;
            } else if(!load_models(&resident,s,work,work_bytes))return false;
            previous=*fields;remainder=0;
        }
        report(s,&resident);
        fzx_machine_menu_status=s->request?2:1;
        if(s->request==FZX_MACHINE_REQUEST_RACE && (course!=0 || s->racers[0].character!=0))fzx_machine_menu_status=3;
        if(!draw(commands,capacity,&resident,&s->snapshot,&list,false)){fzx_machine_menu_status=7;return false;}
        if(s->request==FZX_MACHINE_REQUEST_COURSE)break;
        if(s->request==FZX_MACHINE_REQUEST_RACE && course==0 && s->racers[0].character==0) {
            fzx_machine_menu_selection=(fzx_machine_menu_selection_t){1,course,(uint8_t)s->racers[0].character,
                (uint8_t)s->racers[0].machineSkinIndex,s->engine[0]};
            *race_ready=true;break;
        }
        if(s->request==FZX_MACHINE_REQUEST_SETTINGS || s->request==FZX_MACHINE_REQUEST_SELECT) {
            const uint8_t screen=s->request==FZX_MACHINE_REQUEST_SETTINGS?FZX_MACHINE_SCREEN_SETTINGS:FZX_MACHINE_SCREEN_SELECT;
            release_buttons();
            if(!blank_pages(commands,capacity,&resident,&list) || !reset_source(s,&config,screen) ||
               !load_screen(&resident,s,work,work_bytes))return false;
            previous=*fields;remainder=0;last_buttons=0;
        }
    }
    release_buttons();
    if(!blank_pages(commands,capacity,&resident,&list))return false;
    if(!guarded(state_before) || !guarded(state_after)){fzx_machine_menu_status=5;return false;}
    for(unsigned g=0;g<4;++g)if(!guarded(resident.guards[g])){fzx_machine_menu_status=5;return false;}
    if(!fzx_machine_menu_model_guards()){fzx_machine_menu_status=9;return false;}
    menu_history.character=(uint8_t)s->racers[0].character;
    menu_history.skin=(uint8_t)s->racers[0].machineSkinIndex;
    menu_history.changed=s->changed[0];
    memcpy(menu_history.last_engine,s->last_engine,sizeof(menu_history.last_engine));
    menu_history.valid=true;
    fzx_machine_menu_status=0;return true;
}
