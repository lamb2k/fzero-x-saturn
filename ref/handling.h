/* Handling probe for the E1 plays-right fixtures (ref/handling_build.sh, HOST only).
 * Read-only view of racer 0 after a tick, in source units. Separate from the
 * v1 trace of main.c and its hash. MIT; see ../LICENSE. */
#ifndef FZX_REF_HANDLING_H
#define FZX_REF_HANDLING_H

typedef struct {
    /* Track frame. lateral is the offset from the track centre along the
     * left axis of the segment (positive left); radius_* are the edge distances. */
    float lateral, radius_left, radius_right, height, lap_distance, race_distance;
    /* Body-frame velocity in units per tick: side (positive left), up, forward. */
    float v_side, v_up, v_forward, speed;
    /* Heading against the track: forward . track-left and forward . track-forward. */
    float heading_side, heading_forward;
    float forward[3], pos[3];
    float accel_force, drift_force, slide_boost, energy, max_energy;
    int flags, boost_timer, spin_timer, attack_state;
    int grip_a, grip_b, slide_count, a_tap;
    int segment, lap, laps_completed, race_time, lap_times[3], intro;
    /* Velocity along the road's left axis (signed): the wall-normal velocity. */
    float v_lateral;
} RefHandling;

void ref_handling(RefHandling* out);
/* Surface effects, jumps and landmines as Race_Init sets them up (see handling_probe.c). */
void ref_handling_race_features(void);
int ref_handling_select_machine(int character, float engine);
typedef void (*RefBakeEmit)(const char* key, int a, int b, const float* v, int n);
void ref_handling_bake(RefBakeEmit emit);
/* Inside spans (pipes, tunnels) with their joins (course-look lane). */
void ref_handling_inside(RefBakeEmit emit);

#endif
