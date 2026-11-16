/* Authored synthetic state gate. No ROM data, graphics calls or race init. */
#include "global.h"
#include "fzx_racer.h"
#include "fzx_camera.h"
#include "fzx_course.h"
#include "fzx_game.h"
#include "fzx_machine.h"
#include "ref.h"

Racer gRacers[TOTAL_RACER_COUNT];
Camera gCameras[4];
s32 gGameMode, gNumPlayers;
f32 D_800CE748, D_800CE74C, D_800CE750;
static CourseSegment segment;
static Vec3s pipe[PIPE_MAX], tunnel[TUNNEL_MAX];
Vec3s* sPipeFogColors = pipe;
Vec3s* sTunnelFogColors = tunnel;
static const unsigned char outside[3] = { 17, 23, 31 };
static unsigned int checks;
extern int printf(const char*, ...);
#define CHECK(x) do { ++checks; if (!(x)) { printf("machine check line %d failed\n", __LINE__); return 1; } } while (0)

static int same(const void* a, const void* b, unsigned int size) {
    const unsigned char* x = a; const unsigned char* y = b;
    while (size--) if (*x++ != *y++) return 0;
    return 1;
}
static void reset(void) {
    Racer* r = gRacers; Camera* c = gCameras;
    unsigned int i;
    memset(gRacers, 0, sizeof(gRacers)); memset(gCameras, 0, sizeof(gCameras));
    memset(&segment, 0, sizeof(segment));
    gGameMode = GAMEMODE_TIME_ATTACK; gNumPlayers = 1;
    r->character = CAPTAIN_FALCON; r->customType = CUSTOM_MACHINE_DEFAULT;
    r->stateFlags = RACER_STATE_ACTIVE; r->machineLod = 5;
    r->segmentPositionInfo.courseSegment = &segment;
    r->bodyR = 51; r->bodyG = 71; r->bodyB = 91;
    r->modelPos.x = 20; r->modelPos.y = 30; r->modelPos.z = 40;
    r->modelBasis.x.z = 1; r->modelBasis.y.y = 1; r->modelBasis.z.x = -1;
    r->segmentBasis.y.y = 1;
    D_800CE748 = 0.1f; D_800CE74C = 0.2f; D_800CE750 = 0.3f;
    /* Authored row-vector perspective: translated W = 100, Z = pos.z+100. */
    c->basis.x.z = -1;
    for (i = 0; i < 3; ++i) c->viewMtx.m[i][i] = c->projectionViewMtx.m[i][i] = 1;
    c->projectionMtx.m[0][0] = c->projectionMtx.m[1][1] = 1;
    c->projectionMtx.m[2][2] = -1;
    segment.previousJoinEndTValue = 0.2f; segment.nextJoinStartTValue = 0.8f;
    segment.joinScale = 0.25f;
    r->segmentPositionInfo.segmentTValue = r->segmentPositionInfo.segmentLengthProportion = 0.5f;
    sPipeFogColors = pipe; sTunnelFogColors = tunnel;
    for (i = 0; i < PIPE_MAX; ++i) { pipe[i].x = 10+i; pipe[i].y = 20+i; pipe[i].z = 30+i; }
    for (i = 0; i < TUNNEL_MAX; ++i) { tunnel[i].x = 40+i; tunnel[i].y = 50+i; tunnel[i].z = 60+i; }
}
static int snapshot(RefMachine* out) {
    Racer racers[TOTAL_RACER_COUNT]; Camera cameras[4]; CourseSegment before = segment;
    Vec3s old_pipe[PIPE_MAX], old_tunnel[TUNNEL_MAX]; RefMachine repeat;
    int ok, again;
    memcpy(racers, gRacers, sizeof(racers)); memcpy(cameras, gCameras, sizeof(cameras));
    memcpy(old_pipe, pipe, sizeof(pipe)); memcpy(old_tunnel, tunnel, sizeof(tunnel));
    memset(out, 0xa5, sizeof(*out)); memset(&repeat, 0x5a, sizeof(repeat));
    ok = ref_machine(out, outside); again = ref_machine(&repeat, outside);
    if (ok != again || !same(out, &repeat, sizeof(repeat)) ||
        !same(racers, gRacers, sizeof(racers)) || !same(cameras, gCameras, sizeof(cameras)) ||
        !same(&before, &segment, sizeof(segment)) || !same(old_pipe, pipe, sizeof(pipe)) ||
        !same(old_tunnel, tunnel, sizeof(tunnel))) return -1;
    return ok;
}
static int fault(unsigned int expected) {
    RefMachine out, zero;
    memset(&zero, 0, sizeof(zero)); zero.faults = expected;
    return snapshot(&out) == 0 && same(&out, &zero, sizeof(out));
}
int main(void) {
    static const float distances[] = {-1,0,229,230,289,290,379,380,469,470,1499,1500,2500,2501};
    static const int lods[] = {0,1,1,2,2,3,3,4,4,5,5,6,6,0};
    Racer* r = gRacers; Camera* c = gCameras; RefMachine out;
    unsigned int i; union { unsigned int u; float f; } nan = {0x7fc00000u};
    reset(); CHECK(sizeof(out) == 112); CHECK(snapshot(&out) == 1);
    CHECK(out.source_lod == 1 && out.normal_body_visible == 1 && out.shifted_distance == 100);
    CHECK(same(out.model_pos, &r->modelPos, 12) && same(out.model_basis, &r->modelBasis, 36));
    CHECK(out.model_scale[0] == 0.1f && out.model_scale[1] == 0.2f && out.model_scale[2] == 0.3f);
    CHECK(out.body[0] == 51 && out.body[1] == 71 && out.body[2] == 91);
    CHECK(out.character == CAPTAIN_FALCON && out.custom_type == CUSTOM_MACHINE_DEFAULT);
    CHECK(out.state_flags == RACER_STATE_ACTIVE && !out.faults && !out.reserved && !out.reserved2);
    for (i = 0; i < 3; ++i) CHECK(out.ambient[i] == 100 && out.light_rgb[i] == 255 && out.light_dir[i] == 69 && out.fog[i] == outside[i]);
    CHECK(out.reflection[0] == 9459 && out.reflection[1] == 9459 && out.reflection[2] == 25842);
    r->bodyR = 204; r->bodyG = 15; r->bodyB = 6; CHECK(snapshot(&out) == 1 && out.body[0] == 204 && out.body[1] == 15 && out.body[2] == 6);
    for (i = 0; i < sizeof(lods)/sizeof(lods[0]); ++i) {
        r->segmentPositionInfo.pos.z = distances[i]-100;
        CHECK(snapshot(&out) == 1 && out.shifted_distance == distances[i] && out.source_lod == lods[i]);
        CHECK(out.normal_body_visible == (lods[i] != 0));
    }
    reset(); r->segmentPositionInfo.pos.x = 100; CHECK(snapshot(&out) == 1 && out.source_lod == 1);
    r->segmentPositionInfo.pos.x = 100.01f; CHECK(snapshot(&out) == 1 && !out.source_lod);
    r->segmentPositionInfo.pos.x = -100; CHECK(snapshot(&out) == 1 && out.source_lod == 1);
    r->segmentPositionInfo.pos.x = -100.01f; CHECK(snapshot(&out) == 1 && !out.source_lod);
    reset(); r->segmentPositionInfo.pos.y = 100; CHECK(snapshot(&out) == 1 && out.source_lod == 1);
    r->segmentPositionInfo.pos.y = -100.01f; CHECK(snapshot(&out) == 1 && !out.source_lod);
    reset(); c->eye.z = 40; CHECK(snapshot(&out) == 1 && out.shifted_distance == 140);
    reset(); r->stateFlags = 0; CHECK(snapshot(&out) == 1 && !out.source_lod && !out.normal_body_visible);
    reset(); r->stateFlags |= RACER_STATE_CRASHED; CHECK(snapshot(&out) == 1 && out.source_lod == 1 && !out.normal_body_visible);
    for (i = 0; i < PIPE_MAX; ++i) {
        reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE | TRACK_SHAPE_PIPE | i;
        CHECK(snapshot(&out) == 1 && out.ambient[0] == 50 && out.light_dir[0] == 0 && out.light_dir[1] == 120 && out.light_dir[2] == 0);
        CHECK(out.fog[0] == 10+i && out.fog[1] == 20+i && out.fog[2] == 30+i);
        CHECK(out.reflection[0] == 0 && out.reflection[1] == 16383 && out.reflection[2] == 16383);
        segment.trackSegmentInfo = TRACK_FLAG_INSIDE | TRACK_SHAPE_TUNNEL | i;
        CHECK(snapshot(&out) == 1 && out.fog[0] == 40+i && out.fog[1] == 50+i && out.fog[2] == 60+i);
    }
    r->segmentPositionInfo.segmentTValue = 0.9f; r->segmentPositionInfo.segmentLengthProportion = 0.875f;
    CHECK(snapshot(&out) == 1 && out.ambient[0] == 75);
    r->segmentPositionInfo.segmentTValue = 0.1f; r->segmentPositionInfo.segmentLengthProportion = 0.125f;
    CHECK(snapshot(&out) == 1 && out.ambient[0] == 75);
    r->segmentPositionInfo.segmentLengthProportion = 0.0625f; CHECK(snapshot(&out) == 1 && out.ambient[0] == 87);
    r->stateFlags = (s32)((u32)r->stateFlags | 0x80000000u);
    CHECK(snapshot(&out) == 1 && out.ambient[0] == 87 && out.light_dir[0] == 69 && same(out.fog, outside, 3));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE | TRACK_SHAPE_PIPE;
    r->segmentBasis.y.x = -0.25f; r->segmentBasis.y.y = 0.5f;
    CHECK(snapshot(&out) == 1 && out.light_dir[0] == -30 && out.light_dir[1] == 60 && out.reflection[0] == -4096);
    reset(); CHECK(ref_machine(0, outside) == 0); CHECK(ref_machine(&out, 0) == 0 && out.faults == REF_MACHINE_STATE);
    r->segmentPositionInfo.courseSegment = 0; CHECK(fault(REF_MACHINE_STATE));
    reset(); gNumPlayers = 2; CHECK(fault(REF_MACHINE_UNSUPPORTED));
    reset(); gGameMode = -1; CHECK(fault(REF_MACHINE_UNSUPPORTED));
    reset(); r->character = -1; CHECK(fault(REF_MACHINE_UNSUPPORTED));
    reset(); r->customType = -1; CHECK(fault(REF_MACHINE_UNSUPPORTED));
    reset(); r->bodyR = -1; CHECK(fault(REF_MACHINE_STATE));
    reset(); r->bodyB = 256; CHECK(fault(REF_MACHINE_STATE));
    reset(); r->modelPos.x = nan.f; CHECK(fault(REF_MACHINE_NONFINITE));
    reset(); c->projectionMtx.m[1][2] = nan.f; CHECK(fault(REF_MACHINE_NONFINITE));
    reset(); D_800CE748 = 0; CHECK(fault(REF_MACHINE_STATE));
    reset(); c->basis.x.x = 2; CHECK(fault(REF_MACHINE_STATE));
    reset(); c->basis.x.z = 0; CHECK(fault(REF_MACHINE_PROJECTION));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE | TRACK_SHAPE_PIPE | PIPE_MAX; CHECK(fault(REF_MACHINE_LIGHT));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE | TRACK_SHAPE_TUNNEL | TUNNEL_MAX; CHECK(fault(REF_MACHINE_LIGHT));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE | TRACK_SHAPE_PIPE; sPipeFogColors = 0; CHECK(fault(REF_MACHINE_LIGHT));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE | TRACK_SHAPE_TUNNEL; sTunnelFogColors = 0; CHECK(fault(REF_MACHINE_LIGHT));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE; r->segmentPositionInfo.segmentTValue = 0.1f; segment.joinScale = 0; CHECK(fault(REF_MACHINE_LIGHT));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE; r->segmentBasis.y.x = nan.f; CHECK(fault(REF_MACHINE_NONFINITE));
    reset(); segment.trackSegmentInfo = TRACK_FLAG_INSIDE; r->segmentBasis.y.x = 2; CHECK(fault(REF_MACHINE_LIGHT));
    printf("machine-state synthetic checks=%u PASS\n", checks);
    return 0;
}
