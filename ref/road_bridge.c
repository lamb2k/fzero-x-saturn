/* HOST-only source road capture. MIT; see ../NOTICE.md for the source game.
 * Calls the original full-detail course draw, never the native ribbon.
 * Exported data is private; pointer addresses are replaced by asset names. */
#include "global.h"
#include "fzx_course.h"
#include "fzx_segmentA.h"
#include "fzx_racer.h"
#include "ref.h"
#include "road_bindings.h"

extern s32 gSegmentChunkCount;
extern SegmentChunk gSegmentChunks[];
extern SegmentChunk* sLastSegmentChunk;
extern Vtx* gCourseVtxPtr;
extern Gfx* func_800A95B4(Gfx*);
extern void func_800A4BAC(void);
extern void road_out_counts(int, int, int, int);
extern void road_out_chunk(int, unsigned int, int, const short*);
extern void road_out_span(int, int, int, int, int, int);
extern void road_out_vertex(int, const short*, const short*, const unsigned char*);
extern void road_out_command(unsigned int, unsigned int, const char*);

/* The fixed source ring has at most 1024 chunks. Worst supported source shape
 * uses fewer than 32 vertices/commands per chunk. Keep space for the closure
 * sentinel and check the returned pointers before exposing any data. */
static struct { unsigned int before[16]; Vtx data[65536]; unsigned int after[16]; } vertex_storage;
static struct { unsigned int before[16]; Gfx data[65536]; unsigned int after[16]; } command_storage;
#define vertices vertex_storage.data
#define commands command_storage.data
/* Game_Init initializes both frame pools, larger than a generic data stub. */
GfxPool D_8024DCE0[2];
extern void (*sBackwardChunkGroupDrawFuncs[8])(void);
extern SegmentChunk* sWorkingSegmentChunk;
extern Gfx* sCourseDisp;
extern bool D_800F89D8;
static int retain_outer = 1;
void ref_road_outer(int enabled) { retain_outer = enabled != 0; }
static void (*original_draw[8])(void);
typedef struct { int chunk, first_command, end_command, first_vertex, end_vertex; } RoadSpan;
static RoadSpan spans[1024];
static int span_count, span_error;
static void draw_chunk(unsigned int shape) {
    RoadSpan* span;
    if (span_count >= 1024) { span_error = 1; return; }
    span = &spans[span_count++];
    span->chunk = sWorkingSegmentChunk - gSegmentChunks;
    span->first_command = sCourseDisp - commands;
    span->first_vertex = gCourseVtxPtr - vertices;
    /* Override the all-course helper's unconditional true with the same
     * branch value Course_Draw selects inside an enclosed source segment. */
    D_800F89D8 = retain_outer;
    original_draw[shape]();
    span->end_command = sCourseDisp - commands;
    span->end_vertex = gCourseVtxPtr - vertices;
}
#define WRAP(n) static void draw_##n(void) { draw_chunk(n); }
WRAP(0) WRAP(1) WRAP(2) WRAP(3) WRAP(4) WRAP(5) WRAP(6) WRAP(7)
static void (*const wrappers[8])(void) = {draw_0,draw_1,draw_2,draw_3,draw_4,draw_5,draw_6,draw_7};

int ref_road_export(void) {
    Gfx* end;
    int i, vc, cc;
    Racer saved_racer;
    extern Racer gRacers[];
    extern s32 gRandSeed1, gRandSeed2;
    extern u32 gRandMask1, gRandMask2;
    s32 seed1 = gRandSeed1, seed2 = gRandSeed2;
    u32 mask1 = gRandMask1, mask2 = gRandMask2;
    memcpy(&saved_racer, &gRacers[0], sizeof(saved_racer));
    if (gSegmentChunkCount < 3 || gSegmentChunkCount > 1024 ||
        sLastSegmentChunk != &gSegmentChunks[gSegmentChunkCount]) return 1;
    for (i = 0; i < 65536; ++i) {
        vertices[i].v.cn[0] = vertices[i].v.cn[1] = vertices[i].v.cn[2] = 0;
        vertices[i].v.cn[3] = 255;
    }
    for (i = 0; i < 16; ++i)
        vertex_storage.before[i] = vertex_storage.after[i] =
            command_storage.before[i] = command_storage.after[i] = 0xA19C52E7u;
    gCourseVtxPtr = vertices;
    span_count = span_error = 0;
    for (i = 0; i < 8; ++i) {
        original_draw[i] = sBackwardChunkGroupDrawFuncs[i];
        sBackwardChunkGroupDrawFuncs[i] = wrappers[i];
    }
    end = func_800A95B4(commands);
    for (i = 0; i < 8; ++i) sBackwardChunkGroupDrawFuncs[i] = original_draw[i];
    vc = gCourseVtxPtr - vertices;
    cc = end - commands;
    if (vc < 0 || vc > 65536 || cc < 0 || cc > 65536) return 2;
    if (span_error || span_count != gSegmentChunkCount) return 7;
    for (i = 0; i < 16; ++i)
        if (vertex_storage.before[i] != 0xA19C52E7u || vertex_storage.after[i] != 0xA19C52E7u ||
            command_storage.before[i] != 0xA19C52E7u || command_storage.after[i] != 0xA19C52E7u) return 5;
    if (memcmp(&saved_racer, &gRacers[0], sizeof(saved_racer)) || seed1 != gRandSeed1 ||
        seed2 != gRandSeed2 || mask1 != gRandMask1 || mask2 != gRandMask2) return 6;
    road_out_counts(gSegmentChunkCount, vc, cc, span_count);
    for (i = 0; i <= gSegmentChunkCount; ++i)
        road_out_chunk(i, (unsigned int)gSegmentChunks[i].trackSegmentInfo,
                       gSegmentChunks[i].segmentIndex, gSegmentChunks[i].referencePos1);
    for (i = 0; i < span_count; ++i)
        road_out_span(i, spans[i].chunk, spans[i].first_command, spans[i].end_command,
                      spans[i].first_vertex, spans[i].end_vertex);
    for (i = 0; i < vc; ++i)
        road_out_vertex(i, vertices[i].v.ob, vertices[i].v.tc, vertices[i].v.cn);
    for (i = 0; i < cc; ++i) {
        unsigned int a = commands[i].words.w0, b = commands[i].words.w1;
        const char* name = 0;
        if ((a >> 24) == 1) {
            if (b < (unsigned int)vertices || b >= (unsigned int)(vertices + vc) ||
                (b - (unsigned int)vertices) % sizeof(Vtx)) return 3;
            b = (b - (unsigned int)vertices) / sizeof(Vtx);
            name = "vertices";
        } else if ((a >> 24) == 0xDE || (a >> 24) == 0xFD) {
            name = road_asset_name(b);
            if (!name) return 4;
            b = 0;
        }
        road_out_command(a, b, name);
    }
    return 0;
}

void ref_road_boot(void) {
    /* The real Game_Init sets the shape display-list table before Course_Init.
     * No draw or extra simulation tick is inserted into the race harness. */
    func_800A4BAC();
}
