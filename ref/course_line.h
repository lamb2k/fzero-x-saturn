/* Shared course-line baker (E1 physics and E2 rivals): the stations along the course
 * line both packs index, from the original Course_Init data, with one rounding path.
 * HOST only (int is 32-bit, as on every HOST the reference builds for; the decomp
 * headers on the bridge side do not mix with <stdint.h>). MIT; see ../LICENSE.
 *
 * Output contract (course-line-1, generated/E1-PHYSICS-PLAN.md section 8):
 * - Unit: world units in Q12, converted once from the float: q12 = lround(x * 4096).
 * - Stations: each segment start (t = 0, distance = the segment's lengthFromStart),
 *   then t = k / n for k = 1 .. n - 1 with n = ceil(segment length / spacing); the
 *   distance of an inner station is Course_SplineGetLengthInfo's arc length. A station
 *   within 1/128 unit of the previous one is dropped, so distances strictly increase.
 * - Lap: distance 0 is segment 0's start; the lap wraps at the course length in Q12.
 * - Q12 to Q8: course_line_q8 only, for station distances and the lap length alike;
 *   never re-round from the float.
 * - Every gap between stations, and from the last station to the lap length, lies in
 *   [2, 65536] Q8 (2/256 to 256 units), or the baker fails.
 * - Agreement hash: FNV-1a 64 over big-endian int32 words: lap length Q12, segment
 *   count, then each segment start's distance Q12. The E1 physics pack carries the same
 *   words (node start, line length), so both packs can be checked to agree. */
#ifndef FZX_REF_COURSE_LINE_H
#define FZX_REF_COURSE_LINE_H

#define COURSE_LINE_SPACING 64.0f

typedef struct {
    int dist_q12;   /* distance along the line from the start, Q12 */
    int segment;    /* course segment index */
    int t_q16;      /* in-segment spline parameter, Q16 (round half away from zero) */
    float t;            /* the same t as a float, HOST only */
} CourseLineStation;

/* Q12 to Q8, round half up (inputs are non-negative). */
static inline int course_line_q8(int q12) {
    return (int) (((unsigned int) q12 + 8u) >> 4);
}

/* After ref_race_init: fills up to cap stations; returns the count, or -1 (too many
 * stations, or a gap outside the Q8 bounds). *length_q12 receives the lap length. */
int course_line_stations(CourseLineStation* out, int cap, int* length_q12, float spacing);

/* Agreement hash of the lap length and the segment starts (see above). */
unsigned long long course_line_hash(int length_q12, const int* segment_starts_q12, int segments);

/* The same hash from the current course (after ref_race_init). */
unsigned long long course_line_course_hash(void);

#endif
