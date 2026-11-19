/* Engine line, milestone E0 (docs/ENGINE_PLAN.md): track flythrough.
 *
 * A separate Saturn image with its own main loop. It links no original game
 * code. The camera rides a baked path (ENGPATH.PAK); the engine derives the
 * view, the backdrop, the skyline and the track pick from it in fixed point.
 * VDP2 draws sky, floor and clouds; the slave SH-2 picks and builds the track;
 * VDP1 command and shade banks alternate when FZX_VDP1_DBANK is 1.
 *
 * Reused presentation modules keep their names for now (fzx_source_*, and the
 * low-RAM symbols sat_stub_data_end and fzx_input_log, which the Makefile
 * defines for this image's own layout). */
#include <yaul.h>
#include <string.h>
#include "engine_path.h"
#include "engine_path_identity.h"
#include "sky_native.h"
#include "skyline_native.h"
#include "planes_native.h"
#include "floor_rbg_native.h"
#include "clouds_vdp2_native.h"
#include "road_native.h"
#include "road_stream_native.h"
#include "road_lod_identity.h"
#include "slave_road.h"
#include "texture_cd.h"
#include "sh2zam/segmesh_experimental.h"
#include "fzx_road_stream_phase.h"
#if FZX_ROAD_STREAM_PHASES
/* Diagnostic builds only: the track build's phase totals (slave timer units). */
fzx_rsp_state_t fzx_rsp;
volatile uint32_t fzx_scp_road[FZX_RSP_PHASES], fzx_scp_road_calls;
#include "fzx_scene_phase.h"
/* and the master's floor and cloud table phases (master timer units). */
volatile uint32_t fzx_scp_ticks[FZX_SCP_LAYERS][FZX_SCP_PHASES], fzx_scp_calls[FZX_SCP_LAYERS];
/* and the track build's per-frame counters, summed (quad clips, piece clips, pieces, ...). */
volatile uint32_t fzx_e0_rsp_counts[FZX_RSPC_COUNTERS];
#endif
#ifndef FZX_VDP1_DBANK
#define FZX_VDP1_DBANK 0
#endif
#ifndef FZX_VDP1_DBANK_STALE
#define FZX_VDP1_DBANK_STALE 0
#endif
#if FZX_VDP1_DBANK
#include "sh2zam/vdp1_banks_experimental.h"
#endif

enum {
    WIDTH = 320, HEIGHT = 224,
    CMD_SYSTEM_CLIP = 0, CMD_LOCAL = 1, CMD_FIRST = 2,
    SCENE_CAPACITY = FZX_SLAVE_ROAD_RESERVE + FZX_ROAD_STREAM_MAX_DRAWS + 8u,
    CMD_CAPACITY = SCENE_CAPACITY + 3u,
    PATH_BASE = 0x00200000u, PATH_CAPACITY = 0x0008C000u, /* Makefile: sat_stub_data_end follows */
    ROW_MAGIC = 0x45305246u, /* "E0RF" */
    ROW_WORDS = 9u
};
_Static_assert(FZX_ENGINE_PATH_BYTES + 64u <= PATH_CAPACITY, "path pack exceeds its low-RAM reservation");
_Static_assert(CMD_CAPACITY <= VDP1_VRAM_DEFAULT_CMDT_COUNT, "commands exceed the VDP1 command partition");

extern const unsigned char fzx_course_record[0x7e0];
extern unsigned char sat_stub_data_end[];

volatile uint32_t fzx_input_count; /* the presented logic tick, read by the reused modules */
fzx_view_t fzx_native_view;

/* Status for captures. 1 running, 2 path done; faults are 16 and above. */
volatile uint32_t fzx_e0_status, fzx_e0_done, fzx_e0_fault;
/* [0] magic, [1] rows written, [2] row capacity, [3] rows base, [4] fields at
 * start, [5] frames built, [6] fields per second, [7] VDP1 texture bytes used,
 * [8] plan groups maximum, [9] road commands maximum, [10] prelude maximum,
 * [11] slave busy sum at the last frame, [12] FRT ticks per field, [13] pick
 * BAD_ARGS, [14] pick TOO_MANY, [15] visible chunks maximum. */
volatile uint32_t fzx_e0_report[16];
/* Debugger hold: when nonzero, the camera stays at this tick (stills). */
volatile uint32_t fzx_e0_hold_tick;
#define HOLD_TICK (*(volatile uint32_t *)((uintptr_t)&fzx_e0_hold_tick | CPU_CACHE_THROUGH))

static fzx_engine_path_t path;
static vdp1_cmdt_t commands_buf[CMD_CAPACITY];
static vdp1_cmdt_list_t command_list;
static vdp1_vram_partitions_t partitions;
static void *skyline_vram, *planes_vram, *road_pixels, *road_shades[2];
static volatile uint32_t video_fields;
static volatile uint16_t vblank_frt, field_frt;

/* ---- frame table: one row per presented frame, in low work RAM ----------- */
typedef struct {
    uint32_t tick;          /* logic tick this frame shows */
    uint32_t build_field;   /* field counter when the build began */
    uint32_t present_field; /* field at which VDP1 finished and the frame was swapped in */
    uint16_t build_ticks, master_ticks, wait_road_ticks, slave_ticks;
    uint16_t wait_prev_ticks, prelude, road, groups;
    uint16_t pick_ticks, flags;
    uint16_t draw_ticks; /* list submit to VDP1's end-of-draw interrupt */
    uint16_t submit_frt;
} row_t;
_Static_assert(sizeof(row_t) == ROW_WORDS * 4u, "frame row layout");
static row_t *rows;
static uint32_t row_capacity;
static volatile uint32_t pending_row; /* index + 1 of the frame VDP1 is drawing */

/* VDP1's end-of-draw interrupt: stamp the free-running timer, then run Yaul's
 * own handler with the interrupt frame untouched (the jump reads r0 before
 * its delay slot restores it). */
volatile uint32_t fzx_e0_draw_end_frt __attribute__((used));
void (*volatile fzx_e0_draw_end_next)(void) __attribute__((used));
void fzx_e0_draw_end(void);
__asm__(
    ".section .text.fzx_e0_draw_end,\"ax\",@progbits\n"
    ".align 2\n"
    ".global _fzx_e0_draw_end\n"
    "_fzx_e0_draw_end:\n"
    "    mov.l r0,@-r15\n"
    "    mov.l r1,@-r15\n"
    "    mov.l r2,@-r15\n"
    "    mov.l 1f,r1\n"
    "    mov.b @r1,r0\n"
    "    extu.b r0,r2\n"
    "    shll8 r2\n"
    "    mov.b @(1,r1),r0\n"
    "    extu.b r0,r0\n"
    "    or r2,r0\n"
    "    mov.l 2f,r1\n"
    "    mov.l r0,@r1\n"
    "    mov.l 3f,r0\n"
    "    mov.l @r0,r0\n"
    "    mov.l @r15+,r2\n"
    "    mov.l @r15+,r1\n"
    "    jmp @r0\n"
    "    mov.l @r15+,r0\n"
    "    .align 2\n"
    "1:  .long 0xfffffe12\n"
    "2:  .long _fzx_e0_draw_end_frt\n"
    "3:  .long _fzx_e0_draw_end_next\n"
    ".text\n");

/* The floor's render-end callback calls this when a frame has been drawn. */
void fzx_fp_present(void) {
    const uint32_t i = pending_row;
    if (i && i <= row_capacity && !rows[i - 1u].present_field) {
        rows[i - 1u].present_field = video_fields;
        rows[i - 1u].draw_ticks = (uint16_t)((uint16_t)fzx_e0_draw_end_frt - rows[i - 1u].submit_frt);
    }
}

/* Values the slave wrote: read from memory, not through the master's cache. */
#define SLAVE32(x) (*(volatile uint32_t *)((uintptr_t)&(x) | CPU_CACHE_THROUGH))

static void fail(uint32_t code, const char *text, uint32_t detail) {
    fzx_e0_fault = code; fzx_e0_status = code;
    dbgio_printf("\x1b[H\x1b[2J E0 %s %lu\n", text, (unsigned long)detail);
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
    for (;;) {}
}

static void vblank_out(void *work) {
    (void)work;
    const uint16_t now = cpu_frt_count_get();
    field_frt = (uint16_t)(now - vblank_frt);
    vblank_frt = now;
    ++video_fields;
}

void user_init(void) {
    const unsigned command_reserve = (CMD_CAPACITY + 31u) & ~31u;
    vdp1_vram_partitions_set(command_reserve,
        VDP1_VRAM_DEFAULT_TEXTURE_SIZE + (VDP1_VRAM_DEFAULT_CMDT_COUNT - command_reserve) * 32u,
        VDP1_VRAM_DEFAULT_GOURAUD_COUNT, VDP1_VRAM_DEFAULT_CLUT_COUNT);
    smpc_peripheral_init();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 1, 2, 6));
    vdp2_sprite_priority_set(0, 6);
    vdp1_env_t env;
    vdp1_env_default_init(&env);
    env.erase_color = RGB1555(0, 0, 0, 0);
    vdp1_env_set(&env);
    vdp1_sync_interval_set(-1); /* present when the list has been drawn */
    vdp_sync_vblank_out_set(vblank_out, NULL);
    vdp2_tvmd_display_set();
}

static void commands_init(void) {
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(WIDTH - 1, HEIGHT - 1);
    const int16_vec2_t origin = INT16_VEC2_INITIALIZER(0, 0);
    memset(commands_buf, 0, sizeof(commands_buf));
    command_list.cmdts = commands_buf;
    command_list.count = 3;
    vdp1_cmdt_system_clip_coord_set(&commands_buf[CMD_SYSTEM_CLIP]);
    vdp1_cmdt_vtx_system_clip_coord_set(&commands_buf[CMD_SYSTEM_CLIP], clip);
    vdp1_cmdt_local_coord_set(&commands_buf[CMD_LOCAL]);
    vdp1_cmdt_vtx_local_coord_set(&commands_buf[CMD_LOCAL], origin);
    vdp1_cmdt_end_set(&commands_buf[CMD_FIRST]);
}

/* ---- track pick: runs on the slave SH-2, ahead of the road build ---------- */
static const fzx_engine_cam_t *volatile job_cam;
static volatile uint32_t job_tick;
static int32_t pick_depth[FZX_EPATH_MAX_CHUNKS + 1u];
static int8_t pick_state[FZX_EPATH_MAX_CHUNKS + 1u];
static sh2z_segmesh_run_t pick_runs[REF_ROAD_GROUPS];
volatile uint32_t fzx_e0_pick[6]; /* result, runs, marked, bad view, FRT ticks, calls */
_Static_assert(sizeof(sh2z_segmesh_anchor_t) == sizeof(fzx_engine_anchor_t), "anchor layout");

bool fzx_engine_slave_pick(void) {
    RefRoadPlan *out = &fzx_source_road_plan;
    const fzx_engine_consts_t *k = &path.consts;
    const uint16_t t0 = cpu_frt_count_get();
    fzx_engine_pick_t pv;
    out->faults = 0; out->count = 0; out->visible_chunks = 0;
    out->outer = 1; /* the camera is outside the track surface on this course */
    out->fog_start = k->fog_start;
    memcpy(out->fog, k->fog, 4);
    out->primitive[0] = (unsigned char)(255u - ((job_tick * 8u) & 255u));
    out->primitive[1] = out->primitive[2] = 0; out->primitive[3] = 255;
    ++fzx_e0_pick[5];
    if (!fzx_engine_pick_view(&pv, job_cam, k)) { ++fzx_e0_pick[3]; return false; }
    sh2z_segmesh_view_t sv;
    for (unsigned i = 0; i < 4u; ++i) {
        sv.x[i] = pv.rows[i]; sv.y[i] = pv.rows[4u + i]; sv.w[i] = pv.rows[8u + i]; sv.d[i] = pv.rows[12u + i];
    }
    sv.far = pv.thresholds[0]; sv.slope = pv.thresholds[1]; sv.split = pv.thresholds[2]; sv.floor = pv.thresholds[3];
    const sh2z_segmesh_scratch_t scratch = { pick_depth, pick_state };
    uint32_t runs = 0, marked = 0;
    const int r = sh2z_segmesh_pick((const sh2z_segmesh_anchor_t *)(const void *)path.anchors, path.chunks, &sv,
                                    &scratch, pick_runs, REF_ROAD_GROUPS, &runs, &marked);
    fzx_e0_pick[0] = (uint32_t)r; fzx_e0_pick[1] = runs; fzx_e0_pick[2] = marked;
    if (r == SH2Z_SEGMESH_BAD_ARGS) { ++fzx_e0_report[13]; return false; }
    out->visible_chunks = marked;
    if (r == SH2Z_SEGMESH_TOO_MANY) { out->faults = 2; ++fzx_e0_report[14]; return false; }
    out->count = runs;
    for (uint32_t i = 0; i < runs; ++i) {
        RefRoadGroup *g = &out->groups[i];
        g->start = pick_runs[i].start; g->end = pick_runs[i].end; g->direction = pick_runs[i].direction;
        g->average_depth = (float)pick_runs[i].average / (float)(1u << pv.depth_shift);
    }
    fzx_e0_pick[4] = (uint16_t)(cpu_frt_count_get() - t0);
#if FZX_ROAD_STREAM_PHASES
    fzx_rsp_begin();
#endif
    return true;
}

/* ---- VDP1 banks ----------------------------------------------------------- */
#if FZX_VDP1_DBANK
#define DBANK_GUARD 0xdb4b5a17u
#define DBANK_GUARD_WORDS 8u
static volatile uint32_t *dbank_guard_lo, *dbank_guard_hi, *dbank_stage_lo, *dbank_stage_hi;
sh2z_vdp1_banks_t fzx_dbank_banks;
volatile uint32_t fzx_dbank_status, fzx_dbank_bytes, fzx_dbank_stage_bytes;

static void dbank_init(void) {
    const uint32_t guard = DBANK_GUARD_WORDS * 4u;
    sh2z_vdp1_banks_init(&fzx_dbank_banks, FZX_VDP1_DBANK_STALE != 0);
    {
        const uint32_t sizes[1] = { FZX_ROAD_STREAM_SHADE_BYTES }, aligns[1] = { 8u };
        uintptr_t out[1], end;
        if ((uintptr_t)partitions.gouraud_base < (uintptr_t)partitions.texture_base + partitions.texture_size ||
            !sh2z_vdp1_carve((uintptr_t)partitions.gouraud_base, partitions.gouraud_size, guard, sizes, aligns, 1u, out, &end))
            fail(40, "DBANK GOURAUD PARTITION", partitions.gouraud_size);
        fzx_dbank_bytes = (uint32_t)(end + guard - (uintptr_t)partitions.gouraud_base);
        dbank_guard_lo = (volatile uint32_t *)(uintptr_t)partitions.gouraud_base; dbank_guard_hi = (volatile uint32_t *)end;
        road_shades[1] = (void *)out[0];
        sh2z_vdp1_guard_fill(dbank_guard_lo, DBANK_GUARD_WORDS, DBANK_GUARD, 0u);
        sh2z_vdp1_guard_fill(dbank_guard_hi, DBANK_GUARD_WORDS, DBANK_GUARD, 8u);
        memcpy(road_shades[1], road_shades[0], FZX_ROAD_STREAM_SHADE_BYTES);
    }
    {
        const uint32_t sizes[4] = { sizeof(fzx_floor_rbg_t), sizeof(fzx_floor_rbg_t), sizeof(fzx_clouds_rbg_t), 224u * sizeof(uint16_t) };
        const uint32_t aligns[4] = { 4u, 4u, 4u, 4u };
        uintptr_t out[4], end;
        if ((uintptr_t)partitions.clut_base < (uintptr_t)partitions.gouraud_base + partitions.gouraud_size ||
            !sh2z_vdp1_carve((uintptr_t)partitions.clut_base, partitions.clut_size, guard, sizes, aligns, 4u, out, &end))
            fail(41, "DBANK CLUT PARTITION", partitions.clut_size);
        fzx_dbank_stage_bytes = (uint32_t)(end + guard - (uintptr_t)partitions.clut_base);
        dbank_stage_lo = (volatile uint32_t *)(uintptr_t)partitions.clut_base; dbank_stage_hi = (volatile uint32_t *)end;
        sh2z_vdp1_guard_fill(dbank_stage_lo, DBANK_GUARD_WORDS, DBANK_GUARD, 16u);
        sh2z_vdp1_guard_fill(dbank_stage_hi, DBANK_GUARD_WORDS, DBANK_GUARD, 24u);
        for (unsigned i = 0; i < 4u; ++i) memset((void *)out[i], 0, sizes[i]);
        fzx_floor_dbank((void *)out[0], (void *)out[1]);
        fzx_clouds_dbank((void *)out[2], (void *)out[3]);
    }
    fzx_dbank_status = 1;
}

static bool dbank_guards(void) {
    return sh2z_vdp1_guard_check(dbank_guard_lo, DBANK_GUARD_WORDS, DBANK_GUARD, 0u) &&
           sh2z_vdp1_guard_check(dbank_guard_hi, DBANK_GUARD_WORDS, DBANK_GUARD, 8u) &&
           sh2z_vdp1_guard_check(dbank_stage_lo, DBANK_GUARD_WORDS, DBANK_GUARD, 16u) &&
           sh2z_vdp1_guard_check(dbank_stage_hi, DBANK_GUARD_WORDS, DBANK_GUARD, 24u);
}
#endif

/* Before a build: VDP2's previous sync and the previous list's transfer are
 * done. With banks, VDP1 may still be drawing the previous frame. */
/* Early kick: the slave's track job only needs the previous command list to
 * have been transferred (and, serial, the previous draw to be done), so it
 * starts before the master waits for VDP2's sync at the field boundary. */
static void before_slave(void) {
#if FZX_VDP1_DBANK
    vdp1_sync_put_wait();
    if (sh2z_vdp1_banks_begin(&fzx_dbank_banks, false, vdp1_sync_busy())) vdp1_sync_wait();
#else
    vdp1_sync_wait();
#endif
}

static void before_master(void) {
#if FZX_VDP1_DBANK
    vdp2_sync_wait();
    const uint32_t bank = sh2z_vdp1_banks_bank(&fzx_dbank_banks);
    fzx_floor_dbank_select(bank);
    fzx_clouds_dbank_select(bank);
    if (!dbank_guards()) { fzx_dbank_status = 3; fail(42, "DBANK GUARD", 0); }
    /* A build that would write the bank VDP1 is still drawing must never run. */
    if (fzx_dbank_banks.conflicts) { fzx_dbank_status = 4; fail(43, "DBANK STALE BANK", fzx_dbank_banks.conflicts); }
#else
    vdp2_sync_wait();
#endif
}

/* The view, then the slave's job: pick the visible track and build it at a fixed slot. */
static void kick(const fzx_engine_cam_t *cam, uint32_t tick) {
    vdp1_cmdt_t *const commands = commands_buf;
    const fzx_engine_consts_t *k = &path.consts;
    fzx_engine_view(&fzx_native_view, cam, k);
    job_cam = cam; job_tick = tick;
    fzx_slave_road_args_t job;
    job.pa = k->pa; job.pb = k->pb;
    job.eye[0] = cam->eye[0]; job.eye[1] = cam->eye[1]; job.eye[2] = cam->eye[2];
    job.commands = &commands[CMD_FIRST + FZX_SLAVE_ROAD_RESERVE];
    job.capacity = SCENE_CAPACITY - FZX_SLAVE_ROAD_RESERVE;
    job.view = &fzx_native_view; job.plan = &fzx_source_road_plan;
    job.pixels = road_pixels;
#if FZX_VDP1_DBANK
    job.shades = road_shades[sh2z_vdp1_banks_bank(&fzx_dbank_banks)];
#else
    job.shades = road_shades[0];
#endif
    fzx_slave_road_start(&job);
}

static void build(row_t *row, const fzx_engine_cam_t *cam, uint32_t tick, uint16_t kicked) {
    vdp1_cmdt_t *const commands = commands_buf;
    const fzx_engine_consts_t *k = &path.consts;
    uint32_t first = 0, n = 0, road_count = 0;
    const uint16_t t0 = cpu_frt_count_get();

    if (!fzx_engine_backdrop(&fzx_source_backdrop, cam, k, tick)) fail(20, "BACKDROP", tick);
    if (!fzx_engine_skyline(&fzx_source_skyline, cam)) fail(21, "SKYLINE", tick);
    fzx_sky_status = 1; fzx_skyline_status = 1;

    /* The master's prelude: sky lines and cloud fade bands, skyline, floor. */
    if (!fzx_clouds_sky_lines(&fzx_native_view, &fzx_source_backdrop)) fail(22, "SKY LINES", fzx_sky_stats.fault);
    if (!fzx_clouds_native(&commands[CMD_FIRST], FZX_SLAVE_ROAD_RESERVE - 1u, &fzx_native_view, &fzx_source_backdrop, &first))
        fail(23, "CLOUDS", fzx_clouds_rbg.fault);
    if (!fzx_skyline_native(&commands[CMD_FIRST + first], FZX_SLAVE_ROAD_RESERVE - 1u - first, &fzx_native_view, skyline_vram, &n))
        fail(24, "SKYLINE DRAW", fzx_skyline_stats.fault);
    first += n; n = 0;
    if (!fzx_floor_native(&commands[CMD_FIRST + first], FZX_SLAVE_ROAD_RESERVE - 1u - first, &fzx_native_view, &fzx_source_backdrop, &n))
        fail(25, "FLOOR", fzx_floor_rbg.fault);
    first += n;
    if (first >= FZX_SLAVE_ROAD_RESERVE) fail(26, "PRELUDE OVERFLOW", first);
    vdp1_cmdt_t *jump = &commands[CMD_FIRST + first];
    memset(jump, 0, sizeof(*jump));
    vdp1_cmdt_jump_skip_assign(jump, CMD_FIRST + FZX_SLAVE_ROAD_RESERVE);
    row->master_ticks = (uint16_t)(cpu_frt_count_get() - t0);

    const uint16_t t1 = cpu_frt_count_get();
    if (!fzx_slave_road_finish(&video_fields, &road_count))
        fail(27, "TRACK", fzx_road_stream_stats.fault * 100u + fzx_slave_road_status);
    row->wait_road_ticks = (uint16_t)(cpu_frt_count_get() - t1);
#if FZX_ROAD_STREAM_PHASES
    for (unsigned i = 0; i < FZX_RSPC_COUNTERS; ++i)
        fzx_e0_rsp_counts[i] += ((volatile uint16_t *)((uintptr_t)fzx_rsp.counts | CPU_CACHE_THROUGH))[i];
#endif

    const uint32_t draws = FZX_SLAVE_ROAD_RESERVE + road_count;
    vdp1_cmdt_t *end = &commands[CMD_FIRST + draws];
    memset(end, 0, sizeof(*end));
    vdp1_cmdt_end_set(end);
    command_list.count = (uint16_t)(draws + 3u);

    row->prelude = (uint16_t)first; row->road = (uint16_t)road_count;
    const uint32_t groups = SLAVE32(fzx_source_road_plan.count), visible = SLAVE32(fzx_source_road_plan.visible_chunks);
    row->groups = (uint16_t)groups;
    row->pick_ticks = (uint16_t)SLAVE32(fzx_e0_pick[4]); /* slave timer units (divider 128) */
    row->build_ticks = (uint16_t)(cpu_frt_count_get() - kicked); /* slave kick to list ready */
    if (groups > fzx_e0_report[8]) fzx_e0_report[8] = groups;
    if (road_count > fzx_e0_report[9]) fzx_e0_report[9] = road_count;
    if (first > fzx_e0_report[10]) fzx_e0_report[10] = first;
    if (visible > fzx_e0_report[15]) fzx_e0_report[15] = visible;
}

int main(void) {
    cpu_intc_mask_set(0);
    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    fzx_e0_report[0] = ROW_MAGIC; /* the image is running: captures wait for this before reading status */
    fzx_sky_stack_paint(); /* master stack watermark, read by captures */

    /* The path pack: CD to low work RAM, then validated in place. */
    const uintptr_t scratch = ((uintptr_t)sat_stub_data_end + 31u) & ~(uintptr_t)31u;
    if (scratch != PATH_BASE + PATH_CAPACITY) fail(16, "LOW RAM LAYOUT", (uint32_t)scratch);
    for (unsigned i = 0; i < 8u; ++i) ((volatile uint32_t *)(PATH_BASE + PATH_CAPACITY))[(int)i - 8] = 0x45504744u;
    if (!fzx_texture_cd_load("ENGPATH.PAK", FZX_ENGINE_PATH_BYTES, FZX_ENGINE_PATH_FILE_HASH, (void *)PATH_BASE,
                             (void *)scratch, 0x00300000u - scratch))
        fail(17, "PATH LOAD", fzx_texture_cd_status.status);
    if (!fzx_engine_path_open(&path, (const void *)PATH_BASE, FZX_ENGINE_PATH_BYTES, FZX_ENGINE_PATH_HASH))
        fail(18, "PATH PACK", 0);

    vdp1_vram_partitions_get(&partitions);
    uint32_t offset = FZX_SKY_VRAM_BYTES;
    skyline_vram = (void *)((uintptr_t)partitions.texture_base + offset);
    if (offset + FZX_SKYLINE_VRAM_BYTES > partitions.texture_size || !fzx_skyline_load(skyline_vram)) fail(30, "SKYLINE LOAD", 0);
    offset += FZX_SKYLINE_VRAM_BYTES;
    /* The cloud plane bank is only the VDP2 cloud load's source: it is staged
     * where the track textures go next, and the track overwrites it. */
    planes_vram = (void *)((uintptr_t)partitions.texture_base + offset);
    if (offset + FZX_PLANE_RESIDENT_BYTES + FZX_PLANE_SHADE_BYTES > partitions.texture_size || !fzx_planes_load(planes_vram))
        fail(31, "CLOUD TEXTURE LOAD", fzx_plane_status);
    if (!fzx_floor_load()) fail(32, "FLOOR LOAD", fzx_floor_status);
    if (!fzx_clouds_vdp2_load(planes_vram)) fail(33, "CLOUDS LOAD", fzx_clouds_status);
    planes_vram = NULL;
    commands_init();
    dbgio_printf("\x1b[H\x1b[2J LOADING TRACK...\n");
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
    if (!fzx_road_load(fzx_course_record, sizeof(fzx_course_record)))
        fail(34, fzx_road_load_status.status == 2 ? "4 MB RAM CARTRIDGE REQUIRED" : "TRACK PACK LOAD", fzx_road_load_status.status);
    road_pixels = (void *)((uintptr_t)partitions.texture_base + offset);
    road_shades[0] = (void *)((uintptr_t)road_pixels + FZX_ROAD_LOD_TEXTURE_BYTES);
    if (offset + FZX_ROAD_LOD_TEXTURE_BYTES + FZX_ROAD_STREAM_SHADE_BYTES > partitions.texture_size || !fzx_road_stream_load(road_pixels))
        fail(35, "TRACK LOAD", fzx_road_stream_status);
    offset += FZX_ROAD_LOD_TEXTURE_BYTES + FZX_ROAD_STREAM_SHADE_BYTES;
    fzx_slave_road_init();
    if (fzx_slave_road_status != 1) fail(36, "SLAVE INIT", fzx_slave_road_status);
#if FZX_VDP1_DBANK
    dbank_init();
#endif
    /* Frame rows follow the track workspace in low work RAM, guarded. */
    {
        uintptr_t base = ((uintptr_t)fzx_road_stream_low_end + 32u + 63u) & ~(uintptr_t)63u;
        if (base < PATH_BASE + PATH_CAPACITY || base > 0x002f0000u) fail(37, "ROW AREA", (uint32_t)base);
        rows = (row_t *)base;
        row_capacity = (uint32_t)((0x00300000u - 64u - base) / sizeof(row_t));
        memset(rows, 0, row_capacity * sizeof(row_t));
        for (unsigned i = 0; i < 8u; ++i) ((volatile uint32_t *)(0x00300000u - 32u))[i] = 0x45504744u;
        fzx_e0_report[2] = row_capacity; fzx_e0_report[3] = (uint32_t)base;
    }
    fzx_e0_report[6] = vdp2_tvmd_tv_standard_get() == VDP2_TVMD_TV_STANDARD_PAL ? 50u : 60u;
    const uint32_t hz = fzx_e0_report[6];
    cpu_frt_init(CPU_FRT_CLOCK_DIV_32);
    /* Chain the end-of-draw stamp ahead of Yaul's handler. */
    {
        const uint32_t mask = cpu_intc_mask_get();
        cpu_intc_mask_set(15);
        fzx_e0_draw_end_next = (void (*)(void))scu_ic_ihr_get(SCU_IC_INTERRUPT_SPRITE_END);
        if (!fzx_e0_draw_end_next) fail(38, "NO END-OF-DRAW HANDLER", 0);
        scu_ic_ihr_set(SCU_IC_INTERRUPT_SPRITE_END, fzx_e0_draw_end);
        /* Yaul leaves the end-of-draw interrupt masked; unmask it for the stamp. */
        scu_ic_mask_chg(SCU_IC_MASK_ALL & ~SCU_IC_MASK_SPRITE_END, SCU_IC_MASK_NONE);
        cpu_intc_mask_set(mask);
    }
    dbgio_printf("\x1b[H\x1b[2J");
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
    /* VRAM use for the memory report: texture partition bytes used by E0. */
    fzx_e0_report[7] = offset;

    uint32_t tick = 0, remainder = 0, previous = video_fields, row_index = 0;
    fzx_e0_report[4] = previous;
    fzx_e0_status = 1;
    for (;;) {
        const uint16_t loop_start = cpu_frt_count_get();
        const uint32_t field = video_fields;
        /* Real time: the path advances one tick per 1/60 s, whatever the frame takes. */
        remainder += (field - previous) * 60u;
        previous = field;
        uint32_t advance = remainder / hz;
        remainder -= advance * hz;
        if (!tick) advance = 1;
        tick += advance;
        if (tick >= path.ticks) { tick = path.ticks; fzx_e0_done = 1; fzx_e0_status = 2; }
        const uint32_t hold = HOLD_TICK;
        const uint32_t shown = hold && hold <= path.ticks ? hold : tick;
        fzx_input_count = shown;

        row_t scratch_row, *row = row_index < row_capacity && !fzx_e0_done ? &rows[row_index] : &scratch_row;
        memset(row, 0, sizeof(*row));
        row->tick = shown; row->build_field = field;
        const uint16_t w0 = cpu_frt_count_get();
        before_slave();
        const uint16_t kicked = cpu_frt_count_get();
        kick(&path.cams[shown - 1u], shown);
        const uint16_t w1 = cpu_frt_count_get();
        before_master();
        row->wait_prev_ticks = (uint16_t)((uint16_t)(kicked - w0) + (uint16_t)(cpu_frt_count_get() - w1));
        build(row, &path.cams[shown - 1u], shown, kicked);
#if FZX_VDP1_DBANK
        {
            /* The previous frame must be drawn and swapped before this one is sent. */
            const uint16_t w2 = cpu_frt_count_get();
            vdp1_sync_wait();
            vdp2_sync_wait();
            row->wait_prev_ticks = (uint16_t)(row->wait_prev_ticks + (uint16_t)(cpu_frt_count_get() - w2));
        }
#endif
        row->slave_ticks = (uint16_t)(fzx_slave_road_busy_sum - fzx_e0_report[11]); /* this frame's slave job */
        fzx_e0_report[11] = fzx_slave_road_busy_sum;
        if (row != &scratch_row) { pending_row = row_index + 1u; ++row_index; fzx_e0_report[1] = row_index; }
        else pending_row = 0;
        row->submit_frt = cpu_frt_count_get();
        vdp1_sync_cmdt_list_put(&command_list, 0);
        vdp1_sync_render();
        vdp1_sync();
        vdp2_sync();
#if FZX_VDP1_DBANK
        sh2z_vdp1_banks_submitted(&fzx_dbank_banks, true);
#endif
        ++fzx_e0_report[5];
        fzx_e0_report[12] = field_frt;
        (void)loop_start;
    }
}
