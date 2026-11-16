/* Native asset display probe, not a playable game or simulation acceptance. */
#include <yaul.h>
#include <string.h>
#include "fzx_hud.h"
#include "hud_native.h"
#include "hud.h"

_Static_assert(FZX_HUD_FIXTURE == FZX_EXPECT_HUD_FIXTURE, "Wrong HUD provenance for probe mode");
enum { CAPACITY = FZX_HUD_MAX_DRAWS + 3 };
_Static_assert(CAPACITY <= VDP1_VRAM_DEFAULT_CMDT_COUNT, "HUD command capacity");
static vdp1_cmdt_t commands[CAPACITY];
static vdp1_cmdt_list_t list = {.cmdts=commands};
static fzx_hud_list_t draws;
static vdp1_vram_partitions_t partitions;
volatile uint32_t video_fields;
volatile uint32_t fzx_hud_probe_status; /* 0 booting, 1 drawing, 2 invalid atlas */
volatile uint32_t fzx_hud_probe_case;
volatile uint32_t fzx_hud_probe_hold; /* set for stable reference stills */

static void vblank(void *work) { (void)work; ++video_fields; }

void user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
                            VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3,0x1fffe), RGB1555(1,2,2,5));
    vdp2_sprite_priority_set(0,6);
    vdp1_env_t env;
    vdp1_env_default_init(&env);
    env.erase_color=RGB1555(0,0,0,0);
    vdp1_env_set(&env);
    vdp1_sync_interval_set(-1);
    vdp_sync_vblank_out_set(vblank,NULL);
    vdp2_tvmd_display_set();
}

static void build(void)
{
    const unsigned speeds[8]={0,9,10,99,100,999,1000,9999};
    const unsigned times[8]={0,9,10,999,1000,59999,60000,3599999};
    const unsigned selected=fzx_hud_probe_case%8u;
    const fzx_hud_snapshot_t snapshot={times[selected],speeds[selected],
        (uint16_t)(1u+selected%3u),3,selected==7u};
    if (!fzx_hud_build(&fzx_hud_atlas,&snapshot,&draws)) {
        fzx_hud_probe_status=2;
        return;
    }
    memset(commands,0,sizeof(commands));
    const int16_vec2_t clip=INT16_VEC2_INITIALIZER(319,223);
    const int16_vec2_t origin=INT16_VEC2_INITIALIZER(0,0);
    vdp1_cmdt_system_clip_coord_set(&commands[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&commands[0],clip);
    vdp1_cmdt_local_coord_set(&commands[1]);
    vdp1_cmdt_vtx_local_coord_set(&commands[1],origin);
    if (!fzx_hud_vdp1(&commands[2], FZX_HUD_MAX_DRAWS, &draws, partitions.texture_base)) {
        fzx_hud_probe_status=2;
        return;
    }
    vdp1_cmdt_end_set(&commands[draws.count+2]);
    list.count=(uint16_t)(draws.count+3);
    fzx_hud_probe_status=1;
}

int main(void)
{
    cpu_intc_mask_set(0);
    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    vdp1_vram_partitions_get(&partitions);
    if(sizeof(fzx_hud_pixels)>partitions.texture_size) {
        fzx_hud_probe_status=2;
        for(;;) {}
    }
    memcpy(partitions.texture_base,fzx_hud_pixels,sizeof(fzx_hud_pixels));
    dbgio_printf("\x1b[H\x1b[2J\x1b[11;2H NATIVE HUD ASSET PROBE\n"
                 "\x1b[13;2H %s\n\x1b[15;2H NO GAME SIMULATION",
                 FZX_HUD_FIXTURE?"SYNTHETIC TEST PIXELS":"LOCAL SOURCE ART / REVIEW PENDING");
    dbgio_flush();
    vdp2_sync();
    for(;;) {
        vdp1_sync_wait();
        vdp2_sync_wait();
        if(!fzx_hud_probe_hold) fzx_hud_probe_case=(video_fields/120u)%8u;
        build();
        if(fzx_hud_probe_status!=1) for(;;) {}
        vdp1_sync_cmdt_list_put(&list,0);
        vdp1_sync_render();
        vdp1_sync();
        vdp2_sync();
    }
}
