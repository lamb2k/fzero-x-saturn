/* Native Saturn backend for the F-Zero X engineering showcase.
 * MIT; Copyright (c) 2026 SH2ZAM Contributors.
 * Yaul API references: yaul-org/libyaul-examples/vdp1-drawing and vdp1-mesh.
 * This backend submits the shared core's polygons; it does not emulate N64.
 */
#include <yaul.h>
#include <stdint.h>
#include <string.h>

#include "fzx.h"

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

static void
vblank_out(void *work)
{
    (void)work;
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
    vdp1_sync_interval_set(-1);

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

    dbgio_printf("\x1b[H\x1b[2J F-ZERO X / SH2ZAM\n"
                 "\x1b[26;1H D-PAD STEER   A/C GO   B BRAKE\n"
                 " START RESET");

    while (true) {
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
        }

        const fzx_frame_t *frame = fzx_render();
        commands_build(frame);
        dbgio_printf("\x1b[2;1H SPEED %04lu   LAP %02lu   POLYS %03lu "
                     "\x1b[3;1H %s                     ",
                     (unsigned long)frame->speed,
                     (unsigned long)frame->lap,
                     (unsigned long)frame->count,
                     (frame->flags & FZX_FRAME_IMPORTED) ? "IMPORTED COURSE"
                                                        : "DEMO COURSE");
        dbgio_flush();

        vdp1_sync_cmdt_list_put(&command_list, 0);
        vdp1_sync_render();
        vdp1_sync();
        vdp2_sync();
        /* Wait for both the polygon frame and the HUD transfer. */
        vdp1_sync_wait();
        vdp2_sync_wait();
    }
}
