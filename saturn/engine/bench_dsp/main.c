/* SCU DSP micro-bench (DSP lane, 2026-11-19): a batched 3x3 + translation
 * transform over N vectors on the SCU DSP (SH2ZAM dsp-1) against the master
 * SH-2. Standalone image: no game data, no ROM assets. EMULATOR tier; the
 * DSP's DMA and bus sharing are whatever the emulator charges (MODELLED).
 *
 * Results: dspb_rows[] (one row per N), dspb_once[]; the headless harness
 * dumps work RAM and reads them by symbol. FRT at clock / 8 (one tick = 8
 * SH-2 cycles). Each figure is the minimum over DSPB_REPS runs.
 *
 * DSPB_CORRUPT=1 (control): one kernel word altered after load; the
 * comparison columns must show mismatches. */
#include <yaul.h>
#include <stdint.h>
#include <string.h>
#include "sh2zam/dsp_experimental.h"
#include "sh2zam/view_experimental.h"

#ifndef DSPB_CORRUPT
#define DSPB_CORRUPT 0
#endif
#define DSPB_REPS 5u
#define DSPB_MAXN 320u

/* Harness interface (as the engine image): status 1 running, 2 done. */
#define USED __attribute__((used))
USED volatile uint32_t fzx_e0_status, fzx_e0_done, fzx_e0_fault, fzx_e0_hold_tick, fzx_input_count;
USED volatile uint32_t fzx_e0_report[16];
USED volatile uint32_t fzx_slave_road_status;

enum { COL_N, COL_C, COL_VIEW, COL_PROJ, COL_START, COL_WALL, COL_FINISH, COL_MISS_CPU, COL_MISS_VIEW,
       COL_OVL_ALONE, COL_OVL_BOTH, COL_OVL_MISS, COL_BLOCKS, COL_PAD, COLS = 16 };
static const uint32_t sizes[] = {4, 8, 12, 16, 20, 24, 32, 40, 64, 128, 256, 320};
#define ROWS (sizeof(sizes) / sizeof(sizes[0]))
USED volatile uint32_t dspb_rows[ROWS][COLS];
/* [0] magic, [1] rows, [2] program load ticks, [3] 64 PDD writes ticks, [4] corrupt build, [5] program words,
 * [6] 64 PPAF reads ticks, [7] purge of 320 vectors ticks, [8] one floor's band corners (C) ticks,
 * [9] 29 rivals' cull dots (C) ticks. */
USED volatile uint32_t dspb_once[16];

static int32_t in32[DSPB_MAXN * 3] __attribute__((aligned(16)));
static int32_t out_dsp[DSPB_MAXN * 3] __attribute__((aligned(16)));
static int32_t out_cpu[DSPB_MAXN * 3] __attribute__((aligned(16)));
static int32_t work_in[DSPB_MAXN * 3] __attribute__((aligned(16)));
static int32_t work_out[DSPB_MAXN * 3] __attribute__((aligned(16)));
static sh2z_pos16_t pos16[DSPB_MAXN];
static sh2z_view_vertex_t vview[DSPB_MAXN], vproj[DSPB_MAXN];
static sh2z_dsp_xform_t job __attribute__((aligned(16)));
static int32_t m[3][4];
static sh2z_view_t view;

static inline uint16_t now(void) { return cpu_frt_count_get(); }

/* The plain C transform the master would run: N vectors, results AoS. */
static void __attribute__((noinline)) cpu_transform(const int32_t mm[3][4], const int32_t *in, int32_t *out, uint32_t n) {
    for (uint32_t k = 0; k < n; ++k) {
        const int32_t x = in[3 * k], y = in[3 * k + 1], z = in[3 * k + 2];
        out[3 * k] = mm[0][0] * x + mm[0][1] * y + mm[0][2] * z + mm[0][3];
        out[3 * k + 1] = mm[1][0] * x + mm[1][1] * y + mm[1][2] * z + mm[1][3];
        out[3 * k + 2] = mm[2][0] * x + mm[2][1] * y + mm[2][2] * z + mm[2][3];
    }
}

static uint32_t seed = 12345u;
static uint32_t rnd(void) { seed = seed * 1664525u + 1013904223u; return seed >> 8; }

static void setup(void) {
    /* A Q1.14 rotation (yaw 30 deg, pitch 10 deg) and a view 30000 units ahead. */
    static const int16_t r[3][3] = {{14189, 0, 8192}, {1423, 16135, -2464}, {-8068, 2845, 13974}};
    memset(&view, 0, sizeof(view));
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) view.r[i][j] = r[i][j];
    view.t[0] = 300 * 16384; view.t[1] = -200 * 16384; view.t[2] = 30000 * 16384;
    view.focal_x = view.focal_y = 256 << 16; view.center_x = 160; view.center_y = 112;
    view.near_z = 16384; view.far_z = 120000 * 16384;
    view.clip[0] = 0; view.clip[1] = 0; view.clip[2] = 319; view.clip[3] = 223;
    if (!sh2z_view_init(&view, &view)) { fzx_e0_status = 30; for (;;) {} }
    for (unsigned i = 0; i < 3; ++i) { for (unsigned j = 0; j < 3; ++j) m[i][j] = r[i][j]; m[i][3] = view.t[i]; }
    for (uint32_t k = 0; k < DSPB_MAXN; ++k) {
        pos16[k].x = (int16_t)(rnd() % 4001u) - 2000; pos16[k].y = (int16_t)(rnd() % 1001u) - 500;
        pos16[k].z = (int16_t)(rnd() % 4001u) - 2000;
        in32[3 * k] = pos16[k].x; in32[3 * k + 1] = pos16[k].y; in32[3 * k + 2] = pos16[k].z;
        work_in[3 * k] = (int32_t)rnd(); work_in[3 * k + 1] = (int32_t)rnd(); work_in[3 * k + 2] = (int32_t)rnd();
    }
}

#define MIN_INTO(slot, expr) do { const uint16_t t0_ = now(); expr; const uint32_t d_ = (uint16_t)(now() - t0_); \
    if (d_ < (slot)) (slot) = d_; } while (0)

/* Reference copies of two candidate loops, timed on the SH-2 (once):
 * the floor's fog-band corners (src/fzx_floor_rbg.c, one frame: 2 halves x 7 bands x 4 corners x 3)
 * and the rival cull's camera-space dots (engine_drive.c cull_dot, 29 cars x 3 rows). */
static int32_t band_pos[8][3], band_out[4][3];
static volatile int32_t band_sink;
static void __attribute__((noinline)) band_corners(void) {
    for (unsigned half = 0; half < 2; ++half) for (unsigned band = 1; band < 8; ++band) {
        const int32_t low = (int32_t)(2 * band - 1) * 65536 / 14, high = band == 7 ? 65536 : (int32_t)(2 * band + 1) * 65536 / 14;
        const int32_t fractions[4] = {low, low, high, high};
        const unsigned sides[4] = {0, 1, 1, 0};
        for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < 3; ++j) {
            const unsigned inner = half ? 4 + sides[i] : 2 + sides[i], edge = half ? 6 + sides[i] : sides[i];
            band_out[i][j] = band_pos[inner][j] + (int32_t)(((int64_t)band_pos[edge][j] - band_pos[inner][j]) * fractions[i] / 65536);
        }
        band_sink = band_out[3][2];
    }
}
static int16_t cam_r[3][3];
static int32_t car_d[29][3], car_t[29][3];
static int32_t cull_dot(const int16_t row[3], const int32_t d[3]) {
    const int64_t sum = (int64_t)row[0] * d[0] + (int64_t)row[1] * d[1] + (int64_t)row[2] * d[2];
    return (int32_t)(sum >> 16);
}
static void __attribute__((noinline)) cull_dots(void) {
    for (unsigned i = 0; i < 29; ++i) for (unsigned r = 0; r < 3; ++r) car_t[i][r] = cull_dot(cam_r[r], car_d[i]);
}

int main(void) {
    fzx_e0_report[0] = 0x45305246u;
    /* Referenced so section GC keeps every symbol the harness looks up. */
    fzx_e0_hold_tick = 0; fzx_input_count = 0; fzx_slave_road_status = 0; fzx_e0_fault = 0; fzx_e0_done = 0;
    fzx_e0_status = 1;
    cpu_frt_init(CPU_FRT_CLOCK_DIV_8);
    setup();
    dspb_once[0] = 0x44535042u; /* "DSPB" */
    dspb_once[4] = DSPB_CORRUPT;
    dspb_once[5] = sh2z_dsp_xform_program_words;
    {
        uint16_t t0 = now();
        if (!sh2z_dsp_xform_load()) { fzx_e0_status = 31; for (;;) {} }
        dspb_once[2] = (uint16_t)(now() - t0);
#if DSPB_CORRUPT
        /* Control: the pass-1 accumulate becomes a subtract (the host test's first mutation). */
        uint32_t p[SH2Z_DSP_PROGRAM_WORDS];
        memcpy(p, sh2z_dsp_xform_program, sh2z_dsp_xform_program_words * 4u);
        unsigned seen = 0;
        for (uint32_t i = 0; i < sh2z_dsp_xform_program_words; ++i)
            if (p[i] == 0x135D0000u && seen++ == 1u) p[i] ^= 1u << 26;
        sh2z_dsp_load(p, sh2z_dsp_xform_program_words);
#endif
        uint32_t words[64];
        t0 = now();
        sh2z_dsp_data_write(2, 0, words, 64);
        dspb_once[3] = (uint16_t)(now() - t0);
        t0 = now();
        for (unsigned i = 0; i < 64; ++i) (void)sh2z_dsp_busy();
        dspb_once[6] = (uint16_t)(now() - t0);
        t0 = now();
        sh2z_dsp_purge(out_dsp, DSPB_MAXN * 12u);
        dspb_once[7] = (uint16_t)(now() - t0);
    }
    {
        for (unsigned i = 0; i < 8; ++i) for (unsigned j = 0; j < 3; ++j) band_pos[i][j] = (int32_t)(rnd() % 200000000u) - 100000000;
        for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) cam_r[i][j] = (int16_t)m[i][j];
        for (unsigned i = 0; i < 29; ++i) for (unsigned j = 0; j < 3; ++j) car_d[i][j] = (int32_t)(rnd() % 40000000u) - 20000000;
        uint32_t b = 0xffffu, c = 0xffffu;
        for (unsigned rep = 0; rep < DSPB_REPS; ++rep) { MIN_INTO(b, band_corners()); MIN_INTO(c, cull_dots()); }
        dspb_once[8] = b; dspb_once[9] = c;
    }
    for (unsigned row = 0; row < ROWS; ++row) {
        const uint32_t n = sizes[row];
        volatile uint32_t *r = dspb_rows[row];
        uint32_t c = 0xffffu, v = 0xffffu, pr = 0xffffu, st = 0xffffu, wall = 0xffffu, fin = 0xffffu, alone = 0xffffu, both = 0xffffu;
        uint32_t miss_cpu = 0, miss_view = 0, miss_ovl = 0;
        for (unsigned rep = 0; rep < DSPB_REPS; ++rep) {
            MIN_INTO(c, cpu_transform((const int32_t (*)[4])m, in32, out_cpu, n));
            MIN_INTO(v, sh2z_view_batch_divu(vview, &view, pos16, sizeof(pos16[0]), n));
            for (uint32_t k = 0; k < n; ++k) { vproj[k] = vview[k]; }
            MIN_INTO(pr, sh2z_view_project_divu(vproj, &view, vproj, n));
            /* DSP: master cost to start, wall time to the end, master cost to collect. */
            memset(out_dsp, 0x5a, sizeof(out_dsp));
            const uint16_t t0 = now();
            if (!sh2z_dsp_xform_prepare(&job, (const int32_t (*)[4])m, in32, out_dsp, n)) { fzx_e0_status = 32; for (;;) {} }
            sh2z_dsp_xform_start(&job);
            const uint16_t t1 = now();
            while (sh2z_dsp_busy()) {}
            const uint16_t t2 = now();
            const bool ok = sh2z_dsp_xform_finish(&job, 0);
            const uint16_t t3 = now();
            if (!ok) { fzx_e0_status = 33; for (;;) {} }
            if ((uint16_t)(t1 - t0) < st) st = (uint16_t)(t1 - t0);
            if ((uint16_t)(t2 - t0) < wall) wall = (uint16_t)(t2 - t0);
            if ((uint16_t)(t3 - t2) < fin) fin = (uint16_t)(t3 - t2);
            if (rep == 0) {
                cpu_transform((const int32_t (*)[4])m, in32, out_cpu, n);
                for (uint32_t k = 0; k < n; ++k)
                    for (uint32_t i = 0; i < 3; ++i) {
                        const int32_t d = out_dsp[sh2z_dsp_xform_at(k, i)];
                        if (d != out_cpu[3 * k + i]) ++miss_cpu;
                        const int32_t w = i == 0 ? vview[k].x : i == 1 ? vview[k].y : vview[k].z;
                        if (d != w) ++miss_view;
                    }
            }
            /* Overlap: the master transforms another 320 vectors while the DSP runs this job. */
            MIN_INTO(alone, cpu_transform((const int32_t (*)[4])m, work_in, work_out, DSPB_MAXN));
            memset(out_dsp, 0x5a, sizeof(out_dsp));
            {
                const uint16_t s0 = now();
                sh2z_dsp_xform_prepare(&job, (const int32_t (*)[4])m, in32, out_dsp, n);
                sh2z_dsp_xform_start(&job);
                cpu_transform((const int32_t (*)[4])m, work_in, work_out, DSPB_MAXN);
                sh2z_dsp_xform_finish(&job, 0);
                const uint32_t d = (uint16_t)(now() - s0);
                if (d < both) both = d;
            }
            if (rep == 0)
                for (uint32_t k = 0; k < n; ++k)
                    for (uint32_t i = 0; i < 3; ++i)
                        if (out_dsp[sh2z_dsp_xform_at(k, i)] != out_cpu[3 * k + i]) ++miss_ovl;
        }
        r[COL_N] = n; r[COL_C] = c; r[COL_VIEW] = v; r[COL_PROJ] = pr; r[COL_START] = st; r[COL_WALL] = wall;
        r[COL_FINISH] = fin; r[COL_MISS_CPU] = miss_cpu; r[COL_MISS_VIEW] = miss_view;
        r[COL_OVL_ALONE] = alone; r[COL_OVL_BOTH] = both; r[COL_OVL_MISS] = miss_ovl; r[COL_BLOCKS] = job.blocks;
        dspb_once[1] = row + 1u;
    }
    fzx_e0_done = 1;
    fzx_e0_status = 2;
    for (;;) {}
}
