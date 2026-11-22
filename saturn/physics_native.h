/* E1 native player physics: glue between the game and SH2ZAM vehicle-dynamics-1.
 *
 * - The physics pack (tools/physics_pack.py): course spline, surface zones, jumps,
 *   mines, the machine's derived constants and the racer's starting state, as
 *   big-endian 32-bit words. Baked from the owner's data; never committed.
 * - The rule constants of the original race code, in fixed point.
 * - The pad mapping: Saturn pad (or N64 buttons for HOST qualification) to the
 *   model's buttons and stick.
 * HOST and SH-2 builds share this file. MIT; see ../LICENSE. */
#ifndef FZX_PHYSICS_NATIVE_H
#define FZX_PHYSICS_NATIVE_H

#include <stddef.h>
#include <stdint.h>

#include "sh2zam/vehicle_dynamics_experimental.h"

#define FZX_PHYS_MAGIC 0x465A5850u /* "FZXP" */
#define FZX_PHYS_VERSION 2u
#define FZX_PHYS_VERSION_TUBES 3u /* v2 plus one join record per node (tube courses) */
#define FZX_PHYS_VERSION_FORCE 4u        /* v2 plus a force_frac word and Q(force_frac) force constants and accel_force */
#define FZX_PHYS_VERSION_FORCE_TUBES 5u  /* v3 plus the same */

/* The engine force scale of this build: SH2ZAM vehicle-dynamics-4's SH2Z_VD_FORCE_FRAC (16, the default, is
 * today's Q16; 24 is the wide force). A pack names its scale (versions 4/5) and a reader built for another
 * scale refuses it; a version 2/3 pack is Q16 and a wide build widens it exactly (<<) and flags it. */
#define FZX_PHYS_FORCE_FRAC SH2Z_VD_FORCE_FRAC

/* FZX_PHYS_TUBES=1: pipe and cylinder spans (SH2ZAM vehicle-dynamics-2, sh2z_vd_step2) and
 * version-3 packs. Off, the glue is the vehicle-dynamics-1 glue it always was and refuses a
 * version-3 pack. */
#ifndef FZX_PHYS_TUBES
#define FZX_PHYS_TUBES 0
#endif

#define FZX_PHYS_MAX_NODES 128
#define FZX_PHYS_MAX_ZONES 128
#define FZX_PHYS_MAX_RAMPS 32
#define FZX_PHYS_MAX_MINES 64

typedef struct {
    sh2z_vd_track_t track;
    sh2z_vd_vehicle_t vehicle;
    sh2z_vd_body_t start;
    sh2z_vd_node_t nodes[FZX_PHYS_MAX_NODES];
    sh2z_vd_zone_t zones[FZX_PHYS_MAX_ZONES];
    sh2z_vd_ramp_t ramps[FZX_PHYS_MAX_RAMPS];
    sh2z_vd_mine_t mines[FZX_PHYS_MAX_MINES];
    uint32_t state_offset;   /* byte offset of the starting state in the pack */
    uint8_t force_frac;      /* the pack's force scale (16 for a version 2/3 pack) */
    uint8_t legacy_force;    /* 1: a version 2/3 pack loaded into a wide build (constants widened exactly, rounded as Q16) */
    uint16_t prev_buttons;   /* N64 buttons applied on the tick the state follows (restarts) */
#if FZX_PHYS_TUBES
    sh2z_vd_track2_t track2; /* track (copied after the load), the joins and the tube rules */
    sh2z_vd_join_t joins[FZX_PHYS_MAX_NODES];
#endif
} FzxPhysCourse;

extern const sh2z_vd_rules_t fzx_phys_rules;
#if FZX_PHYS_TUBES
extern const sh2z_vd_tube_rules_t fzx_phys_tube_rules;
#endif

/* One tick of the course's model: vehicle-dynamics-2 with tubes, vehicle-dynamics-1 without. */
static inline void fzx_phys_step(const FzxPhysCourse* c, sh2z_vd_body_t* b, const sh2z_vd_rules_t* r,
                                 const sh2z_vd_input_t* in) {
#if FZX_PHYS_TUBES
    sh2z_vd_step2(b, &c->track2, &c->vehicle, r, in);
#else
    sh2z_vd_step(b, &c->track, &c->vehicle, r, in);
#endif
}

/* Loads a pack image (big-endian words). zones == 0 drops surface zones, jumps and
 * mines (the legacy bridge's races). Returns 0, or a negative error: -1 magic or version (a version this
 * build does not read), -2 counts, -3 a record out of range (also a force constant that does not fit the
 * build's scale), -4 the state block, -5 a tube join, -6 a pack whose force_frac is not this build's scale. */
int fzx_phys_load(FzxPhysCourse* c, const uint8_t* data, size_t size, int zones);

/* Writes a pack whose starting state is b: the course part of image (the pack c was
 * loaded from), then b and the buttons applied on b's last tick. Returns the size, or 0
 * if out is too small (or if c is a version 2/3 pack in a wide build: its state would not round-trip).
 * A run restored from it continues bit for bit (state restore). */
size_t fzx_phys_save(const FzxPhysCourse* c, const uint8_t* image, const sh2z_vd_body_t* b, uint16_t prev_buttons,
                     uint8_t* out, size_t cap);

/* N64 button word (A 0x8000, B 0x4000, Z 0x2000, R 0x10, C-down 0x4, ...) to the
 * model's buttons. HOST qualification feeds the reference's inputs through this. */
uint16_t fzx_phys_buttons_n64(uint16_t n64);

/* Saturn digital pad to model input, the proposed mapping (owner feel check at E1):
 * - d-pad left/right: the stick ramps toward full lock by 16 a call (4 calls to 63), so
 *   a tap steers lightly and a hold turns fully; release returns it to centre at once;
 * - d-pad up/down: nose down/up (full pitch input);
 * - A accelerate, B boost, C brake (the original's C-down), L lean left, R lean right
 *   (double tap for the side attack, hold the other for the spin attack);
 * - X, Y, Z, Start: no driving function (they count as "any button" for the hover start).
 * Called once per 30 Hz frame; the result is held for both 60 Hz ticks, with pressed
 * and released cleared for the second tick so a press is one edge. */
typedef struct {
    uint16_t held;   /* FZX_PAD_* bits */
    int8_t stick_x;  /* current ramped stick */
} FzxPadState;

#define FZX_PAD_RIGHT 0x8000u
#define FZX_PAD_LEFT  0x4000u
#define FZX_PAD_DOWN  0x2000u
#define FZX_PAD_UP    0x1000u
#define FZX_PAD_START 0x0800u
#define FZX_PAD_A     0x0400u
#define FZX_PAD_C     0x0200u
#define FZX_PAD_B     0x0100u
#define FZX_PAD_R     0x0080u
#define FZX_PAD_X     0x0040u
#define FZX_PAD_Y     0x0020u
#define FZX_PAD_Z     0x0010u
#define FZX_PAD_L     0x0008u

void fzx_phys_pad(FzxPadState* s, uint16_t pad_held, sh2z_vd_input_t* in);

#endif
