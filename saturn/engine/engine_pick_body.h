/* The engine's track pick and stable chunk cap (docs/ENGINE_PLAN.md, E0), moved
 * verbatim out of engine_main.c so HOST tools build the same code
 * (tools/e0_pick_host.c). Not a normal header: include it once, in one
 * translation unit, after defining or declaring what it uses: path
 * (fzx_engine_path_t), fzx_source_road_plan, fzx_e0_report[16],
 * cpu_frt_count_get(), and with FZX_ROAD_STREAM_PHASES fzx_rsp_begin(). */
#ifndef FZX_ENGINE_PICK_BODY_H
#define FZX_ENGINE_PICK_BODY_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "engine_path.h"
#include "sh2zam/segmesh_experimental.h"

/* ---- track pick: runs on the slave SH-2, ahead of the road build ---------- */
#ifndef E0_POP_LOG
#define E0_POP_LOG 0
#endif
#if E0_POP_LOG
/* Diagnostic (E0_POP_LOG=1): per built frame, the chunks the pick marked and
 * the chunks drawn after the cap, as 320-bit maps; the master copies them into
 * the frame's row slot. */
enum { POP_WORDS = 10u, POP_ROWS = 4608u };
static uint32_t pop_marked[POP_WORDS], pop_drawn[POP_WORDS];
uint32_t fzx_e0_pop[POP_ROWS][2u * POP_WORDS];
static void pop_mark(uint32_t *map, const sh2z_segmesh_run_t *r, uint32_t from, uint32_t to) {
    for (unsigned i = 0; i < POP_WORDS; ++i) map[i] = 0;
    const uint32_t n = path.chunks;
    for (uint32_t k = from; k < to; ++k) {
        const uint32_t span = r[k].start == r[k].end ? n : (uint32_t)((r[k].end + n - r[k].start) % n);
        for (uint32_t j = 0, c = r[k].start; j < span; ++j, c = c + 1u < n ? c + 1u : 0u)
            if (c < 32u * POP_WORDS) map[c >> 5] |= 1u << (c & 31u);
    }
}
#endif
static const fzx_engine_cam_t *volatile job_cam;
#if FZX_PHYS_TUBES || ENG_LOOK_OUTER
static volatile uint8_t job_outer = 1u; /* the road plan's outer bit for the job (tube courses; ENG_LOOK_OUTER) */
#endif
#if ENG_LOOK_FOG
static volatile uint8_t job_fog[3]; /* ENG_LOOK_FOG: the course fog for the job, darker inside pipes and tunnels */
#endif
static volatile uint32_t job_tick;
static int32_t pick_depth[FZX_EPATH_MAX_CHUNKS + 1u];
static int8_t pick_state[FZX_EPATH_MAX_CHUNKS + 1u];
static sh2z_segmesh_run_t pick_runs[REF_ROAD_GROUPS];
volatile uint32_t fzx_e0_pick[6]; /* result, runs, marked, bad view, FRT ticks, calls */
_Static_assert(sizeof(sh2z_segmesh_anchor_t) == sizeof(fzx_engine_anchor_t), "anchor layout");

#ifndef E0_CAP_STABLE
#define E0_CAP_STABLE 0
#endif
#if E0_CAP_STABLE
#if !E0_CHUNK_CAP
#error E0_CAP_STABLE needs E0_CHUNK_CAP (the N it keeps)
#endif
/* E0_CAP_STABLE (with E0_CHUNK_CAP = N): rank the marked chunks by pick depth
 * and keep the nearest, with hysteresis: a newly marked chunk is kept when it
 * ranks inside N; a kept chunk is dropped only when it ranks N+2 or farther; a
 * dropped chunk returns after ranking inside N-2 for 4 frames in a row. At
 * most N+2 are kept (farthest dropped first). Runs are split only around
 * dropped chunks, keeping the pick's far-to-near order. */
enum { CAP_LIST = 128u };
static uint32_t cap_last[FZX_EPATH_MAX_CHUNKS], cap_frame;
static uint8_t cap_kept[FZX_EPATH_MAX_CHUNKS], cap_wait[FZX_EPATH_MAX_CHUNKS];
static uint16_t cap_list[CAP_LIST];
static int32_t cap_depth[CAP_LIST];
volatile uint32_t fzx_e0_cap_dropped;
static uint32_t run_span(const sh2z_segmesh_run_t *r, uint32_t n) {
    return r->start == r->end ? n : (uint32_t)((r->end + n - r->start) % n);
}
/* Returns false when the frame does not fit the method (then nothing is dropped). */
static bool cap_stable(const fzx_engine_pick_t *pv, uint32_t runs, RefRoadPlan *out) {
    const uint32_t n = path.chunks, N = E0_CHUNK_CAP;
    const int32_t *d = &pv->rows[12];
    uint32_t m = 0;
    ++cap_frame;
    for (uint32_t k = 0; k < runs; ++k)
        for (uint32_t j = 0, c = pick_runs[k].start, s = run_span(&pick_runs[k], n); j < s; ++j, c = c + 1u < n ? c + 1u : 0u) {
            if (m >= CAP_LIST) return false;
            const fzx_engine_anchor_t *a = &path.anchors[c];
            const int32_t depth = d[0] * a->x + d[1] * a->y + d[2] * a->z + d[3];
            uint32_t i = m++;
            while (i && cap_depth[i - 1u] > depth) { cap_depth[i] = cap_depth[i - 1u]; cap_list[i] = cap_list[i - 1u]; --i; }
            cap_depth[i] = depth; cap_list[i] = (uint16_t)c;
        }
    uint32_t kept = 0;
    for (uint32_t r = 0; r < m; ++r) {
        const uint32_t c = cap_list[r];
        if (cap_last[c] + 1u != cap_frame) { cap_kept[c] = r < N; cap_wait[c] = 0; }
        else if (cap_kept[c]) { if (r >= N + 2u) { cap_kept[c] = 0; cap_wait[c] = 0; } }
        else if (r + 2u < N) { if (++cap_wait[c] >= 4u) { cap_kept[c] = 1; cap_wait[c] = 0; } }
        else cap_wait[c] = 0;
        cap_last[c] = cap_frame;
        kept += cap_kept[c];
    }
    for (uint32_t r = m; r-- > 0 && kept > N + 2u;) if (cap_kept[cap_list[r]]) { cap_kept[cap_list[r]] = 0; --kept; }
    fzx_e0_cap_dropped += m - kept;
    /* Groups: each run's kept stretches, in the run's drawing order. */
    uint32_t groups = 0, visible = 0;
    for (uint32_t k = 0; k < runs; ++k) {
        const sh2z_segmesh_run_t *pr = &pick_runs[k];
        const uint32_t s = run_span(pr, n);
        uint16_t seg[2][32]; uint32_t ns = 0;
        for (uint32_t j = 0, c = pr->start; j < s;) {
            if (!cap_kept[c]) { ++j; c = c + 1u < n ? c + 1u : 0u; continue; }
            const uint32_t a = c, j0 = j;
            while (j < s && cap_kept[c]) { ++j; c = c + 1u < n ? c + 1u : 0u; }
            if (ns >= 32u) return false;
            seg[0][ns] = (uint16_t)a; seg[1][ns] = (uint16_t)(j - j0); ++ns;
        }
        if (groups + ns > REF_ROAD_GROUPS) return false;
        for (uint32_t q = 0; q < ns; ++q) {
            /* direction +1 draws from the end backward: emit the highest stretch first. */
            const uint32_t i = pr->direction == 1 ? ns - 1u - q : q;
            RefRoadGroup *g = &out->groups[groups++];
            g->start = seg[0][i];
            g->end = seg[1][i] == n ? seg[0][i] : (uint16_t)((seg[0][i] + seg[1][i]) % n);
            g->direction = pr->direction;
            g->average_depth = (float)pr->average / (float)(1u << pv->depth_shift);
            visible += seg[1][i];
        }
    }
#if E0_POP_LOG
    for (unsigned i = 0; i < POP_WORDS; ++i) pop_drawn[i] = 0;
    for (uint32_t r = 0; r < m; ++r) { const uint32_t c = cap_list[r]; if (cap_kept[c] && c < 32u * POP_WORDS) pop_drawn[c >> 5] |= 1u << (c & 31u); }
#endif
    out->count = groups; out->visible_chunks = visible;
    return true;
}
#endif

bool fzx_engine_slave_pick(void) {
    RefRoadPlan *out = &fzx_source_road_plan;
    const fzx_engine_consts_t *k = &path.consts;
    const uint16_t t0 = cpu_frt_count_get();
    fzx_engine_pick_t pv;
    out->faults = 0; out->count = 0; out->visible_chunks = 0;
#if FZX_PHYS_TUBES || ENG_LOOK_OUTER
    out->outer = job_outer; /* 0 inside a pipe (ENG_LOOK_OUTER: or a tunnel): its outer shell is not drawn */
#else
    out->outer = 1; /* the camera is outside the track surface on this course */
#endif
    out->fog_start = k->fog_start;
    memcpy(out->fog, k->fog, 4);
#if ENG_LOOK_FOG
    out->fog[0] = job_fog[0]; out->fog[1] = job_fog[1]; out->fog[2] = job_fog[2];
#endif
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
#if E0_POP_LOG
    pop_mark(pop_marked, pick_runs, 0u, runs);
#endif
#if E0_CAP_STABLE
    if (cap_stable(&pv, runs, out)) {
        fzx_e0_pick[4] = (uint16_t)(cpu_frt_count_get() - t0);
#if FZX_ROAD_STREAM_PHASES
        fzx_rsp_begin();
#endif
        return true;
    }
#endif
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
#if E0_POP_LOG
    pop_mark(pop_drawn, pick_runs, first_run, runs);
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
#endif
