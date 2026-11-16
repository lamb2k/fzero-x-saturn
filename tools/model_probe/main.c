/* Fixed-pose material/raster cost probe; no race/camera/hardware acceptance. */
#include <yaul.h>
#include <string.h>
#include "fzx_model.h"
#include "model_probe.h"

_Static_assert(FZX_MODEL_FIXTURE==FZX_EXPECT_MODEL_FIXTURE,"Wrong model provenance");
static uint16_t pixels[128*128];
static uint32_t depth[128*128];
static fzx_model_image_t raster={pixels,depth,128,128,0,0,0};
static vdp1_cmdt_t commands[4];
static vdp1_cmdt_list_t list={.cmdts=commands,.count=4};
static vdp1_vram_partitions_t partitions;
volatile uint32_t video_fields;
volatile uint32_t fzx_model_probe_status; /* 1 rendered, 2 invalid pack/budget */
volatile uint32_t fzx_model_probe_case;
volatile uint32_t fzx_model_probe_hold;
volatile uint32_t fzx_model_probe_hash;
volatile uint32_t fzx_model_probe_render_ticks;
volatile uint32_t fzx_model_probe_total_ticks;
volatile uint32_t fzx_model_probe_pixels;
volatile uint32_t fzx_model_probe_tested;
volatile uint32_t fzx_model_probe_draws;
volatile uint32_t fzx_model_probe_sequence;
volatile uint32_t fzx_model_probe_completed_case;
volatile uint32_t fzx_model_probe_render_fields;
static volatile uint32_t timer_high;

static void timer_overflow(void){timer_high+=65536u;}
static uint32_t timer_now(void)
{
    uint32_t high,after;uint16_t low;
    do {high=timer_high;low=cpu_frt_count_get();after=timer_high;} while(high!=after);
    return high+low;
}

static void vblank(void *work){(void)work;++video_fields;}
void user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,VDP2_TVMD_HORZ_NORMAL_A,VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe),RGB1555(1,2,2,5));
    vdp2_sprite_priority_set(0,6);
    vdp1_env_t env;vdp1_env_default_init(&env);env.erase_color=RGB1555(0,0,0,0);
    vdp1_env_set(&env);vdp1_sync_interval_set(-1);
    vdp_sync_vblank_out_set(vblank,NULL);vdp2_tvmd_display_set();
}

int main(void)
{
    cpu_intc_mask_set(0);cpu_frt_init(CPU_FRT_CLOCK_DIV_32);
    cpu_frt_ovi_set(timer_overflow);cpu_frt_interrupt_priority_set(12);
    dbgio_init();dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);dbgio_dev_font_load();
    vdp1_vram_partitions_get(&partitions);
    if(sizeof(pixels)>partitions.texture_size){fzx_model_probe_status=2;for(;;){}}
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223),origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(&commands[0]);vdp1_cmdt_vtx_system_clip_coord_set(&commands[0],clip);
    vdp1_cmdt_local_coord_set(&commands[1]);vdp1_cmdt_vtx_local_coord_set(&commands[1],origin);
    vdp1_cmdt_normal_sprite_set(&commands[2]);
    const vdp1_cmdt_draw_mode_t mode={.color_mode=5,.end_code_disable=true};
    vdp1_cmdt_draw_mode_set(&commands[2],mode);
    commands[2].cmd_srca=(uint16_t)(((uintptr_t)partitions.texture_base&0x7ffffu)>>3);
    commands[2].cmd_size=(uint16_t)((128u/8u)<<8|128u);
    commands[2].cmd_xa=96;commands[2].cmd_ya=40;vdp1_cmdt_end_set(&commands[3]);
    dbgio_printf("\x1b[H\x1b[2J\x1b[2;2H NATIVE MODEL MATERIAL PROBE\n\x1b[4;2H %s\n\x1b[24;2H STATIC POSES / NO GAME SIMULATION",
                 FZX_MODEL_FIXTURE?"SYNTHETIC FIXTURE":"LOCAL SOURCE ART / REVIEW PENDING");
    dbgio_flush();vdp2_sync();
    for(;;){
        vdp1_sync_wait();vdp2_sync_wait();
        if(!fzx_model_probe_hold)fzx_model_probe_case=(video_fields/240u)%FZX_MODEL_VIEW_COUNT;
        const unsigned selected=fzx_model_probe_case%FZX_MODEL_VIEW_COUNT;
        const uint32_t start=timer_now(),start_field=video_fields;
        if(!fzx_model_render(&fzx_model_views[selected],fzx_model_textures,8,
                             fzx_model_body,fzx_model_fog,&raster)){
            fzx_model_probe_status=2;for(;;){}
        }
        fzx_model_probe_render_ticks=timer_now()-start;
        fzx_model_probe_render_fields=video_fields-start_field;
        uint32_t hash=2166136261u;
        for(unsigned i=0;i<128u*128u;++i){hash=(hash^(pixels[i]>>8))*16777619u;hash=(hash^(pixels[i]&255u))*16777619u;}
        fzx_model_probe_hash=hash;fzx_model_probe_pixels=raster.drawn_pixels;
        fzx_model_probe_tested=raster.tested_pixels;fzx_model_probe_draws=raster.drawn_triangles;
        memcpy(partitions.texture_base,pixels,sizeof(pixels));
        fzx_model_probe_total_ticks=timer_now()-start;
        fzx_model_probe_completed_case=selected;++fzx_model_probe_sequence;
        fzx_model_probe_status=1;
        vdp1_sync_cmdt_list_put(&list,0);vdp1_sync_render();vdp1_sync();vdp2_sync();
    }
}
