/* Source course presentation state, independent of display-list commands. */
#ifndef FZX_REF_ROAD_PLAN_H
#define FZX_REF_ROAD_PLAN_H
#define REF_ROAD_GROUPS 64
typedef struct {
    unsigned short start, end;
    int direction;
    float average_depth;
} RefRoadGroup;
typedef struct {
    unsigned int faults, count, visible_chunks, outer;
    int fog_start;
    unsigned char fog[4], primitive[4];
    RefRoadGroup groups[REF_ROAD_GROUPS];
} RefRoadPlan;
/* Called after the source camera update. Groups retain source far-to-near
 * order. A start==end group covers the whole circular course. Direction +1
 * traverses backward from end; -1 traverses forward from start, as in source.
 * This does not bake geometry, interpret commands or advance simulation. */
/* The race-only closure omits Background_Init and the graphics main loop.
 * Callers must supply their source environment color and graphics-frame phase;
 * neither silently inherits the closure's zero-filled stand-ins. */
void ref_road_plan(RefRoadPlan *out, unsigned int draw_frame,
                   const unsigned char course_fog[3]);
#endif
