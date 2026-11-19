/* Rival line bake, game side: reads the loaded course's spline through the
 * original course functions (decomp headers). HOST tool only, never part of
 * the Saturn image. The host driver (rivals_main.c) sees plain C types. */
#include "global.h"
#include "fzx_racer.h"
#include "fzx_course.h"

extern CourseInfo* gCurrentCourseInfo;
extern Racer gRacers[];
extern f32 Course_SplineGetLengthInfo(CourseSegment* segment, f32 targetT, f32* lengthFromStart);
extern void Course_SplineGetPosition(CourseSegment* segment, f32 t, Vec3f* pos);
extern f32 Course_SplineGetBasis(CourseSegment* segment, f32 t, Mtx3F* basis, f32 lengthProportionAlongSegment);

int rb_segment_count(void) { return gCurrentCourseInfo->segmentCount; }
float rb_course_length(void) { return gCurrentCourseInfo->length; }

void rb_segment(int index, float out[5]) {
    CourseSegment* s = &gCurrentCourseInfo->courseSegments[index];
    out[0] = s->lengthFromStart;
    out[1] = s->length;
    out[2] = s->radiusLeft;
    out[3] = s->radiusRight;
    out[4] = (float)(s->trackSegmentInfo & TRACK_SHAPE_MASK);
}

/* Position, basis rows (forward, up, left) and absolute course distance at t. */
void rb_sample(int index, float t, float out[13]) {
    CourseSegment* s = &gCurrentCourseInfo->courseSegments[index];
    Vec3f p;
    Mtx3F b;
    f32 dist = 0.0f;
    Course_SplineGetPosition(s, t, &p);
    (void)Course_SplineGetLengthInfo(s, t, &dist);
    (void)Course_SplineGetBasis(s, t, &b, t);
    out[0] = p.x; out[1] = p.y; out[2] = p.z;
    out[3] = b.x.x; out[4] = b.x.y; out[5] = b.x.z;
    out[6] = b.y.x; out[7] = b.y.y; out[8] = b.y.z;
    out[9] = b.z.x; out[10] = b.z.y; out[11] = b.z.z;
    out[12] = dist;
}

/* Player 0 in line space: race distance, lateral (+ right, as unk_33C), speed. */
void rb_player(float out[3]) {
    const Racer* r = &gRacers[0];
    out[0] = r->raceDistance;
    out[1] = -(r->segmentBasis.z.x * r->segmentPositionInfo.segmentDisplacement.x +
               r->segmentBasis.z.y * r->segmentPositionInfo.segmentDisplacement.y +
               r->segmentBasis.z.z * r->segmentPositionInfo.segmentDisplacement.z);
    out[2] = r->speed;
}
