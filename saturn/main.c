/* Native Saturn backend for the F-Zero X engineering showcase.
 * MIT; Copyright (c) 2026 SH2ZAM Contributors.
 * Yaul API references: yaul-org/libyaul-examples/vdp1-drawing and vdp1-mesh.
 * This backend submits the shared core's polygons; it does not emulate N64.
 */
#include <yaul.h>
#include <stdint.h>
#include <string.h>

#include "fzx.h"

/* G1.4 probe knobs; defaults keep the original behaviour.
 * FZX_HUD_EVERY: rewrite and transfer the debug HUD every N pictures.
 * FZX_SYNC_WAIT: 2 = wait for VDP1 then VDP2 (original); 1 = one VDP2 wait (Yaul's examples). */
#ifndef FZX_HUD_EVERY
#define FZX_HUD_EVERY 1
#endif
#ifndef FZX_SYNC_WAIT
#define FZX_SYNC_WAIT 2
#endif
/* FZX_VDP1_INTERVAL: Yaul VDP1 sync interval. -1 = variable, change after draw end (original);
 * 0 = fixed, framebuffer change every field. */
#ifndef FZX_VDP1_INTERVAL
#define FZX_VDP1_INTERVAL (-1)
#endif

enum {
    COMMAND_SYSTEM_CLIP = 0,
    COMMAND_LOCAL_COORD = 1,
    COMMAND_FIRST_POLYGON = 2,
    COMMAND_CAPACITY = FZX_MAX_POLYGONS + 3u,
    MAX_CATCHUP_FIELDS = 5
};

_Static_assert(COMMAND_CAPACITY <= VDP1_VRAM_DEFAULT_CMDT_COUNT,
               "showcase commands must fit Yaul's default VDP1 partition");

/* vdp1_cmdt_t supplies the 32-byte alignment required by the SDK/DMA.
 * A completed VDP1 sync is required before this single buffer is reused. */
static vdp1_cmdt_t commands[COMMAND_CAPACITY];
static vdp1_cmdt_list_t command_list;
static smpc_peripheral_digital_t digital;
static volatile uint32_t video_fields;
/* SH-2 free-running timer at clock/128, sampled each field: an independent check that
 * one field is ~477,750 CPU cycles (NTSC), so FLD counts real fields. */
static volatile uint32_t frt_ticks_total;
static uint16_t frt_previous;

static void
vblank_out(void *work)
{
    (void)work;
    const uint16_t now = cpu_frt_count_get();
    frt_ticks_total += (uint16_t)(now - frt_previous);
    frt_previous = now;
    ++video_fields;
    smpc_peripheral_intback_issue();
}

void
user_init(void)
{
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
                            VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE),
                             RGB1555(1, 1, 2, 6));

    /* Yaul's default debug console uses NBG3 at priority 7. */
    vdp2_sprite_priority_set(0, 6);

    vdp1_env_t env;
    vdp1_env_default_init(&env);
    /* Transparent clear leaves the VDP2 back-screen color visible. */
    env.erase_color = RGB1555(0, 0, 0, 0);
    vdp1_env_set(&env);
    /* Variable interval: present only after the polygon list has finished. */
    vdp1_sync_interval_set(FZX_VDP1_INTERVAL);

    cpu_frt_init(CPU_FRT_CLOCK_DIV_128);
    vdp_sync_vblank_out_set(vblank_out, NULL);
    vdp2_tvmd_display_set();
}

static void
commands_init(void)
{
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(FZX_WIDTH - 1,
                                                   FZX_HEIGHT - 1);
    const int16_vec2_t origin = INT16_VEC2_INITIALIZER(0, 0);

    memset(commands, 0, sizeof(commands));
    command_list.cmdts = commands;
    command_list.count = 3;

    vdp1_cmdt_system_clip_coord_set(&commands[COMMAND_SYSTEM_CLIP]);
    vdp1_cmdt_vtx_system_clip_coord_set(&commands[COMMAND_SYSTEM_CLIP], clip);
    vdp1_cmdt_local_coord_set(&commands[COMMAND_LOCAL_COORD]);
    vdp1_cmdt_vtx_local_coord_set(&commands[COMMAND_LOCAL_COORD], origin);
    vdp1_cmdt_end_set(&commands[COMMAND_FIRST_POLYGON]);
}

static uint32_t
buttons_read(void)
{
    uint32_t buttons = 0;
    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &digital);
    if (!digital.connected) {
        return 0;
    }

    /* In Yaul, "pressed" is the current state; "held" is the down edge. */
    const uint16_t down = digital.pressed.raw;
    if (down & PERIPHERAL_DIGITAL_LEFT)  buttons |= FZX_LEFT;
    if (down & PERIPHERAL_DIGITAL_RIGHT) buttons |= FZX_RIGHT;
    if (down & (PERIPHERAL_DIGITAL_A | PERIPHERAL_DIGITAL_C))
        buttons |= FZX_ACCEL;
    if (down & PERIPHERAL_DIGITAL_B) buttons |= FZX_BRAKE;
    if (digital.held.raw & PERIPHERAL_DIGITAL_START) buttons |= FZX_RESET;
    return buttons;
}

static void
commands_build(const fzx_frame_t *frame)
{
    const vdp1_cmdt_draw_mode_t draw_mode = { .raw = 0 };
    const uint32_t count = frame->count <= FZX_MAX_POLYGONS
                         ? frame->count : FZX_MAX_POLYGONS;

    /* The core supplies clipped polygons in painter order, far to near. */
    for (uint32_t i = 0; i < count; ++i) {
        const fzx_poly_t *p = &frame->polys[i];
        vdp1_cmdt_t *cmd = &commands[COMMAND_FIRST_POLYGON + i];
        const rgb1555_t color = { .raw = p->color };

        /* polygon_set also removes a previous draw-END bit at this slot. */
        vdp1_cmdt_polygon_set(cmd);
        vdp1_cmdt_draw_mode_set(cmd, draw_mode);
        vdp1_cmdt_color_set(cmd, color);
        cmd->cmd_xa = p->x[0]; cmd->cmd_ya = p->y[0];
        cmd->cmd_xb = p->x[1]; cmd->cmd_yb = p->y[1];
        cmd->cmd_xc = p->x[2]; cmd->cmd_yc = p->y[2];
        cmd->cmd_xd = p->x[3]; cmd->cmd_yd = p->y[3];
    }

    vdp1_cmdt_t *end = &commands[COMMAND_FIRST_POLYGON + count];
    memset(end, 0, sizeof(*end));
    vdp1_cmdt_end_set(end);
    command_list.count = (uint16_t)(count + 3u);
}

int
main(void)
{
    cpu_intc_mask_set(0);
    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    commands_init();
    fzx_init();

    const uint32_t fields_per_second =
        vdp2_tvmd_tv_standard_get() == VDP2_TVMD_TV_STANDARD_PAL ? 50u : 60u;
    uint32_t previous_field = video_fields;
    uint32_t tick_remainder = 0;
    uint32_t pending_reset = 0;
    uint32_t ticks = 0;
    uint32_t pictures = 0;
    uint32_t work_ticks = 0;   /* FRT/128 ticks spent in step + render + command build */
    uint32_t render_ticks = 0, build_ticks = 0;

    dbgio_printf("\x1b[H\x1b[2J F-ZERO X / SH2ZAM\n"
                 "\x1b[26;1H D-PAD STEER   A/C GO   B BRAKE\n"
                 " START RESET");

    while (true) {
        const uint16_t work_start = cpu_frt_count_get();
        const uint32_t buttons = buttons_read();
        pending_reset |= buttons & FZX_RESET;
        const uint32_t field = video_fields;
        uint32_t elapsed = field - previous_field;
        previous_field = field;
        /* A long pause cannot create an unbounded simulation catch-up loop. */
        if (elapsed > MAX_CATCHUP_FIELDS) elapsed = MAX_CATCHUP_FIELDS;
        tick_remainder += elapsed * 60u;
        while (tick_remainder >= fields_per_second) {
            fzx_step((buttons & ~FZX_RESET) | pending_reset);
            pending_reset = 0;
            tick_remainder -= fields_per_second;
            ++ticks;
        }

        const uint16_t render_start = cpu_frt_count_get();
        const fzx_frame_t *frame = fzx_render();
        const uint16_t build_start = cpu_frt_count_get();
        commands_build(frame);
        const uint16_t work_end = cpu_frt_count_get();
        render_ticks += (uint16_t)(build_start - render_start);
        build_ticks += (uint16_t)(work_end - build_start);
        work_ticks += (uint16_t)(work_end - work_start);
        if (pictures % FZX_HUD_EVERY == 0) {
        dbgio_printf("\x1b[2;1H SPEED %04lu   LAP %02lu   POLYS %03lu "
                     "\x1b[3;1H %s                     "
                     "\x1b[4;1H SIM %s%s"
                     "\x1b[5;1H FLD %06lu TCK %06lu PIC %06lu"
                     "\x1b[6;1H PAD %04X CYC/FLD %07lu"
                     "\x1b[7;1H CPU/PIC %07lu R %07lu B %06lu",
                     (unsigned long)frame->speed,
                     (unsigned long)frame->lap,
                     (unsigned long)frame->count,
                     (frame->flags & FZX_FRAME_IMPORTED) ? "IMPORTED COURSE"
                                                        : "DEMO COURSE",
                     fzx_sim_name(),
                     fzx_sim_id() == FZX_SIM_PROTO ? " (PROTOTYPE, NOT ORIGINAL)" : "",
                     (unsigned long)field, (unsigned long)ticks,
                     (unsigned long)pictures,
                     (unsigned int)(digital.connected ? digital.pressed.raw : 0xFFFFu),
                     (unsigned long)(field > 1 ? (frt_ticks_total / (field - 1)) * 128u : 0u),
                     (unsigned long)(pictures ? (work_ticks / pictures) * 128u : 0u),
                     (unsigned long)(pictures ? (render_ticks / pictures) * 128u : 0u),
                     (unsigned long)(pictures ? (build_ticks / pictures) * 128u : 0u));
        dbgio_flush();
        }

        vdp1_sync_cmdt_list_put(&command_list, 0);
        vdp1_sync_render();
        vdp1_sync();
        vdp2_sync();
#if FZX_SYNC_WAIT == 2
        /* Wait for both the polygon frame and the HUD transfer. */
        vdp1_sync_wait();
        vdp2_sync_wait();
#else
        vdp2_sync_wait();
#endif
        ++pictures;
    }
}
