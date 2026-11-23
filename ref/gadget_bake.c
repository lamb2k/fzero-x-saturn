/* Course gadget dump, game side: the world-space data the original builds for the gadgets it draws
 * (landmines, jumps, dash plates, pit/dirt/ice strips, gates, signs, buildings), read through the
 * original course functions with the decomp headers. HOST tool only, never part of the Saturn image.
 * The host driver (gadget_main.c) sees plain C types and writes the dump. */
#include "global.h"
#include "fzx_course.h"
#include "fzx_racer.h"
#include "fzx_game.h"

extern CourseInfo* gCurrentCourseInfo;
extern CourseFeaturesInfo gCourseFeaturesInfo;
extern CourseFeature gCourseFeatures[];
extern CourseEffectsInfo gCourseEffectsInfo;
extern CourseEffect gCourseEffects[];
extern EffectDrawData gEffectsDrawData[];
extern CourseDecoration gCourseDecorations[];
extern SegmentChunk gSegmentChunks[];
extern s32 gSegmentChunkCount;
extern unk_80225800 D_80225800;
extern s32 D_800E12C8[];
extern s32 gCourseIndex;

void Course_LandminesViewInteractDataInit(void);
void Course_JumpsViewInteractDataInit(void);
void Course_DecorationsViewInteractDataInit(void);
void Course_EffectsViewInteractDataInit(bool arg0);

void gb_view_init(void) {
    Course_LandminesViewInteractDataInit();
    Course_JumpsViewInteractDataInit();
    Course_DecorationsViewInteractDataInit();
    Course_EffectsViewInteractDataInit(false);
}

int gb_segment_count(void) { return gCurrentCourseInfo->segmentCount; }
int gb_chunk_count(void) { return gSegmentChunkCount; }
/* chunk: segment index, t, world x y z, track segment info */
void gb_chunk(int i, float o[6]) {
    const SegmentChunk* c = &gSegmentChunks[i];
    o[0] = (float)c->segmentIndex; o[1] = c->segmentTValue; o[2] = c->pos.x; o[3] = c->pos.y; o[4] = c->pos.z;
    o[5] = (float)c->trackSegmentInfo;
}
void gb_segment(int i, float o[5]) {
    CourseSegment* s = &gCurrentCourseInfo->courseSegments[i];
    o[0] = s->radiusLeft; o[1] = s->radiusRight; o[2] = (float)(s->trackSegmentInfo & TRACK_SHAPE_MASK);
    o[3] = s->length; o[4] = s->lengthFromStart;
}

int gb_feature_count(void) { return gCourseFeaturesInfo.featureCount; }
int gb_landmine_count(void) { return gCourseFeaturesInfo.landmineCount; }
int gb_jump_count(void) { return gCourseFeaturesInfo.jumpCount; }
/* feature: type, segment, t, lateral, dimensions x y z, load chunk index (decorations, else -1) */
void gb_feature(int i, float o[8]) {
    const CourseFeature* f = &gCourseFeatures[i];
    int k, d = 0;
    o[0] = (float)f->featureType; o[1] = (float)f->segmentIndex; o[2] = f->segmentTValue; o[3] = f->lateralOffset;
    o[4] = f->dimensions.x; o[5] = f->dimensions.y; o[6] = f->dimensions.z; o[7] = -1.0f;
    if (Course_FeatureIsDecorational(f->featureType)) {
        for (k = 0; k < i; k++) if (Course_FeatureIsDecorational(gCourseFeatures[k].featureType)) d++;
        o[7] = (float)(gCourseDecorations[d].loadChunk - gSegmentChunks);
    }
}
/* decoration (d-th decorational feature): pos, basis rows x y z, scale */
void gb_decoration(int d, float o[13]) {
    const CourseDecoration* c = &gCourseDecorations[d];
    o[0] = c->pos.x; o[1] = c->pos.y; o[2] = c->pos.z;
    o[3] = c->basis.x.x; o[4] = c->basis.x.y; o[5] = c->basis.x.z;
    o[6] = c->basis.y.x; o[7] = c->basis.y.y; o[8] = c->basis.y.z;
    o[9] = c->basis.z.x; o[10] = c->basis.z.y; o[11] = c->basis.z.z;
    o[12] = c->scale;
}

static void vtx5(const Vtx* v, float o[5]) {
    o[0] = v->v.ob[0]; o[1] = v->v.ob[1]; o[2] = v->v.ob[2]; o[3] = v->v.tc[0]; o[4] = v->v.tc[1];
}
void gb_landmine_vtx(int i, float o[25]) { int k; for (k = 0; k < 5; k++) vtx5(&D_80225800.landmineVtx[i * 5 + k], o + k * 5); }
void gb_jump_vtx(int i, float o[30]) { int k; for (k = 0; k < 6; k++) vtx5(&D_80225800.jumpVtx[i * 6 + k], o + k * 5); }

int gb_effect_count(void) { return gCourseEffectsInfo.count; }
/* effect: segment, type, t0, t1, right edge / lateral, left edge / angle */
void gb_effect(int i, float o[6]) {
    const CourseEffect* e = &gCourseEffects[i];
    o[0] = (float)e->segmentIndex; o[1] = (float)e->effectType; o[2] = e->segmentTValueStart; o[3] = e->segmentTValueEnd;
    o[4] = e->rightEdgeDistance; o[5] = e->leftEdgeDistance;
}
/* The effect's vertices as drawn: a dash plate is 6 vertices in the dash buffer (index among dash effects),
 * any other effect a strip of vertex pairs in the terrain buffer. Returns the vertex count. */
int gb_effect_vtx_count(int i) {
    int k, n = 0;
    if (gEffectsDrawData[i].effectType == COURSE_EFFECT_DASH) return 6;
    for (k = 0; k < 1; k++) n = (int)(gEffectsDrawData[i].vtxEnd - gEffectsDrawData[i].vtxStart);
    return n;
}
void gb_effect_vtx(int i, int v, float o[6]) {
    const Vtx* base;
    int k, dash = 0;
    if (gEffectsDrawData[i].effectType == COURSE_EFFECT_DASH) {
        for (k = 0; k < i; k++) if (gEffectsDrawData[k].effectType == COURSE_EFFECT_DASH) dash++;
        base = &D_80225800.dashVtx[dash * 6 + v];
        vtx5(base, o); o[5] = -1.0f;
    } else {
        base = gEffectsDrawData[i].vtxStart + v;
        vtx5(base, o);
        o[5] = (float)D_800E12C8[(base - D_80225800.terrainEffectVtx) / 2]; /* the road chunk each vertex pair sits on */
    }
}
