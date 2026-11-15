/* F-Zero X Saturn showcase foundation. MIT; see ../LICENSE.
 * Course spline attribution and limitations: ../NOTICE.md. */
#ifndef SH2ZAM_FZX_H
#define SH2ZAM_FZX_H
#include <sh2zam/sh2zam.h>

#define FZX_MAX_POINTS 64u
#define FZX_MAX_POLYGONS 512u
#define FZX_WIDTH 320
#define FZX_HEIGHT 224
#define FZX_LEFT 1u
#define FZX_RIGHT 2u
#define FZX_ACCEL 4u
#define FZX_BRAKE 8u
#define FZX_RESET 16u
#define FZX_FRAME_IMPORTED 1u

/* Simulation selector (docs/POC_PLAN.md, lesson 1). PROTO is the prototype
 * centreline driver, never the port. ORIG is the original race code (the
 * decompilation, compiled for the SH-2 with bit-exact binary32 soft-float,
 * tools/saturn_orig.sh): the frontend steps it and hands the core a pose.
 * FIXED (a fixed-point port of the racer physics) would come with Gate 3.
 * Every frontend must show the active simulation. */
#define FZX_SIM_PROTO 0u
#define FZX_SIM_ORIG 2u
#ifndef FZX_SIM
#define FZX_SIM FZX_SIM_PROTO
#endif
#if FZX_SIM != FZX_SIM_PROTO && FZX_SIM != FZX_SIM_ORIG
#error "FZX_SIM: FZX_SIM_PROTO or FZX_SIM_ORIG"
#endif

/* Positions and radii are Q16.16, at 1/16 original game scale. Bank is
 * SH2ZAM turns; tension is Q16.16 in [0,2]. This explicit layout is also
 * the browser upload ABI: 32 bytes per point after the 4-byte count. */
typedef struct {
    sh2z_vec3_t position;
    sh2z_fx_t radius_left, radius_right;
    sh2z_angle_t bank;
    uint16_t reserved;
    uint32_t track_segment_info;
    sh2z_fx_t tension;
} fzx_control_point_t;
typedef struct {
    uint32_t count;
    fzx_control_point_t points[FZX_MAX_POINTS];
} fzx_course_t;

/* Polygon coordinates are absolute 320x224 screen pixels. RGB1555 color:
 * bit 15 set, R bits 0..4, G 5..9, B 10..14. Polygons arrive far to near.
 * Four equal/paired vertices may encode a clipped triangle. */
typedef struct {
    int16_t x[4], y[4];
    uint16_t color, flags;
    int32_t depth;
} fzx_poly_t;
typedef struct {
    uint32_t count, speed, lap, flags;
    fzx_poly_t polys[FZX_MAX_POLYGONS];
} fzx_frame_t;

#if defined(__wasm__)
#define FZX_EXPORT __attribute__((visibility("default")))
#else
#define FZX_EXPORT
#endif

/* One tick is 1/60 s. Prototype handling is deliberately separate from the
 * original game's vehicle physics. Rendering does not advance simulation. */
FZX_EXPORT void fzx_init(void);
FZX_EXPORT void fzx_step(uint32_t buttons);
FZX_EXPORT const fzx_frame_t *fzx_render(void);
FZX_EXPORT fzx_course_t *fzx_course_buffer(void);
FZX_EXPORT int fzx_activate_course(void);
FZX_EXPORT void fzx_use_demo(void);
/* 0: general exact SH2ZAM transform, 1: prepared SH2ZAM batch (default).
 * This switch makes correctness comparisons possible; it is not a timer. */
FZX_EXPORT void fzx_set_transform_mode(uint32_t prepared);
/* FZX_SIM_ORIG: racer 0 as the original code left it after a tick. Position
 * Q16.16 at 1/16 game scale (the course's scale); forward and up are unit
 * Q16.16 vectors (the original's basis rows x and y). speed is km/h, as the
 * original HUD shows it. The renderer follows this pose; fzx_step is unused. */
typedef struct {
    sh2z_vec3_t position, forward, up;
    uint32_t speed, lap;
} fzx_pose_t;
FZX_EXPORT void fzx_set_pose(const fzx_pose_t *pose);
/* Test driver only (the Saturn image's debugger autopilot): a stick x in -63..63, negative to
 * the left as the N64 stick, that turns the pose towards the centreline LOOKAHEAD rings ahead
 * of the ring it is on. 0 without a pose. */
FZX_EXPORT int32_t fzx_steer_hint(uint32_t lookahead);
/* The compiled simulation, as FZX_SIM_*. */
FZX_EXPORT uint32_t fzx_sim_id(void);
bool fzx_validate_course(const fzx_course_t *course);
bool fzx_spline_sample(const fzx_course_t *course, uint32_t segment,
                       sh2z_fx_t t, sh2z_vec3_t *position,
                       sh2z_vec3_t *tangent);

_Static_assert(sizeof(fzx_control_point_t) == 32, "course point upload ABI");
_Static_assert(sizeof(fzx_course_t) == 2052, "course upload ABI");
_Static_assert(sizeof(fzx_poly_t) == 24, "polygon browser ABI");
_Static_assert(offsetof(fzx_frame_t, polys) == 16, "frame browser ABI");
#endif
