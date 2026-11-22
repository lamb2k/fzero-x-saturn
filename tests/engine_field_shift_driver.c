/* HOST driver for tests/test_engine_field_shift.py (C-127 finding 3). The statics, the pose functions and the pack's
 * station block are the text of saturn/engine/engine_field.c as it stands (or as it was before the fix), included from
 * the files the test extracts; the library calls they make are stubbed here the same way for both. Prints one hash per
 * case so the old and the new text can be compared, and checks the new text's refusals itself. */
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sh2zam/line_follower_experimental.h"

#include "field_statics.inc"   /* enum, station_t, nodes, stations, line (extracted) */
static int32_t left_sign = 1, hover = 0;
volatile uint32_t fzx_field_report[16];
volatile uint32_t fzx_e2_report[16];

/* Library stubs: the same exact-ish arithmetic for both texts. */
bool sh2z_vec3_normalize_u30(int32_t out[3], const int32_t v[3]) {
    const double x = v[0], y = v[1], z = v[2], n = sqrt(x * x + y * y + z * z);
    if (n < 1.0) return false;
    const double k = 1073741824.0 / n;
    out[0] = (int32_t)lround(x * k); out[1] = (int32_t)lround(y * k); out[2] = (int32_t)lround(z * k);
    return true;
}
static void sh2z_sincos12_u30(uint32_t a, int32_t *s, int32_t *c) {
    const double r = (double)(a & 4095u) * 6.283185307179586 / 4096.0;
    *s = (int32_t)lround(sin(r) * 1073741824.0); *c = (int32_t)lround(cos(r) * 1073741824.0);
}

#include "field_funcs_a.inc"   /* q14 .. tube_at (and stations_ok in the fixed text) */
#include "field_funcs_b.inc"   /* line_pose, line_pose_fast */

static uint32_t pack_buf[(16u * 64u + 32u * 64u) / 4u + 4u];
static bool pack_block(void) {
#include "field_pack.inc"
    return true;
}

static int32_t shl_pos(int32_t v) { return (int32_t)((uint32_t)v << 12); } /* the pack's Q4 -> Q16, unsigned */
static uint64_t rs = 88172645463325252ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }
static int32_t rnds(int32_t lim) { return (int32_t)(rnd() % (2u * (uint32_t)lim + 1u)) - lim; } /* -lim .. lim */
static uint64_t fnv(uint64_t h, uint32_t x) { for (int i = 0; i < 4; ++i) { h ^= (x >> (8 * i)) & 255u; h *= 1099511628211ull; } return h; }

#define COUNT 64u
static void make_line(void) {
    uint32_t start = 0;
    for (uint32_t i = 0; i < COUNT; ++i) {
        const uint32_t span = 256u + (rnd() % 8192u);
        nodes[i].start = (int32_t)start; nodes[i].lateral = 0; nodes[i].speed_cap = 960; nodes[i].flags = 0;
        const uint32_t k = rnd() % 3u;
        nodes[i].flags = k == 1 ? 0x0080u : k == 2 ? 0x00c0u : 0u;   /* road, pipe, cylinder */
        nodes[i].half_width = (uint16_t)(1u + rnd() % 400u);
        nodes[i].inv_span = (uint32_t)(4294967296ull / span);
        for (int c = 0; c < 3; ++c) {
            stations[i].pos[c] = rnds(1 << 26);
            stations[i].fwd[c] = (int16_t)rnds(16384); stations[i].up[c] = (int16_t)rnds(16384);
#if E2_FASTPOSE
            stations[i].left[c] = (int16_t)rnds(16384);
#endif
        }
        start += span;
    }
    line.nodes = nodes; line.count = COUNT; line.length = (int32_t)start;
}

static uint64_t pose_case(const sh2z_track_state_t *t) {
    uint64_t h = 1469598103934665603ull;
    int32_t pos[3], f[3], u[3], l[3];
    line_pose(t, pos, f, u, l);
    for (int c = 0; c < 3; ++c) { h = fnv(h, (uint32_t)pos[c]); h = fnv(h, (uint32_t)f[c]); h = fnv(h, (uint32_t)u[c]); h = fnv(h, (uint32_t)l[c]); }
#if E2_FASTPOSE
    int16_t rows[3][3]; int32_t p2[3];
    line_pose_fast(t, p2, rows);
    for (int c = 0; c < 3; ++c) { h = fnv(h, (uint32_t)p2[c]); for (int r = 0; r < 3; ++r) h = fnv(h, (uint32_t)(uint16_t)rows[r][c]); }
#endif
    return h;
}

static sh2z_track_state_t at(uint32_t n, int32_t lateral) {
    sh2z_track_state_t t; memset(&t, 0, sizeof(t));
    const int32_t span = (int32_t)(4294967296ull / nodes[n].inv_span);
    t.node = n; t.dist = nodes[n].start + (int32_t)(rnd() % (uint32_t)span); t.lateral = lateral;
    return t;
}

int main(void) {
    make_line();
    uint64_t h = 1469598103934665603ull; uint32_t neg = 0, cases = 0;
    for (uint32_t k = 0; k < 4000u; ++k) {
        left_sign = (rnd() & 1u) ? 1 : -1; hover = rnds(1 << 18);
        const uint32_t n = rnd() % COUNT;
        const int32_t lat = rnds((int32_t)nodes[n].half_width * 256);
        neg += lat < 0; ++cases;
        sh2z_track_state_t t = at(n, lat);
        h = fnv(h, (uint32_t)pose_case(&t)); h = fnv(h, (uint32_t)(pose_case(&t) >> 32));
    }
    printf("pose cases=%u negative_lateral=%u hash=%016llx\n", cases, neg, (unsigned long long)h);
    /* The lateral's range ends: +-2^23 - 1 and -2^23 are exact in Q16; beyond them the fixed text saturates. */
    left_sign = 1; hover = 0;
    const int32_t ends[] = { 0x7fffff, -0x800000, 0x800000, -0x800001, 0x1000000, -0x1000000, 0x7fffffff, INT32_MIN };
    for (unsigned i = 0; i < sizeof(ends) / sizeof(ends[0]); ++i) {
        rs = 12345u;   /* the same dist each time */
        sh2z_track_state_t t = at(5, ends[i]);
        printf("lateral %d hash=%016llx\n", ends[i], (unsigned long long)pose_case(&t));
    }
    /* The pack's station block: random packs inside the Q4 range, then the range ends. */
    uint64_t hp = 1469598103934665603ull; unsigned ok = 0;
    for (unsigned k = 0; k < 200u; ++k) {
        make_line();
        uint8_t *raw = (uint8_t *)pack_buf; memset(raw, 0, sizeof(pack_buf));
        typedef struct { int32_t pos[3]; int16_t fwd[3], up[3], left[3]; uint16_t pad; } pose_t;
        sh2z_line_node_t *pn = (sh2z_line_node_t *)(void *)raw;
        pose_t *pp = (pose_t *)(void *)(raw + 16u * COUNT);
        for (uint32_t i = 0; i < COUNT; ++i) {
            pn[i] = nodes[i];
            if (i) pn[i].start += (int32_t)(rnd() % 31u) - 15;          /* the pack's starts differ a little */
            pn[i].lateral = (int16_t)rnds(200);
            for (int c = 0; c < 3; ++c) {
                pp[i].pos[c] = rnds(0x7ffff);                            /* Q4, both signs, inside +-2^19 */
                pp[i].fwd[c] = (int16_t)rnds(16384); pp[i].up[c] = (int16_t)rnds(16384); pp[i].left[c] = (int16_t)rnds(16384);
            }
        }
        for (int c = 0; c < 3; ++c) stations[0].pos[c] = shl_pos(pp[0].pos[c]) + rnds(3000);   /* within a unit of the pack's */
        memset((void *)fzx_e2_report, 0, sizeof(fzx_e2_report)); memset((void *)fzx_field_report, 0, sizeof(fzx_field_report));
        const bool r = pack_block();
        ok += r;
        hp = fnv(hp, r);
        for (uint32_t i = 0; i < COUNT; ++i) for (int c = 0; c < 3; ++c) { hp = fnv(hp, (uint32_t)stations[i].pos[c]); hp = fnv(hp, (uint32_t)(uint16_t)stations[i].fwd[c]); hp = fnv(hp, (uint32_t)(uint16_t)stations[i].up[c]); }
        hp = fnv(hp, fzx_e2_report[1]); hp = fnv(hp, fzx_e2_report[9]); hp = fnv(hp, fzx_field_report[0]);
    }
    printf("pack packs=200 accepted=%u hash=%016llx\n", ok, (unsigned long long)hp);
    /* Out-of-range positions: |Q4| = 2^19 - 1 and -2^19 load; 2^19, -2^19 - 1 and the int32 ends are refused by the fixed text. */
    const int32_t q4[] = { 0x7ffff, -0x80000, 0x80000, -0x80001, INT32_MAX, INT32_MIN };
    for (unsigned i = 0; i < sizeof(q4) / sizeof(q4[0]); ++i) {
        make_line();
        uint8_t *raw = (uint8_t *)pack_buf; memset(raw, 0, sizeof(pack_buf));
        typedef struct { int32_t pos[3]; int16_t fwd[3], up[3], left[3]; uint16_t pad; } pose_t;
        sh2z_line_node_t *pn = (sh2z_line_node_t *)(void *)raw;
        pose_t *pp = (pose_t *)(void *)(raw + 16u * COUNT);
        for (uint32_t k = 0; k < COUNT; ++k) pn[k] = nodes[k];
        pp[17].pos[1] = q4[i];
        for (int c = 0; c < 3; ++c) stations[0].pos[c] = 0;
        memset((void *)fzx_e2_report, 0, sizeof(fzx_e2_report)); memset((void *)fzx_field_report, 0, sizeof(fzx_field_report));
        const bool r = pack_block();
        printf("q4 %d loads=%d status=%u/%u\n", q4[i], (int)r, (unsigned)fzx_field_report[0], (unsigned)fzx_e2_report[0]);
    }
#ifdef NEW_TEXT
    /* stations_ok: adjacent stations (and the last with the first) within int32, no node wider than 0x7fff. */
    make_line();
    for (uint32_t i = 0; i < COUNT; ++i) for (int c = 0; c < 3; ++c) stations[i].pos[c] = 0;
    for (uint32_t i = 0; i < COUNT; ++i) nodes[i].half_width = 10;
    printf("ok flat=%d\n", (int)stations_ok());
    stations[4].pos[1] = INT32_MAX; stations[5].pos[1] = 0;
    printf("ok span INT32_MAX (3->4, 4->5)=%d\n", (int)stations_ok());
    stations[3].pos[1] = INT32_MAX;
    printf("ok span 0 between big pair=%d\n", (int)stations_ok());
    for (uint32_t i = 0; i < COUNT; ++i) stations[i].pos[1] = 0;
    stations[10].pos[0] = INT32_MAX; stations[11].pos[0] = -1;
    printf("ok span -2^31 (10->11)=%d\n", (int)stations_ok());          /* -1 - INT32_MAX = INT32_MIN: fits */
    stations[11].pos[0] = -2;
    printf("ok span -2^31-1 (10->11)=%d\n", (int)stations_ok());
    stations[10].pos[0] = 0; stations[11].pos[0] = 0;
    stations[20].pos[1] = -1; stations[21].pos[1] = INT32_MAX;
    printf("ok span 2^31 (20->21)=%d\n", (int)stations_ok());
    stations[20].pos[1] = 0; stations[21].pos[1] = 0;
    stations[COUNT - 1].pos[2] = INT32_MIN; stations[0].pos[2] = 1;
    printf("ok span 2^31+1 wrap (last->0)=%d\n", (int)stations_ok());
    stations[COUNT - 1].pos[2] = 0; stations[0].pos[2] = 0;
    nodes[9].half_width = 0x7fff; printf("ok width 0x7fff=%d\n", (int)stations_ok());
    nodes[9].half_width = 0x8000; printf("ok width 0x8000=%d\n", (int)stations_ok());
    nodes[9].half_width = 0xffff; printf("ok width 0xffff=%d\n", (int)stations_ok());
#endif
    return 0;
}
