/* Engine race countdown (engine_countdown.h). Reads the player's body and the race camera; never writes them. */
#include <string.h>
#include "engine_countdown.h"
#include "countdown_draw_native.h"
#include "texture_cd.h"
#include "countdown_identity.h"

volatile uint32_t fzx_cdn_report[16];

typedef int64_t i64;

typedef struct {
    fzx_countdown_pack_t pack;
    fzx_countdown_native_work_t work;
    fzx_countdown_native_stats_t stats;
    fzx_countdown_phase_t phase;
    RefCountdown state;
    vdp1_cmdt_t cmds[FZX_CDN_COMMANDS] __attribute__((aligned(32)));
    uint8_t *vram;
    uint32_t vram_bytes;
} scene_t;
static scene_t *scene;

#define ALIGN32(x) (((x) + 31u) & ~31u)

uint32_t fzx_cdn_low_bytes(void) { return ALIGN32(FZX_COUNTDOWN_BYTES) + ALIGN32((uint32_t)sizeof(scene_t)) + 64u; }

static uint32_t fail(uint32_t status) { fzx_cdn_report[0] = status; scene = NULL; return 0; }

uint32_t fzx_cdn_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, uintptr_t low_base, uintptr_t *low_end,
                      void *scratch, uint32_t scratch_bytes, uint32_t floor_min) {
    scene = NULL;
    for (unsigned i = 0; i < 16u; ++i) fzx_cdn_report[i] = 0;
    const uint32_t bank = ALIGN32(FZX_COUNTDOWN_TEXTURE_BYTES);
    if (!parts || !low_end) return fail(2);
    const uintptr_t base = (uintptr_t)parts->texture_base;
    if ((low_base & 31u) || (base & 31u) || low_base < 0x00200000u) return fail(2);
    *low_end = low_base;
    if (ceiling < bank || ceiling - bank < floor_min) return fail(7);
    const uint32_t floor = (ceiling - bank) & ~31u;
    const uintptr_t meta = low_base, ctx = meta + ALIGN32(FZX_COUNTDOWN_BYTES), end = ctx + ALIGN32((uint32_t)sizeof(scene_t));
    if (end > 0x002ffe00u) return fail(2);
    scene_t *const s = (scene_t *)ctx;
    memset(s, 0, sizeof(*s));
    *low_end = end;
    fzx_cdn_report[11] = (uint32_t)meta; fzx_cdn_report[12] = (uint32_t)end;
    if (!fzx_texture_cd_load("COUNTDN.PAK", FZX_COUNTDOWN_BYTES, FZX_COUNTDOWN_HASH, (void *)meta, scratch, scratch_bytes)) return fail(3);
    if (!fzx_countdown_pack_open(&s->pack, (const void *)meta, FZX_COUNTDOWN_BYTES, FZX_COUNTDOWN_HASH, FZX_COUNTDOWN_TEXTURE_BYTES,
                                 FZX_COUNTDOWN_TEXTURE_HASH)) return fail(4);
    uint8_t *const vram = (uint8_t *)(base + floor);
    if (!fzx_texture_cd_load("COUNTDTX.PAK", FZX_COUNTDOWN_TEXTURE_BYTES, FZX_COUNTDOWN_TEXTURE_HASH, vram, scratch, scratch_bytes))
        return fail(5);
    if (!fzx_countdown_draw_native_prepare(&s->work, &s->pack)) return fail(6);
    s->vram = vram; s->vram_bytes = FZX_COUNTDOWN_TEXTURE_BYTES;
    scene = s;
    fzx_cdn_report[9] = (uint32_t)((uintptr_t)vram & 0x7ffffu); fzx_cdn_report[10] = FZX_COUNTDOWN_TEXTURE_BYTES;
    fzx_cdn_report[0] = 1;
    return floor;
}

/* ---- the sign's pose ------------------------------------------------------------------------------------------------ */
static uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}
static void cross30(int32_t o[3], const int32_t a[3], const int32_t b[3]) {
    o[0] = (int32_t)(((i64)a[1] * b[2] - (i64)a[2] * b[1]) >> 30);
    o[1] = (int32_t)(((i64)a[2] * b[0] - (i64)a[0] * b[2]) >> 30);
    o[2] = (int32_t)(((i64)a[0] * b[1] - (i64)a[1] * b[0]) >> 30);
}
/* Q30 unit vector from any Q30 vector; false for a null vector. */
static bool unit30(int32_t v[3]) {
    const uint32_t n = isqrt64((uint64_t)((i64)v[0] * v[0]) + (uint64_t)((i64)v[1] * v[1]) + (uint64_t)((i64)v[2] * v[2]));
    if (n < 1024u) return false;
    for (unsigned i = 0; i < 3u; ++i) v[i] = (int32_t)(((i64)v[i] << 30) / (i64)n);
    return true;
}
/* The original's distance variable (racer.c, race intro), Q16.16: 33 + 1500 s^2 with s = (timer - 220) / 240 above 220,
 * 33 from 220 down to 31, and s = (30 - timer) / 30 from 30 down to 1 (the sign flies off after GO). */
static int32_t sign_distance(int32_t timer) {
    i64 v = (i64)33 << 16;
    if (timer > 220) { const i64 n = timer - 220; v += (n * n * 5 * 65536) / 192; }
    else if (timer <= 30) { const i64 n = 30 - timer; v += (n * n * 5 * 65536) / 3; }
    return (int32_t)v;
}
/* The sign's model matrix, in RefCountdown's layout (row-major Q16.16, the model's axes as rows and its origin in the last row). */
static bool sign_matrix(int32_t m[16], int32_t timer, const fzx_engine_cam_t *cam, const sh2z_vd_body_t *body) {
    int32_t Y[3], X[3], Z[3], Xo[3];
    for (unsigned i = 0; i < 3u; ++i) { Y[i] = body->body[1][i]; X[i] = -cam->rotation[2][i]; }
    if (!unit30(Y)) return false;
    cross30(Z, Y, X);
    if (!unit30(Z)) return false;
    cross30(Xo, Z, Y);
    const int32_t *rows[3] = { Z, Y, Xo };
    const i64 v = sign_distance(timer), hag = body->height;
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 3u; ++c) m[r * 4u + c] = (int32_t)((((i64)rows[r][c] * 214748365ll) + (1ll << 43)) >> 44); /* 0.2 */
        m[r * 4u + 3u] = 0;
    }
    for (unsigned i = 0; i < 3u; ++i) {
        const i64 p = (i64)body->pos[i] + (((v - hag) * body->body[1][i]) >> 30) + (((i64)50 << 16) * body->body[0][i] >> 30)
                      - ((v * cam->rotation[2][i]) >> 30);
        m[12 + i] = (int32_t)p;
    }
    m[15] = 65536;
    return true;
}

unsigned fzx_cdn_stage(const fzx_view_t *view, const fzx_engine_cam_t *cam, const sh2z_vd_body_t *body) {
    scene_t *const s = scene;
    const int32_t timer = body->intro;
    if (!s || timer <= 0 || timer > 460) return 0;
    const uint16_t t0 = cpu_frt_count_get();
    ++fzx_cdn_report[13];
    if (!fzx_cdn_report[14]) fzx_cdn_report[14] = (uint32_t)timer;
    fzx_cdn_report[15] = (uint32_t)timer;
    memset(&s->state, 0, sizeof(s->state));
    s->state.timer = timer;
    uint32_t count = 0;
    bool ok = sign_matrix(s->state.matrix, timer, cam, body) && fzx_countdown_phase(timer, &s->phase) &&
              fzx_countdown_draw_native(&s->work, view, &s->state, &s->phase, s->vram, s->vram_bytes, s->cmds, FZX_CDN_COMMANDS, &count,
                                        &s->stats);
    if (!ok) { count = 0; ++fzx_cdn_report[4]; fzx_cdn_report[5] = s->stats.fault * 256u + s->stats.draw.fault; }
    else if (count) {
        ++fzx_cdn_report[1]; fzx_cdn_report[2] = count;
        if (count > fzx_cdn_report[3]) fzx_cdn_report[3] = count;
    }
    const uint32_t t = (uint16_t)(cpu_frt_count_get() - t0);
    fzx_cdn_report[7] = t; if (t > fzx_cdn_report[8]) fzx_cdn_report[8] = t;
    return count;
}
const vdp1_cmdt_t *fzx_cdn_commands(void) { return scene ? scene->cmds : NULL; }
void fzx_cdn_dropped(unsigned n) { fzx_cdn_report[6] += n; }
