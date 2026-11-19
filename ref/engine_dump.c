/* HOST only (ref/sim_build.sh with FZX_ENGINE_DUMP=1). Reads course and camera
 * constants out of the reference race for the engine's offline bake
 * (tools/engine_path.py). Never part of a Saturn image. */
#include "global.h"
#include "fzx_camera.h"
#include "fzx_game.h"
#include "unk_structs.h"
#include "engine_dump.h"

extern SegmentChunk gSegmentChunks[];
extern SegmentChunk* sLastSegmentChunk;
extern s32 gSegmentChunkCount;
extern f32 sCourseRenderOriginDistance, sCourseFarRenderDistance, D_800CF51C;
extern s32 sCourseFogStartDistance;

unsigned ref_engine_chunk_count(void) {
    if (gSegmentChunkCount < 3 || sLastSegmentChunk != gSegmentChunks + gSegmentChunkCount) return 0;
    return (unsigned)gSegmentChunkCount;
}

void ref_engine_chunk(unsigned i, float out[3]) {
    out[0] = gSegmentChunks[i].pos.x; out[1] = gSegmentChunks[i].pos.y; out[2] = gSegmentChunks[i].pos.z;
}

void ref_engine_constants(float out[REF_ENGINE_CONSTANTS]) {
    const Camera* c = &gCameras[0];
    out[0] = sCourseRenderOriginDistance; out[1] = sCourseFarRenderDistance; out[2] = D_800CF51C;
    out[3] = c->fovScaleX; out[4] = c->fovScaleY; out[5] = c->frustrumCenterX; out[6] = c->frustrumCenterY;
    out[7] = c->xzNormalizedX; out[8] = c->xzNormalizedZ;
    out[9] = (float)sCourseFogStartDistance;
}
