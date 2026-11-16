/* Native VDP1 chart experiment: fixed source poses, no simulation. */
#include <yaul.h>
#include <string.h>
#include "charts.h"
_Static_assert(FZX_CHART_FIXTURE==FZX_EXPECT_CHART_FIXTURE,"Wrong chart provenance");
static vdp1_cmdt_t commands[512];
static uint16_t order[509],addresses[509];
static vdp1_cmdt_list_t list={.cmdts=commands,.count=3};
static vdp1_vram_partitions_t partitions;
volatile uint32_t video_fields;
volatile uint32_t fzx_chart_status,fzx_chart_case,fzx_chart_hold,fzx_chart_completed_case;
volatile uint32_t fzx_chart_sequence,fzx_chart_upload_ticks,fzx_chart_build_ticks;
volatile uint32_t fzx_chart_wait_fields,fzx_chart_hash,fzx_chart_bytes,fzx_chart_count;
static volatile uint32_t timer_high;
static void overflow(void){timer_high+=65536u;}
static uint32_t now(void)
{
    uint32_t a,b;uint16_t low;
    do{a=timer_high;low=cpu_frt_count_get();b=timer_high;}while(a!=b);
    return a+low;
}
static void vblank(void *p){(void)p;++video_fields;}
void user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,VDP2_TVMD_HORZ_NORMAL_A,VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,2,2,5));
    vdp2_sprite_priority_set(0,6);
    vdp1_env_t env;vdp1_env_default_init(&env);env.erase_color=RGB1555(0,0,0,0);
    vdp1_env_set(&env);vdp1_sync_interval_set(-1);
    vdp_sync_vblank_out_set(vblank,NULL);vdp2_tvmd_display_set();
}
static bool upload(unsigned selected)
{
    const chart_view_t *v=&chart_views[selected];
    if(v->count>509||v->bytes>partitions.texture_size)return false;
    uint32_t offset=0,hash=2166136261u;
    for(unsigned i=0;i<v->count;++i){
        const chart_t *c=&v->charts[i];
        if(c->source_triangle!=i||!c->width||(c->width%8)||c->width>128||c->height<2||c->height>128)return false;
        uint32_t size=(uint32_t)c->width*c->height*2;
        if(offset>v->bytes||size>v->bytes-offset)return false;
        addresses[i]=(uint16_t)((((uintptr_t)partitions.texture_base&0x7ffffu)+offset)>>3);
        uint16_t *dest=(uint16_t*)((uint8_t*)partitions.texture_base+offset);
        memcpy(dest,c->pixels,size);
        for(unsigned j=0;j<size/2;++j){hash=(hash^(dest[j]>>8))*16777619u;hash=(hash^(dest[j]&255u))*16777619u;}
        offset+=size;order[i]=i;
    }
    if(offset!=v->bytes||hash!=v->hash)return false;
    /* Stable far-to-near painter order; depth-buffer equivalence is NOT assumed. */
    for(unsigned i=1;i<v->count;++i){unsigned j=i;uint16_t key=order[i];
        while(j&&v->charts[order[j-1]].depth>v->charts[key].depth){order[j]=order[j-1];--j;}order[j]=key;
    }
    fzx_chart_hash=hash;fzx_chart_bytes=offset;fzx_chart_count=v->count;
    return true;
}
static void build(unsigned selected)
{
    const chart_view_t *v=&chart_views[selected];
    const vdp1_cmdt_draw_mode_t mode={.color_mode=5,.end_code_disable=true,.trans_pixel_disable=true};
    for(unsigned i=0;i<v->count;++i){
        unsigned index=order[i];const chart_t *c=&v->charts[index];vdp1_cmdt_t *cmd=&commands[i+2];
        memset(cmd,0,sizeof(*cmd));vdp1_cmdt_distorted_sprite_set(cmd);vdp1_cmdt_draw_mode_set(cmd,mode);
        cmd->cmd_srca=addresses[index];cmd->cmd_size=(uint16_t)((c->width/8)<<8|c->height);
        cmd->cmd_xa=c->xy[0]+96;cmd->cmd_ya=c->xy[1]+40;
        cmd->cmd_xb=c->xy[2]+96;cmd->cmd_yb=c->xy[3]+40;
        cmd->cmd_xc=cmd->cmd_xd=c->xy[4]+96;cmd->cmd_yc=cmd->cmd_yd=c->xy[5]+40;
    }
    memset(&commands[v->count+2],0,sizeof(commands[0]));vdp1_cmdt_end_set(&commands[v->count+2]);
    list.count=v->count+3;
}
int main(void)
{
    cpu_intc_mask_set(0);cpu_frt_init(CPU_FRT_CLOCK_DIV_32);cpu_frt_ovi_set(overflow);cpu_frt_interrupt_priority_set(12);
    dbgio_init();dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);dbgio_dev_font_load();
    vdp1_vram_partitions_set(512,400*1024,0,0);vdp1_vram_partitions_get(&partitions);
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223),origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(&commands[0]);vdp1_cmdt_vtx_system_clip_coord_set(&commands[0],clip);
    vdp1_cmdt_local_coord_set(&commands[1]);vdp1_cmdt_vtx_local_coord_set(&commands[1],origin);
    dbgio_printf("\x1b[H\x1b[2J\x1b[2;2H NATIVE VDP1 CHART PROBE\n\x1b[4;2H %s\n\x1b[24;2H BAKED POSES / ADAPTATION UNREVIEWED",
        FZX_CHART_FIXTURE?"SYNTHETIC FIXTURE":"LOCAL SOURCE ART");dbgio_flush();vdp2_sync();
    unsigned loaded=FZX_CHART_VIEWS;
    for(;;){
        uint32_t field=video_fields;vdp1_sync_wait();vdp2_sync_wait();fzx_chart_wait_fields=video_fields-field;
        if(!fzx_chart_hold)fzx_chart_case=(video_fields/240)%FZX_CHART_VIEWS;
        unsigned selected=fzx_chart_case%FZX_CHART_VIEWS;
        if(loaded!=selected){uint32_t start=now();if(!upload(selected)){fzx_chart_status=2;for(;;){}}
            fzx_chart_upload_ticks=now()-start;loaded=selected;}
        uint32_t start=now();build(selected);fzx_chart_build_ticks=now()-start;
        vdp1_sync_cmdt_list_put(&list,0);vdp1_sync_render();vdp1_sync();vdp2_sync();
        fzx_chart_completed_case=selected;++fzx_chart_sequence;fzx_chart_status=1;
    }
}
