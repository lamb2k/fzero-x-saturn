/* Engine line, milestone E1: the drive slice (see engine_drive.h). */
#include <string.h>
#include <stddef.h>
#include "engine_drive.h"
#include "physics_native.h"
#include "physics_identity.h"
#include "racer_sprites_identity.h"
#include "texture_cd.h"
#include "sh2zam/rotation_sprite_experimental.h"
#include "sh2zam/sprite_cache_experimental.h"
#if E1_FIELD
#include "engine_field.h"
#endif

#ifndef E1_DRIVER
#define E1_DRIVER 0
#endif
/* R01: kl 0.004, kh 2.5, target 0; R02 adds B every 240 ticks. */
#ifndef E1_DRIVER_BOOST
#define E1_DRIVER_BOOST 0
#endif
/* The fixtures' race script: 460 ticks of nothing, then A held for 9,000. */
enum { RACE_START_TICK = 460u, RACE_END_TICK = 460u + 9000u, CHECKPOINT_EVERY = 256u, CHECKPOINTS = 64u };
#define N64_A 0x8000u
#define N64_B 0x4000u

volatile uint32_t fzx_e1_driver = E1_DRIVER;
volatile uint32_t fzx_e1_pad_override;
volatile uint32_t fzx_e1_report[16];
/* FNV-1a 64 of the body alone after every CHECKPOINT_EVERY-th step (hi, lo):
 * the HOST driver prints the same with --checkpoints 256. */
volatile uint32_t fzx_e1_checkpoints[CHECKPOINTS][2];
volatile uint32_t fzx_e1_status; /* 1 loaded; 2 pack load, 3 pack format, 4 sprite load, 5 sprite set, 6 shadow */

static FzxPhysCourse course;
static sh2z_vd_body_t body;
static FzxPadState pad_state;
static uint16_t prev_n64;
static uint32_t ticks;
static uint32_t pack_buf[(FZX_PHYSICS_PACK_BYTES + 3u) / 4u + 2u] __attribute__((aligned(8)));

/* ---- physics ---------------------------------------------------------------- */
static uint64_t fnv64_body(const sh2z_vd_body_t *b) {
    uint64_t h = 0xCBF29CE484222325ull;
    const uint8_t *p = (const uint8_t *)b; /* big-endian words in memory: the HOST hash's byte order */
    for (unsigned i = 0; i < sizeof(*b); ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}
static double q30d(int32_t v) { return v / 1073741824.0; }
static double dot30(const int32_t a[3], const int32_t b[3]) {
    return q30d(a[0]) * q30d(b[0]) + q30d(a[1]) * q30d(b[1]) + q30d(a[2]) * q30d(b[2]);
}

/* The fixtures' driver, as tools/physics_host/physics_main.c computes it (float). */
static signed char driver_stick(void) {
    const float lateral = (float)(sh2z_vd_body_lateral(&body) / 65536.0);
    const float heading = (float)dot30(body.body[0], body.road[2]);
    float steer = 0.004f * (lateral - 0.0f) + 2.5f * heading;
    if (steer > 1.0f) steer = 1.0f;
    if (steer < -1.0f) steer = -1.0f;
    return (signed char)(steer >= 0.0f ? (int)(steer * 63.0f + 0.5f) : -(int)(-steer * 63.0f + 0.5f));
}

#if E1_FIELD
/* Close-up 3D-N: the rival field steps after the player, every tick (FRT time summed for the frame row). */
volatile uint32_t fzx_field_sim_ticks;
#endif
static void step_once(sh2z_vd_input_t *in) {
    sh2z_vd_step(&body, &course.track, &course.vehicle, &fzx_phys_rules, in);
#if E1_FIELD
    {
        const uint16_t f0 = cpu_frt_count_get();
        fzx_field_tick(&body);
        fzx_field_sim_ticks += (uint16_t)(cpu_frt_count_get() - f0);
    }
#endif
    ++ticks;
    if (ticks % CHECKPOINT_EVERY == 0u && ticks / CHECKPOINT_EVERY <= CHECKPOINTS) {
        const uint64_t h = fnv64_body(&body);
        fzx_e1_checkpoints[ticks / CHECKPOINT_EVERY - 1u][0] = (uint32_t)(h >> 32);
        fzx_e1_checkpoints[ticks / CHECKPOINT_EVERY - 1u][1] = (uint32_t)h;
        fzx_e1_report[8] = ticks / CHECKPOINT_EVERY;
    }
}

void fzx_drive_step(uint32_t n) {
    if (fzx_e1_driver) {
        for (uint32_t k = 0; k < n && ticks < RACE_END_TICK; ++k) {
            uint16_t button = ticks >= RACE_START_TICK ? N64_A : 0u;
            signed char sx = 0;
            if (body.intro == 0) {
                sx = driver_stick();
                if (E1_DRIVER_BOOST > 0 && ticks % E1_DRIVER_BOOST == 0u) button |= N64_B;
            }
            sh2z_vd_input_t in;
            in.held = fzx_phys_buttons_n64(button);
            in.pressed = fzx_phys_buttons_n64((uint16_t)((prev_n64 ^ button) & button));
            in.released = fzx_phys_buttons_n64((uint16_t)((prev_n64 ^ button) & prev_n64));
            in.stick_x = sx; in.stick_y = 0;
            prev_n64 = button;
            step_once(&in);
            if (ticks == RACE_END_TICK) {
                const uint64_t h = fnv64_body(&body);
                fzx_e1_report[5] = ticks; fzx_e1_report[6] = (uint32_t)(h >> 32); fzx_e1_report[7] = (uint32_t)h;
            }
        }
    } else if (n) {
        uint16_t pad = 0;
        const uint32_t o = fzx_e1_pad_override;
        if (o & 0x10000u) pad = (uint16_t)o;
        else {
            smpc_peripheral_digital_t d = {0};
            smpc_peripheral_process();
            smpc_peripheral_digital_port(1, &d);
            if (d.connected) pad = d.pressed.raw; /* Yaul: "pressed" is the current state */
        }
        sh2z_vd_input_t in;
        fzx_phys_pad(&pad_state, pad, &in);
        for (uint32_t k = 0; k < n; ++k) {
            step_once(&in);
            in.pressed = 0; in.released = 0; /* one edge per press: the later ticks hold */
        }
    }
    fzx_e1_report[0] = ticks; fzx_e1_report[1] = (uint32_t)body.laps_done; fzx_e1_report[2] = (uint32_t)body.race_time;
    fzx_e1_report[3] = (body.flags & SH2Z_VD_FINISHED) ? 1u : 0u; fzx_e1_report[4] = (uint32_t)body.speed;
    fzx_e1_report[13] = (uint32_t)body.lap_times[0]; fzx_e1_report[14] = (uint32_t)body.lap_times[1];
    fzx_e1_report[15] = (uint32_t)body.lap_times[2];
}

uint32_t fzx_drive_tick(void) { return ticks; }
#if E1_CLOSEUP == 1 || E1_FIELD
/* Arm A (engine_closeup.c) poses the 3D model from the body itself. */
const sh2z_vd_body_t *fzx_drive_body(void) { return &body; }
#endif

/* ---- chase camera --------------------------------------------------------------
 * Fitted to the race camera (camera 0) against the car over ticks 600..6900 of the
 * reference R03 replay, in the car's forward/up frame: eye 98.869 back and 60.294 up,
 * look-at 24.190 ahead and 24.172 down (sd 1.6 to 3.5 units). The rotation is the
 * car frame pitched down by atan(84.466 / 123.059). First cut: rigid, no lag. */
#define QF(x, bits) ((int32_t)((x) * (double)(1 << (bits)) + ((x) >= 0 ? 0.5 : -0.5)))
#define CAM_DX 123.059
#define CAM_DY 84.466
#define CAM_LEN 149.2577 /* sqrt(DX^2 + DY^2) */
static const int32_t EYE_F = QF(-98.869, 16), EYE_U = QF(60.294, 16);
static const int32_t PITCH_C = QF(CAM_DX / CAM_LEN, 30), PITCH_S = QF(CAM_DY / CAM_LEN, 30);
static const int32_t ROLL_C = QF(0.920504853, 30), ROLL_S = QF(0.390731128, 30); /* cos, sin 23 degrees */
volatile uint32_t fzx_e1_roll_limited; /* frames whose camera roll was limited */
#if E1_CLOSEUP == 1
volatile uint32_t fzx_e1_cam_orbit; /* stills: see fzx_drive_camera */
#endif

static int32_t mul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }
static uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

void fzx_drive_camera(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens) {
#if E1_CLOSEUP == 1
    /* Stills only: fzx_e1_cam_orbit 1 swings the chase camera 135 degrees around the car's
     * up axis (a front three-quarter view), 2 by 90 degrees (side); 0 is the chase camera. */
    int32_t orbit_f[3];
    const int32_t *f = body.body[0], *u = body.body[1];
    if (fzx_e1_cam_orbit == 1u || fzx_e1_cam_orbit == 2u) {
        const int32_t oc = fzx_e1_cam_orbit == 1u ? -759250125 : 0, os = fzx_e1_cam_orbit == 1u ? 759250125 : 1073741824;
        for (unsigned i = 0; i < 3u; ++i) orbit_f[i] = mul30(oc, f[i]) + mul30(os, body.body[2][i]);
        f = orbit_f;
    }
#else
    const int32_t *f = body.body[0], *u = body.body[1];
#endif
    int32_t cf[3], cu[3], r0[3];
    for (unsigned i = 0; i < 3u; ++i) {
        cam->eye[i] = body.pos[i] + mul30(EYE_F, f[i]) + mul30(EYE_U, u[i]);
        cf[i] = mul30(PITCH_C, f[i]) - mul30(PITCH_S, u[i]);
        cu[i] = mul30(PITCH_S, f[i]) + mul30(PITCH_C, u[i]);
    }
    r0[0] = (int32_t)(((int64_t)cf[1] * cu[2] - (int64_t)cf[2] * cu[1]) >> 30);
    r0[1] = (int32_t)(((int64_t)cf[2] * cu[0] - (int64_t)cf[0] * cu[2]) >> 30);
    r0[2] = (int32_t)(((int64_t)cf[0] * cu[1] - (int64_t)cf[1] * cu[0]) >> 30);
    {
        /* Roll limit: the race camera never rolls past 23 degrees (reference camera 0,
         * whole R03 race: |roll| max 23.0), and the VDP2 floor mapping refuses some
         * views rolled further (fault 2 in the half-pipe). h is the right row made
         * level, v = h x cf the zero-roll up; past the limit the right row becomes
         * cos(L) h + sin(L) v on the same side, and up = right x forward. */
        const uint32_t hn = isqrt64((uint64_t)((int64_t)r0[0] * r0[0] + (int64_t)r0[2] * r0[2]));
        if (hn > (1u << 24)) {
            const int32_t h[3] = { (int32_t)(((int64_t)r0[0] << 30) / (int64_t)hn), 0,
                                   (int32_t)(((int64_t)r0[2] << 30) / (int64_t)hn) };
            int32_t v[3];
            v[0] = (int32_t)(((int64_t)h[1] * cf[2] - (int64_t)h[2] * cf[1]) >> 30);
            v[1] = (int32_t)(((int64_t)h[2] * cf[0] - (int64_t)h[0] * cf[2]) >> 30);
            v[2] = (int32_t)(((int64_t)h[0] * cf[1] - (int64_t)h[1] * cf[0]) >> 30);
            if ((int64_t)v[0] * cu[0] + (int64_t)v[1] * cu[1] + (int64_t)v[2] * cu[2] < 0) { v[0] = -v[0]; v[1] = -v[1]; v[2] = -v[2]; }
            const int32_t s = (int32_t)(((int64_t)r0[0] * v[0] + (int64_t)r0[1] * v[1] + (int64_t)r0[2] * v[2]) >> 30);
            if (s > ROLL_S || s < -ROLL_S) {
                const int32_t sl = s > 0 ? ROLL_S : -ROLL_S;
                for (unsigned i = 0; i < 3u; ++i) r0[i] = mul30(ROLL_C, h[i]) + mul30(sl, v[i]);
                cu[0] = (int32_t)(((int64_t)r0[1] * cf[2] - (int64_t)r0[2] * cf[1]) >> 30);
                cu[1] = (int32_t)(((int64_t)r0[2] * cf[0] - (int64_t)r0[0] * cf[2]) >> 30);
                cu[2] = (int32_t)(((int64_t)r0[0] * cf[1] - (int64_t)r0[1] * cf[0]) >> 30);
                ++fzx_e1_roll_limited;
            }
        }
    }
    for (unsigned j = 0; j < 3u; ++j) { cam->rotation[0][j] = r0[j]; cam->rotation[1][j] = cu[j]; cam->rotation[2][j] = cf[j]; }
    for (unsigned i = 0; i < 3u; ++i) {
        const int64_t d = (int64_t)cam->rotation[i][0] * cam->eye[0] + (int64_t)cam->rotation[i][1] * cam->eye[1] +
                          (int64_t)cam->rotation[i][2] * cam->eye[2];
        cam->translation[i] = (int32_t)(-(d >> 34)); /* Q16 at 1/16 source scale */
    }
    cam->focal[0] = lens->focal[0]; cam->focal[1] = lens->focal[1]; cam->tan_half = lens->tan_half;
    const uint32_t n = isqrt64((uint64_t)((int64_t)cf[0] * cf[0] + (int64_t)cf[2] * cf[2]));
    if (n) {
        cam->xzn[0] = (int32_t)(((int64_t)cf[0] << 30) / (int64_t)n);
        cam->xzn[1] = (int32_t)(((int64_t)cf[2] << 30) / (int64_t)n);
    } else { cam->xzn[0] = 1 << 30; cam->xzn[1] = 0; }
}

/* ---- player sprite (claude/racer-sprites 45b74b7 draw16 path, through the slot cache) --
 * SPRITE-RACERS-PLAN.md section 9.4 (SH2ZAM sprite-cache-1):
 * - the raw RSP1 set lives in the cartridge; its header and cell table are copied to
 *   work RAM for the pose, its CLUTs to the VDP1 CLUT partition (one 32 B entry per
 *   resident set, 60 reserved), beside the 320 B blob shadow;
 * - cells are drawn from an 89,600 B slot pool at the top of the texture partition;
 * - the master selects (get), emits and queues misses; the slave copies queued cells
 *   from the cartridge into their slots right after the track build (1 ms budget), and
 *   the master sees them READY the next frame. Slot states are read and written
 *   through the cache-through view on both CPUs. */
#define MODEL_SCALE_Q14 1638 /* the source's machine model scale 0.1, Q1.14 */
#define SHADOW_W 32u
#define SHADOW_H 16u
#define VDP1_VRAM_BASE 0x25C00000u
#define CT(p) ((__typeof__(p))((uintptr_t)(p) | CPU_CACHE_THROUGH))
#if E1_CLOSEUP == 2
/* Arm B: the near set's header and 128-cell table (2,216 B) need a larger work RAM copy. */
enum { CLUT_SETS = 60u, POOL_SLOTS = 90u, FILL_MAX = 32u, SET_TABLE_BYTES = 4096u, FILL_BUDGET_TICKS = 210u /* 1 ms at FRT/128 */ };
volatile uint32_t fzx_ab_cell[3] = { 0xffffu, 0u, 0u }; /* last cell, changes, last scale (Q16 screen px per cell px) */
volatile uint32_t fzx_ab_cell_frames[128];           /* frames per cell */
#else
enum { CLUT_SETS = 60u, POOL_SLOTS = 90u, FILL_MAX = 32u, SET_TABLE_BYTES = 2048u, FILL_BUDGET_TICKS = 210u /* 1 ms at FRT/128 */ };
#endif
static const uint32_t pool_sizes[4] = { 512u, 1024u, 1536u, 2304u }, pool_counts[4] = { 40u, 24u, 20u, 6u };
typedef struct {sh2z_rspr_set_t header;sh2z_rspr_cell_t cell;uint16_t clut[16];uint8_t pixels[SHADOW_W*SHADOW_H/2];} shadow_set_t;
static const sh2z_rspr_set_t *racer_set, *shadow_set; /* racer_set: header and cells in work RAM */
static uintptr_t set_cart;                            /* the whole raw set in the cartridge */
static uint32_t shadow_vram, clut_vram;
static uint16_t racer_pmod, shadow_pmod;
static sh2z_rspr_camera_t racer_camera, shadow_camera;
static uint32_t set_table[SET_TABLE_BYTES / 4u];
static sh2z_scache_slot_t pool_slots[POOL_SLOTS];
static sh2z_scache_t pool;
typedef struct { uint16_t slot, cell; } fill_t;
static fill_t fill_queue[FILL_MAX];
static volatile uint32_t fill_count, fill_done;
static uint32_t drawn_slot = UINT32_MAX, drawn_cell; /* the player's last drawn cell */
/* VDP1 map (byte offsets in VDP1 VRAM): [0..1] slot pool, [2] slots, [3..4] set CLUTs,
 * [5..6] shadow, [7] set in the cartridge, [8] set bytes, [9..10] texture partition,
 * [11..12] CLUT partition, [13] dbank stage end, [14] lowest E0 texture end allowed. */
volatile uint32_t fzx_e1_vram[16];
/* Cache: hits, misses, evictions, refused, previous-cell draws, fills, fill bytes,
 * fills left for the next frame, abandoned, slave fill ticks (last), max. */
volatile uint32_t fzx_e1_cache[12];
/* The player sprite this frame: origin x, y (whole pixels, -32768 none), sub-pixel x, y. */
volatile uint32_t fzx_e1_sprite_pos[4];

static uint32_t cell_bytes(uint32_t cell) {
    const sh2z_rspr_cell_t *c = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1) + cell;
    return (uint32_t)c->width * c->height / 2u;
}

static bool shadow_build(void *vram) {
    static shadow_set_t t;memset(&t,0,sizeof(t));
    t.header.magic=SH2Z_RSPR_MAGIC;t.header.bytes=(uint32_t)sizeof(t);t.header.yaw_count=1;t.header.pitch_count=1;
    t.header.clut_count=1;t.header.clut_offset=(uint32_t)offsetof(shadow_set_t,clut);
    t.header.pixels_per_unit=(int32_t)((float)SHADOW_W*65536.0f/(1.4f*FZX_RACER_SPRITE_RADIUS));
    t.header.yaw[0][1]=16384;t.header.pitch[0][1]=16384;
    t.cell=(sh2z_rspr_cell_t){.width=SHADOW_W,.height=SHADOW_H,.origin_x=SHADOW_W/2,.origin_y=SHADOW_H/2,
        .pixel_offset=(uint32_t)offsetof(shadow_set_t,pixels),.clut=0};
    t.clut[1]=0x8000u;
    for(unsigned y=0;y<SHADOW_H;++y)for(unsigned x=0;x<SHADOW_W;++x) {
        const int dx=(int)(2*x+1)-(int)SHADOW_W,dy=(int)(2*y+1)-(int)SHADOW_H;
        if(dx*dx*(int)(SHADOW_H*SHADOW_H)+dy*dy*(int)(SHADOW_W*SHADOW_W)<=(int)(SHADOW_W*SHADOW_W*SHADOW_H*SHADOW_H))
            t.pixels[(y*SHADOW_W+x)/2]|=(uint8_t)((x&1u)?0x01u:0x10u);
    }
    memcpy(vram,&t,sizeof(t));
    shadow_set=(const sh2z_rspr_set_t *)vram;shadow_vram=(uint32_t)((uintptr_t)vram&0x7ffffu);
    return sh2z_rspr_valid(shadow_set);
}

bool fzx_drive_load(void *scratch, uint32_t scratch_bytes) {
    fzx_e1_report[12] = FZX_PHYSICS_PACK_BYTES;
    if (!fzx_texture_cd_load("PHYS.PAK", FZX_PHYSICS_PACK_BYTES, FZX_PHYSICS_PACK_HASH, pack_buf, scratch, scratch_bytes))
        { fzx_e1_status = 2; return false; }
    if (fzx_phys_load(&course, (const uint8_t *)pack_buf, FZX_PHYSICS_PACK_BYTES, 1) != 0) { fzx_e1_status = 3; return false; }
    body = course.start; prev_n64 = 0; ticks = 0;
    memset(&pad_state, 0, sizeof(pad_state));
#if E1_FIELD
#if E2_PACK
    if (!fzx_field_pack_load(scratch, scratch_bytes)) { fzx_e1_status = 10; return false; }
#endif
    if (!fzx_field_load(&course.track, &body)) { fzx_e1_status = 9; return false; }
#endif
    return true;
}

bool fzx_drive_sprites_load(const vdp1_vram_partitions_t *parts, uint32_t clut_used, uintptr_t cart_free,
                            uintptr_t cart_limit, void *scratch, uint32_t scratch_bytes) {
    /* The raw set into the cartridge (CPU stores; no DMA with a cartridge endpoint). */
    set_cart = (cart_free + 63u) & ~(uintptr_t)63u;
    if (set_cart + FZX_RACER_SPRITE_BYTES + 32u > cart_limit ||
        !fzx_texture_cd_load("RSPRITE.PAK", FZX_RACER_SPRITE_BYTES, FZX_RACER_SPRITE_HASH, (void *)set_cart, scratch, scratch_bytes))
        { fzx_e1_status = 4; return false; }
    const sh2z_rspr_set_t *full = (const sh2z_rspr_set_t *)set_cart;
    if (!sh2z_rspr_valid(full)) { fzx_e1_status = 5; return false; }
    const uint32_t table = (uint32_t)sizeof(sh2z_rspr_set_t) + (uint32_t)full->yaw_count * full->pitch_count * (uint32_t)sizeof(sh2z_rspr_cell_t);
    const uint32_t cluts = (uint32_t)full->clut_count * 32u;
    if (table > sizeof(set_table) || full->clut_count > CLUT_SETS) { fzx_e1_status = 5; return false; }
    memcpy(set_table, full, table);
    racer_set = (const sh2z_rspr_set_t *)(const void *)set_table;
    /* CLUT partition: E0's bank stages first, then the set CLUTs (60 reserved), then the shadow. */
    const uintptr_t clut_base = (uintptr_t)parts->clut_base, clut_end = clut_base + parts->clut_size;
    const uintptr_t cl = (clut_base + clut_used + 31u) & ~(uintptr_t)31u, cl_end = cl + CLUT_SETS * 32u;
    const uintptr_t sh = (cl_end + 7u) & ~(uintptr_t)7u, sh_end = sh + ((sizeof(shadow_set_t) + 7u) & ~7u);
    if (sh_end > clut_end) { fzx_e1_status = 6; return false; }
    memcpy((void *)cl, (const void *)(set_cart + full->clut_offset), cluts);
    clut_vram = (uint32_t)(cl & 0x7ffffu);
    if (!shadow_build((void *)sh)) { fzx_e1_status = 6; return false; }
    /* Slot pool at the top of the texture partition. */
    const uint32_t tex_base = (uint32_t)((uintptr_t)parts->texture_base & 0x7ffffu);
    uint32_t pool_bytes = 0;
    for (unsigned k = 0; k < 4u; ++k) pool_bytes += pool_sizes[k] * pool_counts[k];
    const uint32_t pool_base = (tex_base + parts->texture_size - pool_bytes) & ~7u;
    uint32_t pool_end = 0;
    const uint32_t n = sh2z_scache_layout(pool_slots, POOL_SLOTS, pool_base, tex_base + parts->texture_size - pool_base,
                                          pool_sizes, pool_counts, 4u, &pool_end);
    if (!n || !sh2z_scache_init(&pool, CT(&pool_slots[0]), n, 2u /* vdp1-banks-1 double banks */)) { fzx_e1_status = 7; return false; }
    for (uint32_t i = 0; i < full->yaw_count * full->pitch_count; ++i) if (cell_bytes(i) > pool_sizes[3]) { fzx_e1_status = 8; return false; }
    fill_count = 0; fill_done = 0;
    {
        vdp1_cmdt_t mode_cmd; memset(&mode_cmd, 0, sizeof(mode_cmd));
        const vdp1_cmdt_draw_mode_t mode = {.end_code_disable = 1, .user_clipping_mode = 2, .color_mode = VDP1_CMDT_CM_CLUT_16};
        vdp1_cmdt_draw_mode_set(&mode_cmd, mode); racer_pmod = mode_cmd.cmd_pmod;
        const vdp1_cmdt_draw_mode_t half = {.end_code_disable = 1, .user_clipping_mode = 2, .color_mode = VDP1_CMDT_CM_CLUT_16,
                                            .cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT};
        vdp1_cmdt_draw_mode_set(&mode_cmd, half); shadow_pmod = mode_cmd.cmd_pmod;
    }
    fzx_e1_vram[0] = pool_base; fzx_e1_vram[1] = pool_end; fzx_e1_vram[2] = n;
    fzx_e1_vram[3] = (uint32_t)(cl & 0x7ffffu); fzx_e1_vram[4] = (uint32_t)(cl_end & 0x7ffffu);
    fzx_e1_vram[5] = (uint32_t)(sh & 0x7ffffu); fzx_e1_vram[6] = (uint32_t)(sh_end & 0x7ffffu);
    fzx_e1_vram[7] = (uint32_t)set_cart; fzx_e1_vram[8] = FZX_RACER_SPRITE_BYTES;
    fzx_e1_vram[9] = tex_base; fzx_e1_vram[10] = tex_base + parts->texture_size;
    fzx_e1_vram[11] = (uint32_t)(clut_base & 0x7ffffu); fzx_e1_vram[12] = (uint32_t)(clut_end & 0x7ffffu);
    fzx_e1_vram[13] = (uint32_t)((clut_base + clut_used) & 0x7ffffu); fzx_e1_vram[14] = pool_base - tex_base;
    fzx_e1_report[9] = 1; fzx_e1_status = 1;
    return true;
}

uint32_t fzx_drive_texture_floor(void) { return fzx_e1_vram[14]; }

/* Slave, right after the track build: copy queued cells into their slots. */
void fzx_drive_slave_fill(void) {
    if (fzx_e1_status != 1) return;
    const uint16_t t0 = cpu_frt_count_get();
    sh2z_scache_t *const c = CT(&pool);
    const sh2z_rspr_cell_t *const cells = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1);
    const uint32_t count = fill_count;
    uint32_t done = 0, bytes = 0;
    while (done < count) {
        if (done && (uint16_t)(cpu_frt_count_get() - t0) >= FILL_BUDGET_TICKS) break;
        const fill_t f = fill_queue[done];
        if (!sh2z_scache_live(c, f.slot)) { (void)sh2z_scache_abandon(c, f.slot); ++fzx_e1_cache[8]; ++done; continue; }
        const uint32_t n = (uint32_t)cells[f.cell].width * cells[f.cell].height / 8u; /* longwords */
        const volatile uint32_t *src = (const volatile uint32_t *)(set_cart + cells[f.cell].pixel_offset);
        volatile uint32_t *dst = (volatile uint32_t *)(VDP1_VRAM_BASE + c->slots[f.slot].addr);
        for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
        (void)sh2z_scache_ready(c, f.slot);
        bytes += n * 4u; ++done;
    }
    fill_done = done;
    fzx_e1_cache[5] += done; fzx_e1_cache[6] += bytes; fzx_e1_cache[7] = count - done;
    const uint32_t t = (uint16_t)(cpu_frt_count_get() - t0);
    fzx_e1_cache[9] = t; if (t > fzx_e1_cache[10]) fzx_e1_cache[10] = t;
}

static void fill_push(uint32_t slot, uint32_t cell) {
    if (fill_count < FILL_MAX) { fill_queue[fill_count].slot = (uint16_t)slot; fill_queue[fill_count].cell = (uint16_t)cell; ++fill_count; }
    else (void)sh2z_scache_abandon(&pool, slot);
}

static bool sprite_camera(const fzx_view_t *view, const fzx_engine_cam_t *cam) {
    sh2z_rspr_camera_t c; memset(&c, 0, sizeof(c));
    for (unsigned i = 0; i < 3u; ++i) for (unsigned j = 0; j < 3u; ++j) {
        const int32_t n = view->rotation[i][j]; c.r[i][j] = (int16_t)((n >= 0 ? n + 32768 : -(-n + 32768)) / 65536);
    }
    for (unsigned i = 0; i < 3u; ++i) c.eye[i] = cam->eye[i];
    c.focal_x = view->focal[0]; c.focal_y = view->focal[1];
    c.center_x = (int16_t)((view->center[0] + 32768) >> 16); c.center_y = (int16_t)(((view->center[1] + 32768) >> 16) - 1);
    c.near_z = view->near_plane * 4;
    racer_camera = c; shadow_camera = c;
    return sh2z_rspr_camera_scale(&racer_camera, racer_set, MODEL_SCALE_Q14) &&
           sh2z_rspr_camera_scale(&shadow_camera, shadow_set, MODEL_SCALE_Q14);
}

unsigned fzx_drive_sprites(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view, const fzx_engine_cam_t *cam) {
    if (fzx_e1_status != 1 || capacity < 3u) { ++fzx_e1_report[11]; return 0; }
    fzx_e1_sprite_pos[0] = fzx_e1_sprite_pos[1] = (uint32_t)-32768;
    /* The slave's fills of the last job are in: drop them from the queue. */
    {
        const uint32_t done = *CT(&fill_done), count = fill_count;
        uint32_t k = 0;
        for (uint32_t i = done; i < count; ++i) fill_queue[k++] = fill_queue[i];
        fill_count = k; fill_done = 0;
    }
    sh2z_scache_frame(&pool);
    if (!sprite_camera(view, cam)) { ++fzx_e1_report[11]; return 0; }
    int16_t basis[9];
    int32_t pos[3];
    for (unsigned i = 0; i < 3u; ++i) {
        pos[i] = body.pos[i];
        for (unsigned j = 0; j < 3u; ++j) {
            const int32_t v = body.body[i][j];
            basis[i * 3u + j] = (int16_t)((v + (1 << 15)) >> 16); /* Q30 -> Q1.14 */
        }
    }
    unsigned out = 0;
    memset(&commands[out], 0, sizeof(commands[out])); vdp1_cmdt_user_clip_coord_set(&commands[out]);
    commands[out].cmd_xa = view->clip[0]; commands[out].cmd_ya = view->clip[1];
    commands[out].cmd_xc = view->clip[2] - 1; commands[out].cmd_yc = view->clip[3] - 1;
    ++out;
    {
        /* Shadow: down the up axis by 0.3 model radii (Q1.14 basis x Q2 = Q16). */
        const int32_t drop = (int32_t)(0.3f * FZX_RACER_SPRITE_RADIUS * 0.1f * 4.0f);
        int32_t ground[3], depth; sh2z_rspr_pick_t pick; sh2z_rspr_place_t place;
        const sh2z_rspr_pick_t zero = {0};
        for (unsigned k = 0; k < 3u; ++k) ground[k] = pos[k] - basis[3 + k] * drop;
        if (sh2z_rspr_pose(&pick, &place, &depth, &shadow_camera, shadow_set, ground, basis) &&
            sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], shadow_set, &zero, &place, shadow_vram, shadow_pmod)) ++out;
    }
#if E1_CLOSEUP == 1
    /* Arm A: the 3D model follows the shadow (engine_closeup.c); no sprite cell. */
    fzx_e1_report[10] = out - 1u;
    return out;
#endif
    {
        int32_t depth; sh2z_rspr_pick_t pick; sh2z_rspr_place_t place;
        if (sh2z_rspr_pose(&pick, &place, &depth, &racer_camera, racer_set, pos, basis)) {
#if E1_CLOSEUP == 2
            /* Arm B: which cells the player shows (look list: cell changes per run). */
            if (pick.cell != fzx_ab_cell[0]) ++fzx_ab_cell[1];
            fzx_ab_cell[0] = pick.cell; fzx_ab_cell[2] = (uint32_t)place.scale;
            if (pick.cell < 128u) fzx_ab_cell_frames[pick.cell] += 1u;
#endif
            /* Key = racer x 1024 + profile x 256 + cell (player 0, profile 0). */
            uint32_t slot = 0;
            const int r = sh2z_scache_get(&pool, pick.cell, cell_bytes(pick.cell), false, &slot);
            if (r == SH2Z_SCACHE_MISS) fill_push(slot, pick.cell);
            bool draw = r == SH2Z_SCACHE_HIT;
            if (!draw && r != SH2Z_SCACHE_REFUSED && drawn_slot != UINT32_MAX) {
                /* Not in yet: the previous cell, if its slot still holds it. */
                uint32_t again = 0;
                if (sh2z_scache_get(&pool, drawn_cell, cell_bytes(drawn_cell), false, &again) == SH2Z_SCACHE_HIT) {
                    pick.cell = (uint16_t)drawn_cell; slot = again; draw = true; ++fzx_e1_cache[4];
                }
            }
            if (draw && sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], racer_set, &pick, &place, 0u, racer_pmod)) {
                const sh2z_rspr_cell_t *cell = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1) + pick.cell;
                commands[out].cmd_srca = (uint16_t)(pool.slots[slot].addr >> 3);
                commands[out].cmd_colr = (uint16_t)((clut_vram + (uint32_t)cell->clut * 32u) >> 3);
                drawn_slot = slot; drawn_cell = pick.cell;
                fzx_e1_sprite_pos[0] = (uint32_t)(int32_t)place.x; fzx_e1_sprite_pos[1] = (uint32_t)(int32_t)place.y;
                fzx_e1_sprite_pos[2] = place.frac[0]; fzx_e1_sprite_pos[3] = place.frac[1];
                ++out;
            } else ++fzx_e1_report[11];
            /* Prefetch: the cell for one frame (two ticks) of velocity ahead. */
            int32_t ahead[3], d2; sh2z_rspr_pick_t p2; sh2z_rspr_place_t q2;
            for (unsigned k = 0; k < 3u; ++k) ahead[k] = pos[k] + 2 * body.vel[k];
            if (sh2z_rspr_pose(&p2, &q2, &d2, &racer_camera, racer_set, ahead, basis) && p2.cell != pick.cell) {
                uint32_t s2 = 0;
                if (sh2z_scache_get(&pool, p2.cell, cell_bytes(p2.cell), true, &s2) == SH2Z_SCACHE_MISS) fill_push(s2, p2.cell);
            }
        } else ++fzx_e1_report[11];
    }
    fzx_e1_cache[0] = pool.hits; fzx_e1_cache[1] = pool.misses; fzx_e1_cache[2] = pool.evictions; fzx_e1_cache[3] = pool.refused;
    fzx_e1_report[10] = out - 1u;
    return out > 1u ? out : 0u;
}

#if E1_FIELD
/* ---- close-up 3D-N: every car of the field, far to near ---------------------------
 * Sprite cars draw through the slot cache as the player sprite does (one cell each,
 * the car's previous cell while a fill is pending); 3D cars copy the commands built
 * for them earlier (engine_main.c stages them while the slave builds the track).
 * Shadows: the player and the nearest FIELD_SHADOWS other cars. */
enum { FIELD_SHADOWS = 8u };
static uint16_t car_cell[FZX_FIELD_CARS];
static uint8_t car_has_cell[FZX_FIELD_CARS];
volatile uint32_t fzx_field_draw[8]; /* sprites drawn, 3D cars drawn, shadows, sprite skips, cells pending, commands, -, - */
#if E2_CULL
/* Run-time words: the smallest car drawn (pixels of the set's largest cell extent) and the
 * smallest sprite scale (Q16) that still gets a shadow. */
volatile uint32_t fzx_e2_min_px = 4, fzx_e2_shadow_scale = 16384;
static uint32_t cull_extent;
static int32_t cull_dot(const int16_t row[3], const int32_t d[3]) {
    const int64_t sum = (int64_t)row[0] * d[0] + (int64_t)row[1] * d[1] + (int64_t)row[2] * d[2];
    return (int32_t)(sum >> 16);
}
#endif

unsigned fzx_drive_cars(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view, const fzx_engine_cam_t *cam,
                        const fzx_field_car_t *cars, unsigned n, const vdp1_cmdt_t *const *staged, const uint16_t *staged_n) {
    if (fzx_e1_status != 1 || capacity < 2u) { ++fzx_e1_report[11]; return 0; }
    fzx_e1_sprite_pos[0] = fzx_e1_sprite_pos[1] = (uint32_t)-32768;
    {
        const uint32_t done = *CT(&fill_done), count = fill_count;
        uint32_t k = 0;
        for (uint32_t i = done; i < count; ++i) fill_queue[k++] = fill_queue[i];
        fill_count = k; fill_done = 0;
    }
    sh2z_scache_frame(&pool);
    if (!sprite_camera(view, cam)) { ++fzx_e1_report[11]; return 0; }
    typedef struct { sh2z_rspr_pick_t pick; sh2z_rspr_place_t place; int32_t depth; uint8_t ok; } item_t;
    static item_t items[FZX_FIELD_CARS];
    uint8_t order_[FZX_FIELD_CARS];
#if E2_CULL
    /* Cheap reject before any sprite work (render only): behind the near plane, past the
     * size where the car would be under fzx_e2_min_px pixels, or off the view rectangle by
     * more than the set's largest cell extent at that depth. The player and 3D cars always
     * pass. Survivors are posed and sorted; the rest never reach the command list. */
    if (!cull_extent) {
        const sh2z_rspr_cell_t *cells = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1);
        const uint32_t count = (uint32_t)racer_set->yaw_count * racer_set->pitch_count;
        for (uint32_t c = 0; c < count; ++c) {
            if (cells[c].width > cull_extent) cull_extent = cells[c].width;
            if (cells[c].height > cull_extent) cull_extent = cells[c].height;
        }
    }
    const int64_t ext = racer_camera.scale_num * (int64_t)cull_extent;
    const int64_t far_tz = ext / ((int64_t)(fzx_e2_min_px ? fzx_e2_min_px : 1u) << 16);
    const int32_t cx = racer_camera.center_x, cy = racer_camera.center_y;
    const int64_t lx = (int64_t)(cx - view->clip[0]) << 16, rx = (int64_t)(view->clip[2] - cx) << 16;
    const int64_t ty_ = (int64_t)(cy - view->clip[1]) << 16, by = (int64_t)(view->clip[3] - cy) << 16;
    uint32_t c_behind = 0, c_far = 0, c_off = 0;
#endif
    for (unsigned i = 0; i < n; ++i) {
        item_t *it = &items[i];
#if E2_CULL
        if (cars[i].id != 0 && !cars[i].is3d) {
            const int32_t d[3] = { (int32_t)((uint32_t)cars[i].pos[0] - (uint32_t)racer_camera.eye[0]),
                                   (int32_t)((uint32_t)cars[i].pos[1] - (uint32_t)racer_camera.eye[1]),
                                   (int32_t)((uint32_t)cars[i].pos[2] - (uint32_t)racer_camera.eye[2]) };
            const int32_t tz = cull_dot(racer_camera.r[2], d);
            uint8_t reason = 0;
            if (tz <= racer_camera.near_z || tz <= 0) reason = 1;
            else if (tz > far_tz) reason = 2;
            else {
                const int64_t qx = (int64_t)racer_camera.focal_x * cull_dot(racer_camera.r[0], d);
                const int64_t qy = (int64_t)racer_camera.focal_y * cull_dot(racer_camera.r[1], d);
                if (qx - ext > rx * tz || -qx - ext > lx * tz || qy - ext > ty_ * tz || -qy - ext > by * tz) reason = 3;
            }
            if (reason) {
                it->ok = 0; it->depth = INT32_MAX;
                if (reason == 1) ++c_behind; else if (reason == 2) ++c_far; else ++c_off;
                unsigned j = i; /* culled: far end of the order, never drawn */
                while (j > 0 && items[order_[j - 1]].depth < INT32_MAX) { order_[j] = order_[j - 1]; --j; }
                order_[j] = (uint8_t)i;
                continue;
            }
        }
#endif
        int16_t basis[9];
        memcpy(basis, cars[i].basis, sizeof(basis));
        it->ok = sh2z_rspr_pose(&it->pick, &it->place, &it->depth, &racer_camera, racer_set, cars[i].pos, basis) ? 1u : 0u;
        if (!it->ok) it->depth = cars[i].is3d ? 0 : INT32_MAX; /* behind the camera: no sprite; 3D cars last */
        const int32_t key = it->depth;
        unsigned j = i;
        while (j > 0 && items[order_[j - 1]].depth < key) { order_[j] = order_[j - 1]; --j; }
        order_[j] = (uint8_t)i;
    }
    /* Shadows: the player and the nearest FIELD_SHADOWS others (nearest = end of the order). */
    uint8_t shadow[FZX_FIELD_CARS];
    {
        unsigned others = 0;
        for (unsigned k = n; k-- > 0;) {
            const unsigned i = order_[k];
#if E2_CULL
            shadow[i] = (uint8_t)(items[i].ok && (cars[i].id == 0 ||
                (others < FIELD_SHADOWS && items[i].place.scale >= (int32_t)fzx_e2_shadow_scale)));
#else
            shadow[i] = (uint8_t)(items[i].ok && (cars[i].id == 0 || others < FIELD_SHADOWS));
#endif
            if (cars[i].id != 0 && items[i].ok) ++others;
        }
    }
    unsigned out = 0, sprites = 0, solid = 0, shadows = 0;
    memset(&commands[out], 0, sizeof(commands[out])); vdp1_cmdt_user_clip_coord_set(&commands[out]);
    commands[out].cmd_xa = view->clip[0]; commands[out].cmd_ya = view->clip[1];
    commands[out].cmd_xc = view->clip[2] - 1; commands[out].cmd_yc = view->clip[3] - 1;
    ++out;
    const int32_t drop = (int32_t)(0.3f * FZX_RACER_SPRITE_RADIUS * 0.1f * 4.0f);
    const sh2z_rspr_pick_t zero = {0};
    for (unsigned k = 0; k < n; ++k) {
        const unsigned i = order_[k];
        const fzx_field_car_t *c = &cars[i];
        if (shadow[i] && out < capacity) {
            int32_t ground[3], depth; sh2z_rspr_pick_t pick; sh2z_rspr_place_t place;
            int16_t basis[9]; memcpy(basis, c->basis, sizeof(basis));
            for (unsigned a = 0; a < 3u; ++a) ground[a] = c->pos[a] - basis[3 + a] * drop;
            if (sh2z_rspr_pose(&pick, &place, &depth, &shadow_camera, shadow_set, ground, basis) &&
                sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], shadow_set, &zero, &place, shadow_vram, shadow_pmod)) { ++out; ++shadows; }
        }
        if (c->is3d) {
            const unsigned m = staged_n[c->slot];
            if (m && out + m <= capacity) { memcpy(&commands[out], staged[c->slot], m * sizeof(vdp1_cmdt_t)); out += m; ++solid; }
            continue;
        }
        if (!items[i].ok || out >= capacity) continue;
        sh2z_rspr_pick_t pick = items[i].pick;
        uint32_t slot = 0;
        const int r = sh2z_scache_get(&pool, pick.cell, cell_bytes(pick.cell), false, &slot);
        if (r == SH2Z_SCACHE_MISS) fill_push(slot, pick.cell);
        bool draw = r == SH2Z_SCACHE_HIT;
        if (!draw && r != SH2Z_SCACHE_REFUSED && car_has_cell[c->id]) {
            uint32_t again = 0;
            if (sh2z_scache_get(&pool, car_cell[c->id], cell_bytes(car_cell[c->id]), false, &again) == SH2Z_SCACHE_HIT) {
                pick.cell = car_cell[c->id]; slot = again; draw = true; ++fzx_e1_cache[4];
            }
        }
        if (!draw) { ++fzx_field_draw[4]; continue; }
        if (sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], racer_set, &pick, &items[i].place, 0u, racer_pmod)) {
            const sh2z_rspr_cell_t *cell = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1) + pick.cell;
            commands[out].cmd_srca = (uint16_t)(pool.slots[slot].addr >> 3);
            commands[out].cmd_colr = (uint16_t)((clut_vram + (uint32_t)cell->clut * 32u) >> 3);
            car_cell[c->id] = pick.cell; car_has_cell[c->id] = 1;
            if (c->id == 0) { fzx_e1_sprite_pos[0] = (uint32_t)(int32_t)items[i].place.x; fzx_e1_sprite_pos[1] = (uint32_t)(int32_t)items[i].place.y; }
            ++out; ++sprites;
        } else ++fzx_field_draw[3];
    }
    fzx_e1_cache[0] = pool.hits; fzx_e1_cache[1] = pool.misses; fzx_e1_cache[2] = pool.evictions; fzx_e1_cache[3] = pool.refused;
    fzx_field_draw[0] = sprites; fzx_field_draw[1] = solid; fzx_field_draw[2] = shadows; fzx_field_draw[5] = out - 1u;
#if E2_CULL
    fzx_e2_report[2] = sprites + solid; if (sprites + solid > fzx_e2_report[3]) fzx_e2_report[3] = sprites + solid;
    fzx_e2_report[4] += c_behind; fzx_e2_report[5] += c_far; fzx_e2_report[6] += c_off; fzx_e2_report[7] = shadows;
    fzx_e2_report[8] += c_behind + c_far + c_off;
#endif
    fzx_e1_report[10] = out - 1u;
    return out > 1u ? out : 0u;
}
#endif

/* ---- HUD ------------------------------------------------------------------------ */
void fzx_drive_hud(void) {
    const uint32_t kmh = (uint32_t)(((int64_t)(body.speed < 0 ? -body.speed : body.speed) * 216) / (65536 * 10));
    const uint32_t ms = body.race_time > 0 ? (uint32_t)body.race_time : 0u;
    const uint32_t laps = (uint32_t)course.track.laps;
    uint32_t lap = (uint32_t)(body.laps_done > 0 ? body.laps_done : 1);
    if (lap > laps) lap = laps;
    dbgio_printf("\x1b[2;2H%4lu KM/H  LAP %lu/%lu  %lu'%02lu\"%02lu ", (unsigned long)kmh, (unsigned long)lap,
                 (unsigned long)laps, (unsigned long)(ms / 60000u), (unsigned long)((ms / 1000u) % 60u),
                 (unsigned long)((ms / 10u) % 100u));
    if (body.flags & SH2Z_VD_FINISHED) dbgio_printf("\x1b[4;14HFINISH");
    else if (body.intro > 0) dbgio_printf("\x1b[4;14H%s", body.intro > 180 ? "      " : body.intro > 120 ? "  3   " : body.intro > 60 ? "  2   " : "  1   ");
    else dbgio_printf("\x1b[4;14H      ");
    dbgio_flush();
}
