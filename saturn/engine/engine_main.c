/* Engine line, milestones E0 and E1 (docs/ENGINE_PLAN.md): track flythrough,
 * then the drive slice.
 *
 * A separate Saturn image with its own main loop. It links no original game
 * code. E0: the camera rides a baked path (ENGPATH.PAK). E1 (FZX_E1_DRIVE=1):
 * native physics moves the player, the chase camera follows it, the player is a
 * rotation sprite and a basic HUD shows speed, lap and time (engine_drive.c).
 * The engine derives the view, the backdrop, the skyline and the track pick
 * from the camera in fixed point.
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
#ifndef FZX_E1_DRIVE
#define FZX_E1_DRIVE 0
#endif
#if FZX_E1_DRIVE
#include "engine_drive.h"
#endif
#ifndef E1_CLOSEUP
#define E1_CLOSEUP 0
#endif
#ifndef ENG_SCENES
#define ENG_SCENES 0
#endif
#if ENG_SCENES
#include "engine_scenes.h"
#include "engine_load_fast.h"
#include "engine_path_ranges.h"
/* Load step stamps for the scenes lane's load-time table (fields, CD sectors and CD time). */
#define SCN_STAMP(i) fzx_scn_stamp(i)
/* One-time set-up, once per image (a cancelled load may stop between them). */
#define SCN_ONCE(stmt) do { static bool once_; if (!once_) { stmt; once_ = true; } } while (0)
#else
#define SCN_STAMP(i) ((void)0)
#define SCN_ONCE(stmt) stmt
#endif
#ifndef E1_CLOSEUP_EARLY
#define E1_CLOSEUP_EARLY 0
#endif
#if E1_CLOSEUP == 1
#include "engine_closeup.h"
#define CLOSEUP_COMMANDS FZX_CLOSEUP_SLOTS
#else
#define CLOSEUP_COMMANDS 0u
#endif
#if FZX_ROAD_STREAM_PHASES
/* Diagnostic builds only: the track build's phase totals (slave timer units). */
fzx_rsp_state_t fzx_rsp;
volatile uint32_t fzx_scp_road[FZX_RSP_PHASES], fzx_scp_road_calls;
#include "fzx_scene_phase.h"
/* and the master's floor and cloud table phases (master timer units). */
volatile uint32_t fzx_scp_ticks[FZX_SCP_LAYERS][FZX_SCP_PHASES], fzx_scp_calls[FZX_SCP_LAYERS];
/* and the track build's per-frame counters, summed (quad clips, piece clips, pieces, ...). */
volatile uint32_t fzx_e0_rsp_counts[FZX_RSPC_COUNTERS];
/* and one record per frame row (diagnostic builds only, in the free high work
 * RAM): the 16 phase times (slave timer units, saturated), the 19 counters,
 * then quads, chunks, positions, culled, clipped, tiers 0..3 and commands. */
enum { E0_PROW_ROWS = 4608u, E0_PROW_WORDS = 45u };
uint16_t fzx_e0_prow[E0_PROW_ROWS][E0_PROW_WORDS];
static void prow_fill(uint32_t index) {
    if (index >= E0_PROW_ROWS) return;
    uint16_t *r = fzx_e0_prow[index];
    const volatile uint32_t *t = (const volatile uint32_t *)((uintptr_t)fzx_rsp.ticks | CPU_CACHE_THROUGH);
    const volatile uint16_t *c = (const volatile uint16_t *)((uintptr_t)fzx_rsp.counts | CPU_CACHE_THROUGH);
    const volatile fzx_road_stream_draw_stats_t *st =
        (const volatile fzx_road_stream_draw_stats_t *)((uintptr_t)&fzx_road_stream_stats | CPU_CACHE_THROUGH);
    for (unsigned i = 0; i < FZX_RSP_PHASES; ++i) r[i] = (uint16_t)(t[i] > 65535u ? 65535u : t[i]);
    for (unsigned i = 0; i < FZX_RSPC_COUNTERS; ++i) r[FZX_RSP_PHASES + i] = c[i];
    uint16_t *x = r + FZX_RSP_PHASES + FZX_RSPC_COUNTERS;
    x[0] = (uint16_t)st->quads; x[1] = (uint16_t)st->chunks; x[2] = (uint16_t)st->positions;
    x[3] = (uint16_t)st->culled; x[4] = (uint16_t)st->clipped;
    for (unsigned i = 0; i < 4u; ++i) x[5 + i] = (uint16_t)st->tiers[i];
    x[9] = (uint16_t)st->commands;
}
_Static_assert(FZX_RSP_PHASES + FZX_RSPC_COUNTERS + 10u == E0_PROW_WORDS, "per-frame record layout");
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
    /* E1: user clip, shadow and sprite for the player after the track
     * (close-up arm A: and the 3D model's commands). */
    CLOSEUP_SLOTS = CLOSEUP_COMMANDS,
    DRIVE_SLOTS = FZX_E1_DRIVE ? 3u + CLOSEUP_SLOTS : 0u,
    CMD_CAPACITY = SCENE_CAPACITY + 3u + DRIVE_SLOTS,
    PATH_BASE = 0x00200000u, PATH_CAPACITY = 0x0008C000u, /* Makefile: sat_stub_data_end follows */
    ROW_MAGIC = 0x45305246u, /* "E0RF" */
    ROW_WORDS = 9u + (FZX_E1_DRIVE ? 3u : 0u)
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
#ifndef E0_CHUNK_CAP
#define E0_CHUNK_CAP 0
#endif
#if E0_CHUNK_CAP
volatile uint32_t fzx_e0_cap_runs; /* runs dropped by E0_CHUNK_CAP */
#endif
/* Floor refusal: queue front, presented tick and commits before and after the
 * refused call, floor status, build bank (bit 8: VDP1 busy), field, prelude. */
volatile uint32_t fzx_e0_floor_diag[11]; /* [10]: refused frames skipped (E1) */
#if FZX_E1_DRIVE
#include "fzx_floor_rbg.h"
/* Floor fault probe (E1): the first refused frame's inputs, then the same prepare
 * run again as is and with every interrupt masked. [0] refusals seen, [1] tick,
 * [2] retry ok, [3] retry fault, [4] masked ok, [5] masked fault, [6] retry ok
 * count over all refusals, [7] masked ok count. */
volatile uint32_t fzx_e1_floor_probe[8];
fzx_engine_cam_t fzx_e1_floor_cam;
fzx_view_t fzx_e1_floor_view;
RefBackdrop fzx_e1_floor_backdrop;
static fzx_floor_rbg_t probe_out;
static bool probe_emit(void *context, const fzx_floor_polygon_t *q) { (void)q; ++*(uint32_t *)context; return true; }
static void floor_probe(const fzx_engine_cam_t *cam, uint32_t tick) {
    uint32_t emitted = 0;
    const bool first = fzx_e1_floor_probe[0]++ == 0u;
    if (first) {
        fzx_e1_floor_cam = *cam; fzx_e1_floor_view = fzx_native_view; fzx_e1_floor_backdrop = fzx_source_backdrop;
        fzx_e1_floor_probe[1] = tick;
    }
    memset(&probe_out, 0, sizeof(probe_out));
    const bool ok = fzx_floor_rbg_prepare(&fzx_native_view, &fzx_source_backdrop, &probe_out, probe_emit, &emitted);
    if (first) { fzx_e1_floor_probe[2] = ok; fzx_e1_floor_probe[3] = probe_out.fault; }
    fzx_e1_floor_probe[6] += ok ? 1u : 0u;
    const uint32_t mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    memset(&probe_out, 0, sizeof(probe_out));
    const bool okm = fzx_floor_rbg_prepare(&fzx_native_view, &fzx_source_backdrop, &probe_out, probe_emit, &emitted);
    cpu_intc_mask_set(mask);
    if (first) { fzx_e1_floor_probe[4] = okm; fzx_e1_floor_probe[5] = probe_out.fault; }
    fzx_e1_floor_probe[7] += okm ? 1u : 0u;
}
#endif
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
#if FZX_E1_DRIVE
    uint16_t phys_ticks;   /* master FRT units: physics steps of this frame */
    uint16_t sprite_ticks; /* master FRT units: player sprite pose and emit */
    uint16_t steps;        /* physics ticks stepped for this frame */
    uint16_t hud_ticks;    /* master FRT units: HUD text */
    int16_t sprite_x, sprite_y; /* player sprite origin, whole pixels (-32768: not drawn) */
#endif
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

#ifndef E0_HW_COUNTER
#define E0_HW_COUNTER 0
#endif
#if E0_HW_COUNTER
/* E0_HW_COUNTER (default 0; the owner's hardware disc only): count every newly
 * presented frame and the field gap since the previous one, from the first
 * flythrough frame, for an on-screen readout a TV can show. gaps[k]: gaps of
 * k+1 fields, gaps[3]: four or more. */
volatile uint32_t fzx_hw_frames, fzx_hw_first_field, fzx_hw_last_field, fzx_hw_worst, fzx_hw_over2, fzx_hw_gaps[4];
static void hw_count(void) {
    const uint32_t now = video_fields;
    if (fzx_hw_frames) {
        const uint32_t gap = now - fzx_hw_last_field;
        if (gap > fzx_hw_worst) fzx_hw_worst = gap;
        if (gap > 2u) ++fzx_hw_over2;
        if (gap) ++fzx_hw_gaps[gap > 4u ? 3u : gap - 1u];
    } else fzx_hw_first_field = now;
    fzx_hw_last_field = now;
    ++fzx_hw_frames;
}
#endif

/* The floor's render-end callback calls this when a frame has been drawn. */
void fzx_fp_present(void) {
#if E0_HW_COUNTER
    hw_count();
#endif
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

/* Interrupt callbacks and the multiply registers (E1 floor fault, 2026-11-19).
 * The BIOS SCU interrupt dispatcher (0x06000840..0x0600094A) saves r0-r7, PR and
 * GBR but not MACH/MACL, and Yaul's SCU handlers call our VBlank callbacks as plain
 * functions. A callback that multiplies (fzx_fp_present: row index x 44, mul.l)
 * overwrites MACL under whatever master code the VBlank-In interrupt lands in; the
 * floor and view maths right after the field wait are the main victims (fault 2,
 * off-by-a-few quotients from libgcc's division). Every engine callback runs inside
 * these wrappers, which keep MACH/MACL. E0_ISR_MAC_SAVE=0 is the control. */
#ifndef E0_ISR_MAC_SAVE
#define E0_ISR_MAC_SAVE 1
#endif
#if E0_ISR_MAC_SAVE
#define MAC_SAVE() uint32_t mac_h_, mac_l_; __asm__ volatile("sts mach,%0\n\tsts macl,%1" : "=r"(mac_h_), "=r"(mac_l_))
#define MAC_RESTORE() __asm__ volatile("lds %0,mach\n\tlds %1,macl" : : "r"(mac_h_), "r"(mac_l_) : "mach", "macl")
#else
#define MAC_SAVE() do {} while (0)
#define MAC_RESTORE() do {} while (0)
#endif
static void render_end(void *work) {
    MAC_SAVE();
    fzx_floor_present(work);
    MAC_RESTORE();
}
/* Control probe (E0_MAC_PROBE=1, diagnostic only): for up to one field before the
 * VBlank wait, check 32x32->64 products for a changed MACH/MACL. [0] products,
 * [1] wrong. Costs a field per frame. */
#ifndef E0_MAC_PROBE
#define E0_MAC_PROBE 0
#endif
volatile uint32_t fzx_e0_mac_probe[2];
#if E0_MAC_PROBE
static uint32_t probe_seed = 0x2545f491u;
static void mac_probe(void) {
    const uint16_t t0 = cpu_frt_count_get();
    uint32_t n = 0, bad = 0;
    while ((uint16_t)(cpu_frt_count_get() - t0) < field_frt) {
        probe_seed = probe_seed * 1664525u + 1013904223u;
        const int32_t a = (int32_t)probe_seed, b = (int32_t)(probe_seed * 2654435761u);
        int64_t p, q;
        /* A wide window between the multiply and the reads, then a reference. */
        __asm__ volatile("dmuls.l %1,%2\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tsts mach,%S0\n\tsts macl,%R0"
                         : "=&r"(p) : "r"(a), "r"(b) : "mach", "macl");
        __asm__ volatile("dmuls.l %1,%2\n\tsts mach,%S0\n\tsts macl,%R0" : "=&r"(q) : "r"(a), "r"(b) : "mach", "macl");
        ++n; if (p != q) ++bad;
    }
    fzx_e0_mac_probe[0] += n; fzx_e0_mac_probe[1] += bad;
}
#endif

static void vblank_out(void *work) {
    (void)work;
    MAC_SAVE();
    const uint16_t now = cpu_frt_count_get();
    field_frt = (uint16_t)(now - vblank_frt);
    vblank_frt = now;
    ++video_fields;
#if FZX_E1_DRIVE
    smpc_peripheral_intback_issue();
#endif
#if ENG_SCENES
    if (fzx_scn_vblank_hook) fzx_scn_vblank_hook();
#endif
    MAC_RESTORE();
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

/* The slave's job after the track build (slave_road.c, engine line). */
void fzx_engine_slave_after(void) {
#if FZX_E1_DRIVE
    fzx_drive_slave_fill();
#endif
}

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
    uint32_t first_run = 0;
#if E0_CHUNK_CAP
    /* E0_CHUNK_CAP (default 0, off): when more than this many chunks are
     * marked, drop whole runs, farthest first (the pick's order), keeping the
     * nearest run. Dropped track is the most distant, inside the fog. */
    while (marked > E0_CHUNK_CAP && runs - first_run > 1u) {
        const sh2z_segmesh_run_t *f = &pick_runs[first_run];
        const uint32_t n = path.chunks, span = f->start == f->end ? n : (uint32_t)((f->end + n - f->start) % n);
        marked -= span < marked ? span : marked; ++first_run; ++fzx_e0_cap_runs;
    }
    out->visible_chunks = marked;
#endif
    out->count = runs - first_run;
    for (uint32_t i = 0; i < runs - first_run; ++i) {
        RefRoadGroup *g = &out->groups[i];
        const sh2z_segmesh_run_t *pr = &pick_runs[first_run + i];
        g->start = pr->start; g->end = pr->end; g->direction = pr->direction;
        g->average_depth = (float)pr->average / (float)(1u << pv.depth_shift);
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

/* Banked floor and cloud tables: each build queues one stage per layer and the
 * VBlank-In present pops it. A frame's present can still be due when the frame
 * after next is built (the draw ended, the VBlank-In has not come yet); both
 * queue slots are then full and the layer refuses the build (E1 floor fault,
 * status 1: "queue not free"). Wait for that present, at most three fields.
 * [0] frames that waited, [1] longest wait (FRT ticks), [2] waits that timed out. */
#ifndef E0_PRESENT_WAIT
#define E0_PRESENT_WAIT 1 /* 0: the control that refuses as before */
#endif
static uint32_t floor_pushes, clouds_pushes;
volatile uint32_t fzx_e0_present_wait[3];
#if FZX_VDP1_DBANK && E0_PRESENT_WAIT
static void present_wait(void) {
    if (floor_pushes - fzx_floor_commits < 2u && clouds_pushes - fzx_clouds_commits < 2u) return;
    const uint16_t t0 = cpu_frt_count_get();
    const uint32_t f0 = video_fields;
    while (floor_pushes - fzx_floor_commits >= 2u || clouds_pushes - fzx_clouds_commits >= 2u)
        if (video_fields - f0 > 3u) { ++fzx_e0_present_wait[2]; break; }
    const uint32_t t = (uint16_t)(cpu_frt_count_get() - t0);
    ++fzx_e0_present_wait[0];
    if (t > fzx_e0_present_wait[1]) fzx_e0_present_wait[1] = t;
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

/* Low work RAM sentinels: eight words after the path pack (0x0028bfe0) and
 * eight after the frame rows (0x002fffe0), checked every frame. */
#define LOW_SENTINEL 0x45504744u
#ifndef E0_SENTINEL_CONTROL
#define E0_SENTINEL_CONTROL 0
#endif
static bool low_sentinels(void) {
    const volatile uint32_t *a = (const volatile uint32_t *)(PATH_BASE + PATH_CAPACITY - 32u);
    const volatile uint32_t *b = (const volatile uint32_t *)(0x00300000u - 32u);
    for (unsigned i = 0; i < 8u; ++i) if (a[i] != LOW_SENTINEL || b[i] != LOW_SENTINEL) return false;
    return true;
}
#ifndef E0_VDP2_MAP
#define E0_VDP2_MAP 0
#endif
#if E0_VDP2_MAP
/* Diagnostic (E0_VDP2_MAP=1): the VDP2 register shadow and CRAM as set up for
 * the flythrough, for the allocation map (tools: vdp2map.py). */
volatile uint16_t fzx_e0_vdp2_regs[0x120 / 2], fzx_e0_cram[0x1000 / 2];
#endif

static void before_master(void) {
#if E0_MAC_PROBE
    mac_probe();
#endif
#if FZX_VDP1_DBANK
    vdp2_sync_wait();
    const uint32_t bank = sh2z_vdp1_banks_bank(&fzx_dbank_banks);
    fzx_floor_dbank_select(bank);
    fzx_clouds_dbank_select(bank);
    if (!dbank_guards()) { fzx_dbank_status = 3; fail(42, "DBANK GUARD", 0); }
    if (!low_sentinels()) fail(44, "LOW RAM SENTINEL", 0);
    /* A build that would write the bank VDP1 is still drawing must never run. */
    if (fzx_dbank_banks.conflicts) { fzx_dbank_status = 4; fail(43, "DBANK STALE BANK", fzx_dbank_banks.conflicts); }
#else
    vdp2_sync_wait();
    if (!low_sentinels()) fail(44, "LOW RAM SENTINEL", 0);
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
#if FZX_VDP1_DBANK && E0_PRESENT_WAIT
    present_wait();
#endif
    if (!fzx_clouds_sky_lines(&fzx_native_view, &fzx_source_backdrop)) fail(22, "SKY LINES", fzx_sky_stats.fault);
    if (!fzx_clouds_native(&commands[CMD_FIRST], FZX_SLAVE_ROAD_RESERVE - 1u, &fzx_native_view, &fzx_source_backdrop, &first))
        fail(23, "CLOUDS", fzx_clouds_rbg.fault);
    ++clouds_pushes;
    if (!fzx_skyline_native(&commands[CMD_FIRST + first], FZX_SLAVE_ROAD_RESERVE - 1u - first, &fzx_native_view, skyline_vram, &n))
        fail(24, "SKYLINE DRAW", fzx_skyline_stats.fault);
    first += n; n = 0;
    {
        /* Floor refusal diagnostics: queue front and presents before the call. */
        const uint32_t pending = fzx_floor_pending, presented = fzx_floor_presented, commits = fzx_floor_commits;
        if (!fzx_floor_native(&commands[CMD_FIRST + first], FZX_SLAVE_ROAD_RESERVE - 1u - first, &fzx_native_view, &fzx_source_backdrop, &n)) {
            fzx_e0_floor_diag[0] = pending; fzx_e0_floor_diag[1] = presented; fzx_e0_floor_diag[2] = commits;
            fzx_e0_floor_diag[3] = fzx_floor_pending; fzx_e0_floor_diag[4] = fzx_floor_presented;
            fzx_e0_floor_diag[5] = fzx_floor_commits; fzx_e0_floor_diag[6] = fzx_floor_status;
#if FZX_VDP1_DBANK
            fzx_e0_floor_diag[7] = sh2z_vdp1_banks_bank(&fzx_dbank_banks) | (vdp1_sync_busy() ? 0x100u : 0u);
#endif
            fzx_e0_floor_diag[8] = video_fields; fzx_e0_floor_diag[9] = first;
            ++fzx_e0_floor_diag[10];
#if FZX_E1_DRIVE
            floor_probe(cam, tick);
#endif
            fail(25, "FLOOR", fzx_floor_rbg.fault);
        }
    }
    ++floor_pushes;
    first += n;
    if (first >= FZX_SLAVE_ROAD_RESERVE) fail(26, "PRELUDE OVERFLOW", first);
    vdp1_cmdt_t *jump = &commands[CMD_FIRST + first];
    memset(jump, 0, sizeof(*jump));
    vdp1_cmdt_jump_skip_assign(jump, CMD_FIRST + FZX_SLAVE_ROAD_RESERVE);
    row->master_ticks = (uint16_t)(cpu_frt_count_get() - t0);
#if E1_CLOSEUP == 1
#if FZX_VDP1_DBANK
    const uint32_t vbank = sh2z_vdp1_banks_bank(&fzx_dbank_banks);
#else
    const uint32_t vbank = 0;
#endif
#if E1_CLOSEUP_EARLY
    /* Arm A, early: the vehicle is built while the slave still builds the track (its
     * Gouraud words go to this build's bank, which VDP1 is not drawing); only the
     * copy of its commands after the shadow waits for the track. */
    static vdp1_cmdt_t vehicle_stage[FZX_CLOSEUP_SLOTS];
    const uint16_t v0 = cpu_frt_count_get();
    const unsigned vehicle_staged = fzx_closeup_draw(vehicle_stage, FZX_CLOSEUP_SLOTS, &fzx_native_view, cam, vbank);
    const uint16_t vehicle_ticks = (uint16_t)(cpu_frt_count_get() - v0);
#endif
#endif

    const uint16_t t1 = cpu_frt_count_get();
    if (!fzx_slave_road_finish(&video_fields, &road_count))
        fail(27, "TRACK", fzx_road_stream_stats.fault * 100u + fzx_slave_road_status);
    row->wait_road_ticks = (uint16_t)(cpu_frt_count_get() - t1);
#if FZX_ROAD_STREAM_PHASES
    for (unsigned i = 0; i < FZX_RSPC_COUNTERS; ++i)
        fzx_e0_rsp_counts[i] += ((volatile uint16_t *)((uintptr_t)fzx_rsp.counts | CPU_CACHE_THROUGH))[i];
#endif

    uint32_t draws = FZX_SLAVE_ROAD_RESERVE + road_count;
#if FZX_E1_DRIVE
    {
        const uint16_t s0 = cpu_frt_count_get();
        draws += fzx_drive_sprites(&commands[CMD_FIRST + draws], DRIVE_SLOTS - CLOSEUP_SLOTS, &fzx_native_view, cam);
#if E1_CLOSEUP == 1 && E1_CLOSEUP_EARLY
        /* Arm A, early: the staged vehicle after its shadow. */
        memcpy(&commands[CMD_FIRST + draws], vehicle_stage, vehicle_staged * sizeof(vdp1_cmdt_t));
        draws += vehicle_staged;
#elif E1_CLOSEUP == 1
        /* Arm A: the 3D model after its shadow, Gouraud words in this build's bank. */
        draws += fzx_closeup_draw(&commands[CMD_FIRST + draws], CLOSEUP_SLOTS, &fzx_native_view, cam, vbank);
#endif
        row->sprite_x = (int16_t)fzx_e1_sprite_pos[0]; row->sprite_y = (int16_t)fzx_e1_sprite_pos[1];
        row->sprite_ticks = (uint16_t)(cpu_frt_count_get() - s0);
#if E1_CLOSEUP == 1 && E1_CLOSEUP_EARLY
        /* Master time of the player's draw, wherever it ran (the vehicle ran before the wait). */
        row->sprite_ticks = (uint16_t)(row->sprite_ticks + vehicle_ticks);
#endif
    }
#endif
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
#if FZX_E1_DRIVE
    /* The HUD updates every frame: the synchronous device writes VDP2 VRAM at
     * flush. (The asynchronous one stopped taking updates mid-race in Ymir.) */
    dbgio_dev_default_init(DBGIO_DEV_VDP2);
#else
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
#endif
    dbgio_dev_font_load();
    fzx_e0_report[0] = ROW_MAGIC; /* the image is running: captures wait for this before reading status */
    fzx_sky_stack_paint(); /* master stack watermark, read by captures */
#if ENG_SCENES
    fzx_scn_main(); /* never returns: the boot-to-reset loop (engine_scenes.c) */
}

uint32_t video_fields_get(void) { return video_fields; }
/* The race's last command list (prelude ... END) and the buffer's capacity: RESULTS draws
 * it again behind its overlay (SCENES.md section 8, the retained backdrop). */
vdp1_cmdt_t *fzx_race_list(unsigned *count, unsigned *capacity) {
    *count = command_list.count; *capacity = CMD_CAPACITY;
    return commands_buf;
}
/* The race's VDP1 render callback (the floor's present) runs only while RACE draws. */
void fzx_race_render_callback(bool on) { vdp1_sync_render_set(on ? render_end : NULL, NULL); }

static fzx_engine_cam_t drive_cam;
static const fzx_engine_cam_t *scn_lens;
static uint32_t scn_hz;
static uint32_t tick, remainder, previous, row_index;

/* The race load: the plain image's start-up, shared text. Faults stop the image as before. */
void fzx_race_load(void) {
    SCN_STAMP(0);
#endif

    /* The path pack: CD to low work RAM, then validated in place. */
    const uintptr_t scratch = ((uintptr_t)sat_stub_data_end + 31u) & ~(uintptr_t)31u;
    if (scratch != PATH_BASE + PATH_CAPACITY) fail(16, "LOW RAM LAYOUT", (uint32_t)scratch);
    for (unsigned i = 0; i < 8u; ++i) ((volatile uint32_t *)(PATH_BASE + PATH_CAPACITY))[(int)i - 8] = LOW_SENTINEL;
#if ENG_SCENES
    /* The drive reads only the header and anchors and the lens record (engine_path_ranges.h:
     * about 2 KB of the pack), each checked as it arrives. */
    static fzx_engine_cam_t lens_record __attribute__((aligned(4)));
    if (!fzx_texture_cd_load_range("ENGPATH.PAK", FZX_ENGINE_PATH_BYTES, 0, FZX_EPATH_HEAD_BYTES, FZX_EPATH_HEAD_HASH,
                                   (void *)PATH_BASE, (void *)scratch, 0x00300000u - scratch) ||
        !fzx_texture_cd_load_range("ENGPATH.PAK", FZX_ENGINE_PATH_BYTES, FZX_EPATH_LENS_AT, FZX_EPATH_LENS_BYTES,
                                   FZX_EPATH_LENS_HASH, &lens_record, (void *)scratch, 0x00300000u - scratch))
        fail(17, "PATH LOAD", fzx_texture_cd_status.status);
    if (!fzx_engine_path_open_head(&path, (const void *)PATH_BASE, FZX_EPATH_HEAD_BYTES, FZX_ENGINE_PATH_BYTES,
                                   FZX_ENGINE_PATH_HASH))
#else
    if (!fzx_texture_cd_load("ENGPATH.PAK", FZX_ENGINE_PATH_BYTES, FZX_ENGINE_PATH_FILE_HASH, (void *)PATH_BASE,
                             (void *)scratch, 0x00300000u - scratch))
        fail(17, "PATH LOAD", fzx_texture_cd_status.status);
    if (!fzx_engine_path_open(&path, (const void *)PATH_BASE, FZX_ENGINE_PATH_BYTES, FZX_ENGINE_PATH_HASH))
#endif
        fail(18, "PATH PACK", 0);

    vdp1_vram_partitions_get(&partitions);
#if FZX_E1_DRIVE
    /* Physics pack to work RAM (the sprite set follows the track and the banks). */
    if (!fzx_drive_load((void *)scratch, 0x00300000u - scratch))
        fail(50, "DRIVE LOAD", fzx_e1_status * 100u + fzx_texture_cd_status.status);
    /* The lens (focal, tan_half) of the race camera at speed, from the reference path. */
#if ENG_SCENES
    const fzx_engine_cam_t *const lens = &lens_record;
    _Static_assert(FZX_EPATH_LENS_BYTES == sizeof(fzx_engine_cam_t), "lens record size");
#else
    const fzx_engine_cam_t *const lens = &path.cams[(path.ticks >= 3000u ? 3000u : path.ticks) - 1u];
#endif
#if !ENG_SCENES
    static fzx_engine_cam_t drive_cam;
#endif
#endif
    SCN_STAMP(1);
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
    vdp1_sync_render_set(render_end, NULL); /* the floor's present, inside the MAC wrapper */
    if (!fzx_clouds_vdp2_load(planes_vram)) fail(33, "CLOUDS LOAD", fzx_clouds_status);
    planes_vram = NULL;
    commands_init();
    SCN_STAMP(2);
#if !ENG_SCENES
    dbgio_printf("\x1b[H\x1b[2J LOADING TRACK...\n");
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
#endif
    if (!fzx_road_load(fzx_course_record, sizeof(fzx_course_record)))
        fail(34, fzx_road_load_status.status == 2 ? "4 MB RAM CARTRIDGE REQUIRED" : "TRACK PACK LOAD", fzx_road_load_status.status);
    road_pixels = (void *)((uintptr_t)partitions.texture_base + offset);
    road_shades[0] = (void *)((uintptr_t)road_pixels + FZX_ROAD_LOD_TEXTURE_BYTES);
    if (offset + FZX_ROAD_LOD_TEXTURE_BYTES + FZX_ROAD_STREAM_SHADE_BYTES > partitions.texture_size || !fzx_road_stream_load(road_pixels))
        fail(35, "TRACK LOAD", fzx_road_stream_status);
    offset += FZX_ROAD_LOD_TEXTURE_BYTES + FZX_ROAD_STREAM_SHADE_BYTES;
    SCN_STAMP(3);
    SCN_ONCE(fzx_slave_road_init());
    if (fzx_slave_road_status != 1) fail(36, "SLAVE INIT", fzx_slave_road_status);
#if FZX_VDP1_DBANK
    SCN_ONCE(dbank_init());
#endif
#if FZX_E1_DRIVE
    {
        /* Sprite set to the cartridge after the track stream; CLUTs and shadow after
         * the bank stages; the slot pool at the top of the texture partition. The CD
         * scratch is the 4,096 B block at sat_stub_data_end. */
#if FZX_VDP1_DBANK
        const uint32_t clut_used = fzx_dbank_stage_bytes;
#else
        const uint32_t clut_used = 0;
#endif
        void *const cart_area = dram_cart_area_get();
        const uintptr_t cart = (uintptr_t)cart_area, cart_limit = cart + dram_cart_size_get();
        if (!fzx_drive_sprites_load(&partitions, clut_used, (uintptr_t)fzx_road_stream_cart_end + 32u, cart_limit,
                                    (void *)scratch, 0x0028D000u - 32u - scratch))
            fail(52, "SPRITE LOAD", fzx_e1_status * 100u + fzx_texture_cd_status.status);
        SCN_STAMP(4);
#if E1_CLOSEUP == 1
        /* Arm A: the vehicle's texels and two shade tables just below the slot pool. */
#if ENG_SCENES
        const uint32_t closeup_floor = fzx_closeup_load_family(&partitions, fzx_drive_texture_floor(), (void *)scratch,
                                                               0x0028D000u - 32u - scratch, fzx_scn_player_family());
#else
        const uint32_t closeup_floor = fzx_closeup_load(&partitions, fzx_drive_texture_floor(), (void *)scratch,
                                                        0x0028D000u - 32u - scratch);
#endif
        if (!closeup_floor) fail(53, "VEHICLE LOAD", fzx_ab_report[0] * 100u + fzx_texture_cd_status.status);
        if (offset > closeup_floor) fail(51, "SPRITE VRAM OVERLAP", offset);
        SCN_STAMP(5);
#endif
        if (offset > fzx_drive_texture_floor()) fail(51, "SPRITE VRAM OVERLAP", offset);
    }
#endif
    /* Frame rows follow the track workspace in low work RAM, guarded. */
    {
        uintptr_t base = ((uintptr_t)fzx_road_stream_low_end + 32u + 63u) & ~(uintptr_t)63u;
        if (base < PATH_BASE + PATH_CAPACITY || base > 0x002f0000u) fail(37, "ROW AREA", (uint32_t)base);
        rows = (row_t *)base;
        row_capacity = (uint32_t)((0x00300000u - 64u - base) / sizeof(row_t));
        memset(rows, 0, row_capacity * sizeof(row_t));
        for (unsigned i = 0; i < 8u; ++i) ((volatile uint32_t *)(0x00300000u - 32u))[i] = LOW_SENTINEL;
        fzx_e0_report[2] = row_capacity; fzx_e0_report[3] = (uint32_t)base;
    }
    fzx_e0_report[6] = vdp2_tvmd_tv_standard_get() == VDP2_TVMD_TV_STANDARD_PAL ? 50u : 60u;
    const uint32_t hz = fzx_e0_report[6];
    cpu_frt_init(CPU_FRT_CLOCK_DIV_32);
    /* Chain the end-of-draw stamp ahead of Yaul's handler. */
#if ENG_SCENES
    static bool chained;
    if (!chained && (chained = true))
#endif
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

#if E0_VDP2_MAP
    memcpy((void *)fzx_e0_vdp2_regs, vdp2_regs_get(), sizeof(fzx_e0_vdp2_regs));
    for (unsigned i = 0; i < 0x800u; ++i) fzx_e0_cram[i] = ((const volatile uint16_t *)VDP2_CRAM(0))[i];
#endif
#if ENG_SCENES
    scn_hz = hz; scn_lens = lens;
    SCN_STAMP(6);
}

/* Race-local state to its start: the loop counters and the status words. */
void fzx_race_begin(void) {
    tick = 0; remainder = 0; previous = video_fields; row_index = 0; pending_row = 0;
    fzx_e0_report[1] = 0; fzx_e0_report[4] = previous;
    fzx_e0_done = 0; fzx_e0_status = 1;
}

/* One presented frame of the race (the plain image's loop body, shared text). */
bool fzx_race_frame(void) {
    const uint32_t hz = scn_hz;
    const fzx_engine_cam_t *const lens = scn_lens;
    {
#else
    uint32_t tick = 0, remainder = 0, previous = video_fields, row_index = 0;
#if E0_HW_COUNTER
    uint32_t hw_loops = 0, hw_next = 0, hw_mark_frames = 0;
#endif
    fzx_e0_report[4] = previous;
    fzx_e0_status = 1;
    for (;;) {
#endif
        const uint16_t loop_start = cpu_frt_count_get();
        const uint32_t field = video_fields;
        /* Real time: the path advances one tick per 1/60 s, whatever the frame takes. */
        remainder += (field - previous) * 60u;
        previous = field;
        uint32_t advance = remainder / hz;
        remainder -= advance * hz;
        if (!tick) advance = 1;
#if FZX_E1_DRIVE
        /* Physics at 60 Hz real time (at most 4 ticks a frame); a debugger hold
         * freezes it once it reaches the held tick. */
        const uint32_t hold = HOLD_TICK;
        if (advance > 4u) advance = 4u;
        if (hold && fzx_drive_tick() + advance > hold) advance = fzx_drive_tick() < hold ? hold - fzx_drive_tick() : 0u;
        const uint16_t p0 = cpu_frt_count_get();
        fzx_drive_step(advance);
        const uint16_t phys = (uint16_t)(cpu_frt_count_get() - p0);
        tick = fzx_drive_tick() ? fzx_drive_tick() : 1u;
        if (fzx_e1_driver && fzx_e1_report[5]) { fzx_e0_done = 1; fzx_e0_status = 2; }
        const uint32_t shown = tick;
        fzx_drive_camera(&drive_cam, lens);
        const fzx_engine_cam_t *const cam = &drive_cam;
#else
        tick += advance;
#if E0_HW_COUNTER
        /* The hardware disc loops the flythrough for the soak. */
        if (tick >= path.ticks) { tick = 1; ++hw_loops; }
#else
        if (tick >= path.ticks) { tick = path.ticks; fzx_e0_done = 1; fzx_e0_status = 2; }
#endif
#if E0_SENTINEL_CONTROL
        /* Control: an overrun past the frame rows at tick 600 must stop the image. */
        if (tick >= 600u) ((volatile uint32_t *)(0x00300000u - 32u))[3] = 0;
#endif
        const uint32_t hold = HOLD_TICK;
        const uint32_t shown = hold && hold <= path.ticks ? hold : tick;
        const fzx_engine_cam_t *const cam = &path.cams[shown - 1u];
#endif
        fzx_input_count = shown;

        row_t scratch_row, *row = row_index < row_capacity && !fzx_e0_done ? &rows[row_index] : &scratch_row;
        memset(row, 0, sizeof(*row));
        row->tick = shown; row->build_field = field;
#if FZX_E1_DRIVE
        row->phys_ticks = phys; row->steps = (uint16_t)advance;
#endif
        const uint16_t w0 = cpu_frt_count_get();
        before_slave();
        const uint16_t kicked = cpu_frt_count_get();
        kick(cam, shown);
        const uint16_t w1 = cpu_frt_count_get();
        before_master();
        row->wait_prev_ticks = (uint16_t)((uint16_t)(kicked - w0) + (uint16_t)(cpu_frt_count_get() - w1));
        build(row, cam, shown, kicked);
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
#if FZX_ROAD_STREAM_PHASES
        if (row != &scratch_row) prow_fill(row_index);
#endif
        if (row != &scratch_row) { pending_row = row_index + 1u; ++row_index; fzx_e0_report[1] = row_index; }
        else pending_row = 0;
#if E0_HW_COUNTER
        {   /* Once a second (60 fields): the readout, inset from the top left for TV overscan. */
            const uint32_t frames = fzx_hw_frames, last = fzx_hw_last_field;
            if (frames > 1u && field >= hw_next) {
                const uint32_t fields = last - fzx_hw_first_field, secs = (field - fzx_hw_first_field) / 60u;
                const uint32_t mean10 = fields ? (frames - 1u) * 600u / fields : 0u;
                dbgio_printf("\x1b[H\n\n    E0 HW  %02lu:%02lu  LOOP %lu\n"
                             "    FRAMES %lu  FIELDS %lu\n"
                             "    FPS %lu  MEAN %lu.%lu\n"
                             "    WORST %luF  OVER2 %lu\n"
                             "    GAPS 1F %lu 2F %lu 3F %lu 4F+ %lu  \n",
                             (unsigned long)(secs / 60u), (unsigned long)(secs % 60u), (unsigned long)hw_loops,
                             (unsigned long)frames, (unsigned long)fields,
                             (unsigned long)(frames - hw_mark_frames), (unsigned long)(mean10 / 10u), (unsigned long)(mean10 % 10u),
                             (unsigned long)fzx_hw_worst, (unsigned long)fzx_hw_over2,
                             (unsigned long)fzx_hw_gaps[0], (unsigned long)fzx_hw_gaps[1], (unsigned long)fzx_hw_gaps[2], (unsigned long)fzx_hw_gaps[3]);
                dbgio_flush();
                hw_mark_frames = frames; hw_next = (hw_next ? hw_next : field) + 60u;
            }
        }
#endif
#if FZX_E1_DRIVE
        {
            const uint16_t h0 = cpu_frt_count_get();
            fzx_drive_hud();
            row->hud_ticks = (uint16_t)(cpu_frt_count_get() - h0);
        }
#endif
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
#if ENG_SCENES
    return true;
#endif
}
