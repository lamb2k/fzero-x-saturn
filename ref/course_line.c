/* Shared course-line baker; see course_line.h. Bridge side: decomp headers. MIT. */
#include "global.h"
#include "fzx_course.h"
#include "course_line.h"

extern CourseInfo* gCurrentCourseInfo;
f32 Course_SplineGetLengthInfo(CourseSegment* segment, f32 targetT, f32* lengthFromStart);

static int q12(double x) {
    double v = x * 4096.0;
    return (int) (v >= 0.0 ? v + 0.5 : v - 0.5);
}

static int q16(double x) {
    double v = x * 65536.0;
    return (int) (v >= 0.0 ? v + 0.5 : v - 0.5);
}

int course_line_stations(CourseLineStation* out, int cap, int* length_q12, float spacing) {
    const CourseInfo* c = gCurrentCourseInfo;
    int count = 0;
    s32 i;
    int last = -1;
    *length_q12 = q12(c->length);
    for (i = 0; i < c->segmentCount; i++) {
        CourseSegment* s = &c->courseSegments[i];
        int n = (int) (s->length / spacing);
        int k;
        if ((float) n * spacing < s->length) n++;
        if (n < 1) n = 1;
        for (k = 0; k < n; k++) {
            float t = (float) k / (float) n;
            f32 along = s->lengthFromStart;
            int d;
            if (k > 0) Course_SplineGetLengthInfo(s, t, &along);
            d = q12(along);
            if (last >= 0 && d - last < 32) continue; /* within 1/128 unit */
            if (count == cap) return -1;
            if (last >= 0 && (course_line_q8(d) - course_line_q8(last) < 2 || course_line_q8(d) - course_line_q8(last) > 65536))
                return -1;
            out[count].dist_q12 = d;
            out[count].segment = i;
            out[count].t = t;
            out[count].t_q16 = q16(t);
            last = d;
            count++;
        }
    }
    if (count > 0) {
        int gap = course_line_q8(*length_q12) - course_line_q8(last);
        if (gap < 2 || gap > 65536) return -1;
    }
    return count;
}

unsigned long long course_line_hash(int length_q12, const int* starts, int segments) {
    unsigned long long h = 0xCBF29CE484222325ull;
    int i, k;
    unsigned int words[2];
    words[0] = (unsigned int) length_q12;
    words[1] = (unsigned int) segments;
    for (i = 0; i < 2 + segments; i++) {
        unsigned int w = i < 2 ? words[i] : (unsigned int) starts[i - 2];
        for (k = 24; k >= 0; k -= 8) h = (h ^ ((w >> k) & 0xFFu)) * 0x100000001B3ull;
    }
    return h;
}

unsigned long long course_line_course_hash(void) {
    static int starts[1024];
    const CourseInfo* c = gCurrentCourseInfo;
    s32 i;
    if (c->segmentCount > 1024) return 0;
    for (i = 0; i < c->segmentCount; i++) starts[i] = q12(c->courseSegments[i].lengthFromStart);
    return course_line_hash(q12(c->length), starts, c->segmentCount);
}
