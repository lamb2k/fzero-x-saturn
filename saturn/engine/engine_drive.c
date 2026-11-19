/* Engine line, milestone E1: the drive slice (see engine_drive.h). */
#include <string.h>
#include <stddef.h>
#include "engine_drive.h"
#include "physics_native.h"
#include "physics_identity.h"
#include "racer_sprites_identity.h"
#include "texture_cd.h"
#include "sh2zam/rotation_sprite_experimental.h"

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

static void step_once(sh2z_vd_input_t *in) {
    sh2z_vd_step(&body, &course.track, &course.vehicle, &fzx_phys_rules, in);
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
    const int32_t *f = body.body[0], *u = body.body[1];
    int32_t cf[3], cu[3], r0[3];
    for (unsigned i = 0; i < 3u; ++i) {
        cam->eye[i] = body.pos[i] + mul30(EYE_F, f[i]) + mul30(EYE_U, u[i]);
        cf[i] = mul30(PITCH_C, f[i]) - mul30(PITCH_S, u[i]);
        cu[i] = mul30(PITCH_S, f[i]) + mul30(PITCH_C, u[i]);
    }
    r0[0] = (int32_t)(((int64_t)cf[1] * cu[2] - (int64_t)cf[2] * cu[1]) >> 30);
    r0[1] = (int32_t)(((int64_t)cf[2] * cu[0] - (int64_t)cf[0] * cu[2]) >> 30);
    r0[2] = (int32_t)(((int64_t)cf[0] * cu[1] - (int64_t)cf[1] * cu[0]) >> 30);
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

/* ---- player sprite (claude/racer-sprites 45b74b7, the draw16 path) ----------- */
#define MODEL_SCALE_Q14 1638 /* the source's machine model scale 0.1, Q1.14 */
#define SHADOW_W 32u
#define SHADOW_H 16u
typedef struct {sh2z_rspr_set_t header;sh2z_rspr_cell_t cell;uint16_t clut[16];uint8_t pixels[SHADOW_W*SHADOW_H/2];} shadow_set_t;
static const sh2z_rspr_set_t *racer_set, *shadow_set;
static uint32_t racer_vram, shadow_vram;
static uint16_t racer_pmod, shadow_pmod;
static sh2z_rspr_camera_t racer_camera, shadow_camera;

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

uint32_t fzx_drive_load(const vdp1_vram_partitions_t *parts, void *scratch, uint32_t scratch_bytes) {
    fzx_e1_report[12] = FZX_PHYSICS_PACK_BYTES;
    if (!fzx_texture_cd_load("PHYS.PAK", FZX_PHYSICS_PACK_BYTES, FZX_PHYSICS_PACK_HASH, pack_buf, scratch, scratch_bytes))
        { fzx_e1_status = 2; return 0; }
    if (fzx_phys_load(&course, (const uint8_t *)pack_buf, FZX_PHYSICS_PACK_BYTES, 1) != 0) { fzx_e1_status = 3; return 0; }
    body = course.start; prev_n64 = 0; ticks = 0;
    memset(&pad_state, 0, sizeof(pad_state));
    /* Sprite set and shadow at the top of the texture partition (8-aligned). */
    const uint32_t shadow_bytes = (sizeof(shadow_set_t) + 7u) & ~7u;
    const uint32_t top = parts->texture_size & ~7u;
    if (top < FZX_RACER_SPRITE_BYTES + shadow_bytes + 8u) { fzx_e1_status = 4; return 0; }
    const uint32_t set_off = (top - shadow_bytes - FZX_RACER_SPRITE_BYTES) & ~7u, shadow_off = top - shadow_bytes;
    void *set = (void *)((uintptr_t)parts->texture_base + set_off);
    if (((uintptr_t)set & 7u) ||
        !fzx_texture_cd_load("RSPRITE.PAK", FZX_RACER_SPRITE_BYTES, FZX_RACER_SPRITE_HASH, set, scratch, scratch_bytes))
        { fzx_e1_status = 4; return 0; }
    racer_set = (const sh2z_rspr_set_t *)set; racer_vram = (uint32_t)((uintptr_t)set & 0x7ffffu);
    if (!sh2z_rspr_valid(racer_set)) { fzx_e1_status = 5; return 0; }
    if (!shadow_build((void *)((uintptr_t)parts->texture_base + shadow_off))) { fzx_e1_status = 6; return 0; }
    {
        vdp1_cmdt_t mode_cmd; memset(&mode_cmd, 0, sizeof(mode_cmd));
        const vdp1_cmdt_draw_mode_t mode = {.end_code_disable = 1, .user_clipping_mode = 2, .color_mode = VDP1_CMDT_CM_CLUT_16};
        vdp1_cmdt_draw_mode_set(&mode_cmd, mode); racer_pmod = mode_cmd.cmd_pmod;
        const vdp1_cmdt_draw_mode_t half = {.end_code_disable = 1, .user_clipping_mode = 2, .color_mode = VDP1_CMDT_CM_CLUT_16,
                                            .cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT};
        vdp1_cmdt_draw_mode_set(&mode_cmd, half); shadow_pmod = mode_cmd.cmd_pmod;
    }
    fzx_e1_report[9] = 1; fzx_e1_status = 1;
    return set_off;
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
    if (fzx_e1_status != 1 || capacity < 3u || !sprite_camera(view, cam)) { ++fzx_e1_report[11]; return 0; }
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
    {
        int32_t depth; sh2z_rspr_pick_t pick; sh2z_rspr_place_t place;
        if (sh2z_rspr_pose(&pick, &place, &depth, &racer_camera, racer_set, pos, basis) &&
            sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], racer_set, &pick, &place, racer_vram, racer_pmod)) ++out;
        else ++fzx_e1_report[11];
    }
    fzx_e1_report[10] = out - 1u;
    return out > 1u ? out : 0u;
}

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
