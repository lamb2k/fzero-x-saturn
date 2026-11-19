"""FZX_TRACK_BAKED: HOST checks of the native road-plan pick (C-045).

1. Cache identity (ref/road_plan_segmesh.inc): the anchor cache must be rebuilt
   whenever any chunk position of the ring changes, including an unsampled
   chunk in a same-storage reload, and must be reused across unchanged frames.
2. Fault output (saturn/track_pick.c with SH2ZAM segmesh-1): on an exhausted run
   pool the plan must match the source planner's fault output (faults = 2, the
   marked-chunk count published in visible_chunks, no groups), compared with a
   transcription of the source planner's loops.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SH2ZAM = ROOT / 'third_party' / 'SH2ZAM' / 'saturn'
CC = shutil.which('gcc') or shutil.which('cc')

CACHE_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float f32; typedef int s32; typedef unsigned int u32;
typedef struct { f32 m[4][4]; } MtxF;
typedef struct { struct { f32 x, y, z; } pos; } SegmentChunk;
#include "track_pick.h"
static SegmentChunk gSegmentChunks[9];
static SegmentChunk *sLastSegmentChunk;
static s32 gSegmentChunkCount;
static f32 sCourseFarRenderDistance = 5500.0f, D_800CF51C = 400.0f;
static unsigned begins, sets, picks; static short anchors[8][3];
int fzx_track_anchors_begin(unsigned n) { ++begins; return n >= 3u && n <= 8u; }
void fzx_track_anchor_set(unsigned i, short x, short y, short z) { ++sets; anchors[i][0] = x; anchors[i][1] = y; anchors[i][2] = z; }
int fzx_track_pick(RefRoadPlan *out, const FzxTrackPickView *v) { (void)out; (void)v; ++picks; return 1; }
#include "road_plan_segmesh.inc"
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); return 1; } } while (0)
static void ring(s32 n, f32 base) {
    for (s32 i = 0; i < n; i++) { gSegmentChunks[i].pos.x = base + 100.0f * (f32)i; gSegmentChunks[i].pos.y = 5.0f; gSegmentChunks[i].pos.z = 200.0f + 50.0f * (f32)i; }
    gSegmentChunks[n] = gSegmentChunks[0];
    gSegmentChunkCount = n; sLastSegmentChunk = &gSegmentChunks[n];
}
/* The port's ring-construction call site (ref_race_init after Course_Init) bumps
 * the generation; OLD_INC_CONTROL compiles the bump away for the 659906d control. */
#ifdef OLD_INC_CONTROL
#define RING_BUILT() ((void)0)
#else
#define RING_BUILT() (++ref_track_ring_generation)
#endif
int main(void) {
    MtxF m; RefRoadPlan out; memset(&m, 0, sizeof m);
    m.m[0][0] = 1.0f; m.m[1][1] = 1.0f; m.m[2][2] = 1.0f; m.m[2][3] = 1.0f; m.m[3][2] = -10.0f; m.m[3][3] = 1.0f;
    ring(5, 10.0f); RING_BUILT();
    /* Reuse across unchanged frames: one build, three picks. */
    for (int f = 0; f < 3; f++) CHECK(ref_road_plan_segmesh(&out, &m) == 1);
    CHECK(ref_track_pick_builds == 1u && picks == 3u && begins == 1u);
    /* C-045 probe as a same-storage reload: unsampled chunk 1, x 110 -> 120, same
     * storage and count, rebuilt by Course_Init (generation bump). */
    const short before = anchors[1][0];
    gSegmentChunks[1].pos.x += 10.0f; RING_BUILT();
    CHECK(ref_road_plan_segmesh(&out, &m) == 1);
    CHECK(ref_track_pick_builds == 2u);
    CHECK(anchors[1][0] != before);
    CHECK(ref_road_plan_segmesh(&out, &m) == 1 && ref_track_pick_builds == 2u);
    /* Same-storage reload with new contents (every chunk moved), same count. */
    ring(5, 40.0f); RING_BUILT();
    CHECK(ref_road_plan_segmesh(&out, &m) == 1 && ref_track_pick_builds == 3u);
    /* Same storage, different length: rebuilt even without a bump. */
    ring(7, 40.0f);
    CHECK(ref_road_plan_segmesh(&out, &m) == 1 && ref_track_pick_builds == 4u);
    /* Unchanged frames after all that reuse the build. */
    for (int f = 0; f < 3; f++) CHECK(ref_road_plan_segmesh(&out, &m) == 1);
    CHECK(ref_track_pick_builds == 4u);
#if FZX_TRACK_VERIFY
    /* Diagnostic: a ring change without a bump is counted and rebuilt. */
    gSegmentChunks[3].pos.z += 1.0f;
    CHECK(ref_road_plan_segmesh(&out, &m) == 1 && ref_track_pick_builds == 5u && ref_track_verify_mismatches == 1u);
    CHECK(ref_road_plan_segmesh(&out, &m) == 1 && ref_track_pick_builds == 5u && ref_track_verify_mismatches == 1u);
#endif
    CHECK(ref_track_pick_fallbacks == 0u);
    printf("cache ok: builds %u picks %u\n", ref_track_pick_builds, picks);
    return 0;
}
'''

FAULT_DRIVER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "track_pick.h"
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); return 1; } } while (0)
/* Transcription of the source planner (generated/road_plan/course.c, ref_road_plan):
 * chunk visibility, draw-state marking with the 30-unit step, grouping into a
 * 64-entry pool with the source fault when the pool is exhausted. Depth, x, y and w
 * come from the same rows the native pick uses. */
typedef struct { int64_t depth; int state; } chunk_t;
static void source_plan(RefRoadPlan *out, const short (*a)[3], unsigned n, const FzxTrackPickView *v) {
    chunk_t c[513]; memset(out, 0, sizeof *out);
    for (unsigned i = 0; i < n; i++) {
        const int *r = v->rows; int64_t p[3] = { a[i][0], a[i][1], a[i][2] };
        int64_t x = r[0]*p[0] + r[1]*p[1] + r[2]*p[2] + r[3], y = r[4]*p[0] + r[5]*p[1] + r[6]*p[2] + r[7];
        int64_t w = r[8]*p[0] + r[9]*p[1] + r[10]*p[2] + r[11], d = r[12]*p[0] + r[13]*p[1] + r[14]*p[2] + r[15];
        c[i].depth = d;
        if (d < 0 || v->thresholds[0] < d) c[i].state = 0;
        else { int64_t aw = w < 0 ? -w : w; c[i].state = (x < 0 ? -x : x) <= aw && (y < 0 ? -y : y) <= aw; }
    }
    c[n] = c[0];
    int last = 1; unsigned marked = 0;
    for (unsigned i = 0; i < n; i++) {
        if (c[i].state || c[i + 1].state) {
            int64_t s = c[i + 1].depth - c[i].depth;
            if (s < -v->thresholds[1]) last = -1; else if (s > v->thresholds[1]) last = 1;
            c[i].state = last; ++marked;
        } else c[i].state = 0;
    }
    out->visible_chunks = marked;            /* published before the pool check */
    if (!marked || marked == n) return;
    unsigned ch = n - 1;
    if (!c[ch].state) { ch = 0; while (!c[ch].state) ++ch; }
    else { --ch; while (c[ch].state) --ch; ++ch; }
    const unsigned parse = ch; unsigned groups = 0;
    do {
        const int64_t f = c[ch].depth; const int st = c[ch].state;
        for (;;) {
            if (++ch == n) ch = 0;
            int64_t delta = f - c[ch].depth;
            if (delta < -v->thresholds[2] || v->thresholds[2] < delta || ch == parse || st != c[ch].state) break;
        }
        if (++groups == REF_ROAD_GROUPS) { out->faults = 2; return; }
        while (!c[ch].state) if (++ch == n) ch = 0;
    } while (ch != parse);
}
int main(void) {
    enum { N = 300 };
    static short a[N][3];
    FzxTrackPickView v; memset(&v, 0, sizeof v);
    v.rows[11] = 1 << 28;                      /* w: everything inside x/y */
    v.rows[14] = 1;                            /* depth = z */
    v.thresholds[0] = 1 << 20; v.thresholds[1] = 1000; v.thresholds[2] = 10; v.thresholds[3] = -(1 << 30);
    v.depth_unit = 1.0f;
    for (unsigned i = 0; i < N; i++) { a[i][0] = 0; a[i][1] = 0; a[i][2] = (short)((i & 1) ? 0 : 1000); }
    a[0][2] = a[1][2] = -32768;               /* chunk 0 and its successor hidden: runs split */
    CHECK(fzx_track_anchors_begin(N) == 1);
    for (unsigned i = 0; i < N; i++) fzx_track_anchor_set(i, a[i][0], a[i][1], a[i][2]);
    RefRoadPlan native, source;
    memset(&native, 0, sizeof native);
    const int r = fzx_track_pick(&native, &v);
    source_plan(&source, (const short (*)[3])a, N, &v);
    CHECK(source.faults == 2);                /* the pool really is exhausted */
    CHECK(r == -2);
    CHECK(native.faults == source.faults);
    CHECK(native.visible_chunks == source.visible_chunks);
    CHECK(native.visible_chunks == 299u);
    CHECK(native.count == source.count && native.count == 0u);
    printf("fault ok: faults %u visible %u count %u\n", native.faults, native.visible_chunks, native.count);
    return 0;
}
'''


def build_and_run(test, source, extra):
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / 'driver.c'
        c.write_text(source)
        exe = Path(d) / 'driver'
        cmd = [CC, '-std=gnu11', '-O1', '-Wall', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I', str(ROOT / 'ref'), *extra, str(c), '-o', str(exe)]
        build = subprocess.run(cmd, capture_output=True, text=True)
        test.assertEqual(build.returncode, 0, build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        test.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return run.stdout


@unittest.skipIf(CC is None, 'needs a host C compiler')
class TrackPickCacheTests(unittest.TestCase):
    def test_cache_generation(self):
        out = build_and_run(self, CACHE_DRIVER, [])
        self.assertIn('cache ok', out)

    def test_cache_verify_diagnostic(self):
        out = build_and_run(self, CACHE_DRIVER, ['-DFZX_TRACK_VERIFY=1'])
        self.assertIn('cache ok', out)

    def test_generator_bumps_generation_at_ring_build(self):
        """The ring's construction (func_800A1954 in Course_Init) bumps the generation in the generated unit."""
        import importlib.util
        spec = importlib.util.spec_from_file_location('road_plan', ROOT / 'tools' / 'road_plan.py')
        text = (ROOT / 'tools' / 'road_plan.py').read_text()
        self.assertIn("func_800A1954(gCurrentCourseInfo);\\n#endif\\n'", text)
        self.assertIn('++ref_track_ring_generation;', text)
        self.assertNotIn('ref_track_ring_generation', (ROOT / 'ref' / 'bridge.c').read_text())


@unittest.skipIf(CC is None or not (SH2ZAM / 'src' / 'sh2zam_segmesh.c').is_file(),
                 'needs a host C compiler and SH2ZAM segmesh-1 at third_party/SH2ZAM')
class TrackPickFaultTests(unittest.TestCase):
    def test_exhausted_pool_matches_source(self):
        out = build_and_run(self, FAULT_DRIVER,
                            ['-I', str(SH2ZAM / 'include'), str(ROOT / 'saturn' / 'track_pick.c'),
                             str(SH2ZAM / 'src' / 'sh2zam_segmesh.c')])
        self.assertIn('fault ok', out)


if __name__ == '__main__':
    unittest.main()
