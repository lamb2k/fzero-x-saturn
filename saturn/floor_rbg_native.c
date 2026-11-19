#include "frame_profile.h"
#include "floor_rbg_native.h"
#include "floor_rbg_identity.h"
#include "texture_cd.h"
#include <stddef.h>
#include <string.h>
#if FZX_SOURCE_FINISH_MENU
#include "records_scene.h"
#endif
#if FZX_CLOUDS_VDP2
#include "clouds_vdp2_native.h"
#endif
_Static_assert(!FZX_FLOOR_FIXTURE,"original floor required");
_Static_assert(sizeof(fzx_floor_rp_t)==sizeof(vdp2_scrn_rp_table_t),"rotation ABI size");
_Static_assert(offsetof(fzx_floor_rp_t,coefficient_start)==offsetof(vdp2_scrn_rp_table_t,kast),"rotation ABI coefficient");
#define FLOOR_CHARS VDP2_VRAM_ADDR(0,0)
#define FLOOR_RP VDP2_VRAM_ADDR(1,0)
#define FLOOR_MAP VDP2_VRAM_ADDR(1,0x4000)
#define FLOOR_COEFF VDP2_VRAM_ADDR(2,0)
#define FLOOR_WINDOW VDP2_VRAM_ADDR(3,0)
extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_input_count;
fzx_floor_rbg_t fzx_floor_rbg;
volatile uint32_t fzx_floor_status,fzx_floor_busy,fzx_floor_ticks;
volatile uint32_t fzx_floor_pending,fzx_floor_presented,fzx_floor_commits;
volatile uint32_t fzx_floor_upload_ticks,fzx_floor_upload_blanking;
#if FZX_VDP1_DBANK
/* FZX_VDP1_DBANK: frame N+1 is prepared while N still waits for its upload at
 * the end of its draw, so the tables have two banks, both in VDP1 VRAM (set by
 * main). Prepared frames queue in build order; each present uploads the oldest,
 * which is the frame whose draw has just completed, and copies it into
 * fzx_floor_rbg, which therefore always holds the presented tables. */
#include "sh2zam/vdp1_banks_experimental.h"
static fzx_floor_rbg_t *floor_stage[2];
static unsigned floor_bank;
static sh2z_vdp1_queue_t floor_queue;
void fzx_floor_dbank(void *bank0,void *bank1){floor_stage[0]=bank0;floor_stage[1]=bank1;sh2z_vdp1_queue_init(&floor_queue);}
void fzx_floor_dbank_select(unsigned bank){floor_bank=bank&1u;}
#define FLOOR_STAGE (*floor_stage[floor_bank])
#else
#define FLOOR_STAGE fzx_floor_rbg
#endif
static bool load(const char *name,unsigned size,uint32_t hash,uintptr_t destination) {
    const uintptr_t scratch=((uintptr_t)sat_stub_data_end+31u)&~(uintptr_t)31u;
    return scratch>=0x00200000u && scratch<0x00300000u &&
        fzx_texture_cd_load(name,size,hash,(void *)destination,(void *)scratch,0x00300000u-scratch);
}
bool fzx_floor_load(void) {
    if(!load("FLOORCHR.BIN",FZX_FLOORCHR_BYTES,FZX_FLOORCHR_HASH,FLOOR_CHARS) ||
       !load("FLOORMAP.BIN",FZX_FLOORMAP_BYTES,FZX_FLOORMAP_HASH,FLOOR_MAP) ||
       !load("FLOORKOF.BIN",FZX_FLOORKOF_BYTES,FZX_FLOORKOF_HASH,FLOOR_COEFF)) {
        fzx_floor_status=2;return false;
    }
    memset((void *)FLOOR_RP,0,96);
    for(unsigned y=0;y<224;++y){((volatile uint16_t *)FLOOR_WINDOW)[y*2]=640;((volatile uint16_t *)FLOOR_WINDOW)[y*2+1]=0;}
    const vdp2_scrn_cell_format_t format={.scroll_screen=VDP2_SCRN_RBG0_PA,
        .ccc=VDP2_SCRN_CCC_RGB_32768,.char_size=VDP2_SCRN_CHAR_SIZE_1X1,.pnd_size=2,
        .aux_mode=VDP2_SCRN_AUX_MODE_0,.plane_size=VDP2_SCRN_PLANE_SIZE_1X1,.cpd_base=FLOOR_CHARS};
    const vdp2_scrn_rotation_map_t map={.single=true,.plane=FLOOR_MAP};
    /* Yaul's helper derives KTAOF from a byte address. Hardware KAst in the
     * uploaded table is instead a Q16 entry coordinate; keep them separate. */
    const vdp2_scrn_rp_table_t descriptor={.kast=0x40000};
    const vdp2_scrn_rotation_params_t params={.rp_mode=VDP2_SCRN_RP_MODE_0,
        .rsop_type=VDP2_SCRN_RSOP_TYPE_REPEAT,.rp_table=&descriptor,.rp_table_base=FLOOR_RP,
        .coeff_params={.usage=VDP2_SCRN_COEFF_USAGE_KX_KY,.word_size=VDP2_SCRN_COEFF_WORD_SIZE_2,.enable=1}};
    const vdp2_vram_usage_t usage={.a0=VDP2_VRAM_USAGE_TYPE_CPD_BPD,.a1=VDP2_VRAM_USAGE_TYPE_PND,
        .b0=VDP2_VRAM_USAGE_TYPE_COEFF_TBL,.b1=VDP2_VRAM_USAGE_TYPE_NONE};
    vdp2_scrn_rotation_cell_format_set(&format,&map);
    vdp2_scrn_rotation_rp_mode_set(&params);vdp2_scrn_rotation_sop_set(&params);
    vdp2_scrn_rotation_rp_table_set(&params);vdp2_scrn_rotation_rp_read_set(&params);
    vdp2_scrn_rotation_coeff_params_set(&params);vdp2_scrn_rotation_coeff_table_set(&params);
    vdp2_vram_usage_set(&usage);vdp2_scrn_priority_set(VDP2_SCRN_RBG0,3);
    vdp2_ioregs_t *r=vdp2_regs_get();
    /* Preserve NBG3 and its B1 access slots. Window zero is used only by RBG0. */
    r->bgon|=VDP2_SCRN_DISP_RBG0;r->wctlc=(r->wctlc&0xff00u)|0x03u;
    r->wpsy0=0;r->wpey0=223;r->lwta0=0x80030000u;
    /* Mixed RGB/palette sprite type0, color calculation only at priority5.
     * Existing direct RGB content keeps selector0/priority6 and stays opaque. */
    r->spctl=0x1520u;r->ccctl|=0x0040u;
    vdp2_sprite_priority_set(1,5);
    r->craofb&=(uint16_t)~0x0070u;
    *(volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0,0,1)=FZX_FLOOR_FOG;
    uint16_t *ratios=&r->ccrsa;
    for(unsigned pair=0;pair<4;++pair) {
        unsigned word=0;
        for(unsigned j=0;j<2;++j){const unsigned band=pair*2+j;const unsigned alpha=(band*32+3)/7;const unsigned ratio=alpha>=32?0:31-alpha;word|=ratio<<(8*j);}
        ratios[pair]=(uint16_t)word;
    }
    vdp1_sync_render_set(fzx_floor_present,NULL);
    fzx_floor_status=1;return true;
}
typedef struct {vdp1_cmdt_t *commands;unsigned capacity,count;} submit_t;
static bool emit(void *context,const fzx_floor_polygon_t *q) {
    submit_t *s=context;if(s->count>=s->capacity)return false;
    vdp1_cmdt_t *c=&s->commands[s->count++];memset(c,0,sizeof(*c));vdp1_cmdt_polygon_set(c);
    const vdp1_cmdt_draw_mode_t mode={.end_code_disable=1,.trans_pixel_disable=1,.user_clipping_mode=2};
    vdp1_cmdt_draw_mode_set(c,mode);
    c->cmd_colr=q->band==0?0:q->band==7?FZX_FLOOR_FOG:(uint16_t)(0x4001u|(q->band<<11));
    c->cmd_xa=q->x[0];c->cmd_ya=q->y[0];c->cmd_xb=q->x[1];c->cmd_yb=q->y[1];
    c->cmd_xc=q->x[2];c->cmd_yc=q->y[2];c->cmd_xd=q->x[3];c->cmd_yd=q->y[3];return true;
}
bool fzx_floor_native(vdp1_cmdt_t *commands,unsigned capacity,const fzx_view_t *view,
                       const RefBackdrop *source,uint32_t *count) {
#if FZX_VDP1_DBANK
    if(!commands || capacity<2 || !view || !source || !count || !floor_stage[floor_bank] ||
       !sh2z_vdp1_queue_free(&floor_queue,floor_bank))return false;
#else
    if(!commands || capacity<2 || !view || !source || !count || fzx_floor_pending)return false;
#endif
    const uint16_t start=cpu_frt_count_get();fzx_floor_busy=1;
    memset(commands,0,sizeof(*commands));vdp1_cmdt_user_clip_coord_set(commands);
    commands->cmd_xa=view->clip[0];commands->cmd_ya=view->clip[1];commands->cmd_xc=view->clip[2]-1;commands->cmd_yc=view->clip[3]-1;
    submit_t submit={commands+1,capacity-1,0};
    if(!fzx_floor_rbg_prepare(view,source,&FLOOR_STAGE,emit,&submit)){fzx_floor_status=3;fzx_floor_busy=0;return false;}
    *count=submit.count+1;fzx_floor_ticks=(uint16_t)(cpu_frt_count_get()-start);
#if FZX_VDP1_DBANK
    (void)sh2z_vdp1_queue_push(&floor_queue,floor_bank,fzx_input_count?fzx_input_count:1u);
#endif
    fzx_floor_pending=fzx_input_count;fzx_floor_busy=0;return true;
}
void fzx_floor_present(void *unused) {
    (void)unused;
#if FZX_SOURCE_FINISH_MENU
    fzx_records_finish_menu_present();
#endif
#if FZX_CLOUDS_VDP2
    /* Upload timing covers the cloud writes and the floor upload together. */
    const uint16_t upload_start=cpu_frt_count_get();
    const uint32_t upload_blank=vdp2_tvmd_vblank_in()?1u:0u;
    const bool clouds=fzx_clouds_present();
#if FZX_VDP1_DBANK
    if(!floor_queue.count){if(clouds)fzx_clouds_upload_record(upload_start,upload_blank);return;}
#else
    if(!fzx_floor_pending){if(clouds)fzx_clouds_upload_record(upload_start,upload_blank);return;}
#endif
#else
#if FZX_VDP1_DBANK
    if(!floor_queue.count)return;
#else
    if(!fzx_floor_pending)return;
#endif
#endif
    const uint16_t start=cpu_frt_count_get();
    const uint32_t blank_start=vdp2_tvmd_vblank_in()?1u:0u;
    /* Variable interval -1 invokes this at VBlank when drawing has completed,
     * immediately before requesting the framebuffer change. No active draw or
     * scanout reads these tables while they are copied. */
#if FZX_VDP1_DBANK
    uint32_t bank=0,tick=0;(void)sh2z_vdp1_queue_pop(&floor_queue,&bank,&tick);
    const fzx_floor_rbg_t *stage=floor_stage[bank];
    memcpy((void *)FLOOR_RP,&stage->rp,sizeof(stage->rp));
    memcpy((void *)FLOOR_WINDOW,stage->window,sizeof(stage->window));
    fzx_floor_upload_blanking=blank_start|(vdp2_tvmd_vblank_in()?2u:0u);
    fzx_floor_upload_ticks=(uint16_t)(cpu_frt_count_get()-start);
    memcpy(&fzx_floor_rbg,stage,sizeof(fzx_floor_rbg));
    fzx_floor_presented=tick;
    fzx_floor_pending=sh2z_vdp1_queue_front(&floor_queue);++fzx_floor_commits;
#else
    memcpy((void *)FLOOR_RP,&fzx_floor_rbg.rp,sizeof(fzx_floor_rbg.rp));
    memcpy((void *)FLOOR_WINDOW,fzx_floor_rbg.window,sizeof(fzx_floor_rbg.window));
    fzx_floor_upload_blanking=blank_start|(vdp2_tvmd_vblank_in()?2u:0u);
    fzx_floor_upload_ticks=(uint16_t)(cpu_frt_count_get()-start);
    fzx_floor_presented=fzx_floor_pending;fzx_floor_pending=0;++fzx_floor_commits;
#endif
#if FZX_CLOUDS_VDP2
    fzx_clouds_upload_record(upload_start,upload_blank);
#endif
#if FZX_FRAME_PROFILE
    fzx_fp_present();
#endif
}
