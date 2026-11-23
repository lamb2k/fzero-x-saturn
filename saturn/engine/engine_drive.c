/* Engine line, milestone E1: the drive slice (see engine_drive.h). */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "engine_drive.h"
#include "engine_atk_prof.h"
#if ENG_SND
#include "engine_snd.h"
#endif
#include "physics_native.h"
#include "physics_identity.h"
#if E1_DRIVER == 2
#include "drive_ahead.h"
#endif
#ifndef FZX_P3_BASELINE
#if ENG_SCENES
#include "engine_scenes.h"
#include "machine_tuning.h"
#endif
#endif
#include "course_select.h"
#include "racer_sprites_identity.h"
#include "texture_cd.h"
#include "sh2zam/rotation_sprite_experimental.h"
#include "sh2zam/sprite_cache_experimental.h"
#include "sh2zam/divu_experimental.h"
#ifndef ENG_ROSTER
#define ENG_ROSTER 0
#endif
#if ENG_ROSTER
#include "engine_roster.h"
#endif
#if E1_FIELD
#include "engine_field.h"
#endif
#ifndef ENG_ATTACKS
#define ENG_ATTACKS 0
#endif
#if ENG_ATTACKS
#include "engine_attacks.h"
#endif
#if ENG_ATK_PROF
uint16_t fzx_atk_prof[FZX_ATK_PROF_ROWS][FZX_ATK_PROF_COLS];
uint32_t fzx_atk_prof_row = FZX_ATK_PROF_ROWS;
#endif

#ifndef ENG_ATK_DEFER
#define ENG_ATK_DEFER 0
#endif
/* ENG_ATK_LAG (approximation P1, claude/attacks-cost-3; needs ENG_ATK_DEFER): a second tick of a frame whose contact fzx_atk_player_clear
 * cannot clear waits behind the queued field tick like a cleared one, and the player's share of that contact (push, damage, regain, hit)
 * reaches the player one tick later, in the next step's contact. Every rival's side of the contact is exact and in the original order. */
#ifndef ENG_ATK_LAG
#define ENG_ATK_LAG 0
#endif
#if ENG_ATK_LAG && !(ENG_ATK_DEFER && ENG_ATTACKS)
#error ENG_ATK_LAG needs ENG_ATTACKS and ENG_ATK_DEFER
#endif
#ifndef E1_DRIVER
#define E1_DRIVER 0
#endif
/* 1, R01: kl 0.004, kh 2.5, target 0; R02 adds B every 240 ticks. 2, R03: the original CPU steering law with
 * its look-ahead (drive_ahead.h), the GP test driver that finishes every Jack course (Port Town included). */
#ifndef E1_DRIVER_BOOST
#define E1_DRIVER_BOOST 0
#endif
/* The fixtures' race script: 460 ticks of nothing, then A held for 9,000. */
/* The race runs 9,000 ticks after the start by default (the HOST references' length); a course whose finish is later
 * (Rainbow Road, 10,520 ticks) builds with E1_RACE_TICKS above it, and its HOST reference runs the same count. */
#ifndef E1_RACE_TICKS
#define E1_RACE_TICKS 9000u
#endif
enum { RACE_START_TICK = 460u, RACE_END_TICK = 460u + E1_RACE_TICKS, CHECKPOINT_EVERY = 256u, CHECKPOINTS = 64u };
#define N64_A 0x8000u
#define N64_B 0x4000u

volatile uint32_t fzx_e1_driver = E1_DRIVER;
volatile uint32_t fzx_e1_pad_override;
volatile uint32_t fzx_e1_report[16];
/* FNV-1a 64 of the body alone after every CHECKPOINT_EVERY-th step (hi, lo):
 * the HOST driver prints the same with --checkpoints 256. */
volatile uint32_t fzx_e1_checkpoints[CHECKPOINTS][2];
volatile uint32_t fzx_e1_status; /* 1 loaded; 2 pack load, 3 pack format, 4 sprite load, 5 sprite set, 6 shadow */

static FzxPhysCourse course;
static sh2z_vd_body_t body;
static FzxPadState pad_state;
static uint16_t prev_n64;
static uint32_t ticks;
#if ENG_LOW_COLD
/* Read once by fzx_phys_load at LOAD (which copies what the race uses into `course`): low work RAM (engine_low.x). */
__attribute__((section(".bss.lowcold.drive_pack_buf")))
#endif
static uint32_t pack_buf[(CMAX(FZX_COURSE_MAX_PHYS_BYTES,FZX_PHYSICS_PACK_BYTES) + 3u) / 4u + 2u] __attribute__((aligned(8)));

/* ---- physics ---------------------------------------------------------------- */
static uint64_t fnv64_body(const sh2z_vd_body_t *b) {
    uint64_t h = 0xCBF29CE484222325ull;
    const uint8_t *p = (const uint8_t *)b; /* big-endian words in memory: the HOST hash's byte order */
    for (unsigned i = 0; i < sizeof(*b); ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}
static double q30d(int32_t v) { return v / 1073741824.0; }
static double dot30(const int32_t a[3], const int32_t b[3]) {
    return q30d(a[0]) * q30d(b[0]) + q30d(a[1]) * q30d(b[1]) + q30d(a[2]) * q30d(b[2]);
}

/* The fixtures' driver, as tools/physics_host/physics_main.c computes it (float). */
/* The R01 test driver's heading gain (E1-PLAYS-RIGHT.md); a course whose corners the nominal 2.5 cannot
 * hold without wall contact uses a documented variant (COURSES.md), the same on HOST. */
#ifndef E1_DRIVER_KH
#define E1_DRIVER_KH 2.5f
#endif
/* The lateral gain and the constant lateral target of R01-t (tools/physics_host `--driver kl,kh,0,460,target`);
 * HOST and test driver only, not game behaviour. The defaults are the nominal driver's. */
#ifndef E1_DRIVER_KL
#define E1_DRIVER_KL 0.004f
#endif
#ifndef E1_DRIVER_TARGET
#define E1_DRIVER_TARGET 0.0f
#endif
#if ENG_PHYS_AHEAD
static signed char driver_stick_of(const sh2z_vd_body_t *b) {
    const float lateral = (float)(sh2z_vd_body_lateral(b) / 65536.0);
    const float heading = (float)dot30(b->body[0], b->road[2]);
    float steer = E1_DRIVER_KL * (lateral - E1_DRIVER_TARGET) + E1_DRIVER_KH * heading;
    if (steer > 1.0f) steer = 1.0f;
    if (steer < -1.0f) steer = -1.0f;
    return (signed char)(steer >= 0.0f ? (int)(steer * 63.0f + 0.5f) : -(int)(-steer * 63.0f + 0.5f));
}
static signed char driver_stick(void) { return driver_stick_of(&body); }
#else
static signed char driver_stick(void) {
    const float lateral = (float)(sh2z_vd_body_lateral(&body) / 65536.0);
    const float heading = (float)dot30(body.body[0], body.road[2]);
    float steer = E1_DRIVER_KL * (lateral - E1_DRIVER_TARGET) + E1_DRIVER_KH * heading;
    if (steer > 1.0f) steer = 1.0f;
    if (steer < -1.0f) steer = -1.0f;
    return (signed char)(steer >= 0.0f ? (int)(steer * 63.0f + 0.5f) : -(int)(-steer * 63.0f + 0.5f));
}
#endif
#if ENG_ATTACKS
/* Run-time driver 2 is the scripted hunter (engine_attacks.c) in every image but the R03 look-ahead ones, where it is the
 * look-ahead driver (E1_DRIVER == 2 builds; the hunter would override its steering and buttons). */
#if E1_DRIVER == 2
#define ATK_HUNTER_ON() (0)
#else
#define ATK_HUNTER_ON() (fzx_e1_driver == 2u)
#endif
#endif

#if E1_FIELD
/* Close-up 3D-N: the rival field steps after the player, every tick (FRT time summed for the frame row). */
volatile uint32_t fzx_field_sim_ticks;
#endif
#if E1_FIELD && ENG_BAL_EARLY
/* ENG_BAL_EARLY: the player's state after each tick of this frame, for the field. A frame
 * steps at most 4 ticks; a longer run steps the field in order before taking more. */
enum { FIELD_PENDING_MAX = 8u };
static sh2z_vd_body_t field_bodies[FIELD_PENDING_MAX];
static unsigned field_pending;
#if ENG_ATTACKS
/* With attacks, a tick's contact can wait in the same queue (field_contact[k]: the entry is that
 * tick's contact on the player's state before its step, not a field tick) when it provably gives
 * the player nothing (fzx_atk_player_clear): the field tick before it and the contact then run
 * after the slave's kick, in the original order. field_contacts counts the contacts queued. */
static uint8_t field_contact[FIELD_PENDING_MAX];
static unsigned field_contacts;
#if ENG_ATK_DEFER
/* [0] contacts deferred, [1] live, [2] deferred with a player output (must stay 0), [3] deferral tests,
 * [4] contacts run ahead, [5] ahead contacts taken, [6] ahead contact found with a queue (must stay 0). */
volatile uint32_t fzx_atk_defer_report[ENG_ATK_LAG ? 10 : 8];
/* The next tick's contact, run after this frame's field ticks while the slave builds the track
 * (fzx_drive_contact_ahead): it reads only the field after its last tick and the player's state now,
 * which nothing changes before the next step_once takes it. */
static sh2z_vd_contact_t ahead_contact;
static uint8_t ahead_ok;
#if ENG_ATK_LAG
/* The player's share of a lagged contact (field_contact[k] == 2), held until the next step. */
static sh2z_vd_contact_t lag_share;
static uint8_t lag_have;
#endif
#endif
#endif
void fzx_drive_field_run(void) {
    for (unsigned k = 0; k < field_pending; ++k) {
        const uint16_t f0 = cpu_frt_count_get();
#if ENG_ATTACKS
        if (field_contact[k]) {
            sh2z_vd_contact_t c;
            fzx_atk_contact(&field_bodies[k], &c);
#if ENG_ATK_LAG
            if (field_contact[k] == 2u) {
                lag_share = c;
                lag_have = 1u;
            } else
#endif
#if ENG_ATK_DEFER
            if (c.push[0] | c.push[1] | c.push[2] | c.damage | c.hit | c.regain) ++fzx_atk_defer_report[2];
#endif
        } else
#endif
        fzx_field_tick(&field_bodies[k]);
        fzx_field_sim_ticks += (uint16_t)(cpu_frt_count_get() - f0);
    }
    field_pending = 0;
#if ENG_ATTACKS
    field_contacts = 0;
#endif
}
#if ENG_ATTACKS && ENG_ATK_DEFER
void fzx_drive_contact_ahead(void) {
    if (ahead_ok || field_pending) return;
    if (fzx_e1_driver && ticks >= RACE_END_TICK) return;
    if (ATK_HUNTER_ON()) return;   /* the scripted hunter reads the attack state before the next contact */
    fzx_atk_hold_hooks(true);
    fzx_atk_contact(&body, &ahead_contact);
    fzx_atk_hold_hooks(false);
    ahead_ok = 1;
    ++fzx_atk_defer_report[4];
}
#endif
#elif ENG_BAL_EARLY
void fzx_drive_field_run(void) {}
#endif
#if defined(ENG_SCENES) && ENG_SCENES
/* SCENES.md 11.3: one menu update per simulation tick; the timer counts the updates that
 * observe the player's finish, the first (the tick that sets the flag) being 1. */
void (*volatile fzx_drive_finish_hook)(uint32_t timer, uint32_t tick);
static uint32_t finish_timer;
#endif
#if ENG_ATTACKS
/* fzx_phys_step with contact: the tube tick on tube courses (vehicle-dynamics-3 sh2z_vd_step2_contact). */
static inline void phys_step_contact(const FzxPhysCourse *c, sh2z_vd_body_t *b, const sh2z_vd_rules_t *r,
                                     const sh2z_vd_input_t *in, const sh2z_vd_contact_t *k) {
#if FZX_PHYS_TUBES
    sh2z_vd_step2_contact(b, &c->track2, &c->vehicle, r, in, k);
#else
    sh2z_vd_step_contact(b, &c->track, &c->vehicle, r, in, k);
#endif
}
#endif
#if ENG_PHYS_AHEAD && ENG_ATTACKS
/* ENG_PHYS_AHEAD (claude/speed-5, with attacks claude/attacks-cost-2): the next frame's physics ticks are computed in the
 * master's idle time at the end of this frame (fzx_drive_ahead_prepare, called by the loop while VDP1's previous frame is
 * still being drawn) on a copy of the player's state, assuming no contact on the player, and the next fzx_drive_step takes
 * them instead of stepping, tick by tick: the player's state, the tick count, the sound's read, the field's copy, the finish
 * hook and the checkpoints all happen there, in the same order and on the same states as before. With ENG_ATTACKS each taken
 * tick still runs its field tick and its racer-racer contact (or defers them, as without the prediction); the tick is taken
 * only when the contact this step would give the player is zero (push, damage, regain and hit all 0: the deferred contact
 * of fzx_atk_player_clear, the contact run ahead, or the live one), where sh2z_vd_step_contact is sh2z_vd_step bit for bit.
 * A tick with a contact for the player discards the rest of the prediction and steps as before. A step that finds the
 * prediction wrong (another tick count, the race restarted, another pad word) throws it away; the pad is read again at
 * the step, so a held button is never older than before. The scripted hunter is never predicted. */
enum { AHEAD_MAX = 2u };
static struct {
    uint32_t valid, req, n, cursor, from_ticks;
    uint16_t pad;
    sh2z_vd_body_t after[AHEAD_MAX];
#if ENG_PICK_AHEAD
    /* ENG_PICK_AHEAD (claude/speed-6): the camera of the state the prediction ends in (computed with the lens and orbit named here),
     * the tick and outer bit the next frame will show; view = a camera was computed, kept = the steps took the whole prediction
     * (every one of its ticks; a tick that took a contact for the player drops the rest and leaves kept clear, so the early pick is undone). */
    fzx_engine_cam_t cam;
    uint32_t view, kept, limited, orbit_key, tick_after, outer_after;
    int32_t lens_key[3];
#endif
} ahead;
/* [0] prepared, [1] ticks taken, [2] pad word differed, [3] tick count or start differed, [4] master FRT units spent preparing,
 * [5] the longest prepare (FRT units), [6] frames the loop did not prepare (VDP1 idle), [10] ticks stepped because the contact
 * gave the player something (the prediction dropped from there; [7] counts early picks with ENG_PICK_AHEAD), [8] master FRT units spent on the contact run ahead after the
 * picture (ENG_ATTACKS), [9] the longest. */
volatile uint32_t fzx_e1_ahead[12];
static inline bool ahead_take(bool contact_zero) {
    if (!ahead.valid) return false;
    if (ahead.cursor < ahead.n && contact_zero) {
        body = ahead.after[ahead.cursor++];
        ++fzx_e1_ahead[1];
#if ENG_PICK_AHEAD
        if (ahead.cursor == ahead.n) ahead.kept = 1;   /* only the full n-tick take keeps the early pick */
#endif
        return true;
    }
    ahead.valid = 0;
    if (!contact_zero) ++fzx_e1_ahead[10];
    return false;
}
#endif
#if ENG_PHYS_AHEAD && !ENG_ATTACKS
/* Everything after the physics of one tick: the sound's read, the field's copy, the count, the finish hook and the checkpoint. */
static void after_step(void) {
#if ENG_SND
    engine_snd_tick(&body, &course.vehicle, course.track.laps); /* reads the state only */
#endif
#if E1_FIELD && ENG_BAL_EARLY
    /* The field steps after the kick (fzx_drive_field_run) on this tick's player state. */
    if (field_pending >= FIELD_PENDING_MAX) fzx_drive_field_run();
    field_bodies[field_pending++] = body;
#elif E1_FIELD
    {
        const uint16_t f0 = cpu_frt_count_get();
        fzx_field_tick(&body);
        fzx_field_sim_ticks += (uint16_t)(cpu_frt_count_get() - f0);
    }
#endif
    ++ticks;
#if defined(ENG_SCENES) && ENG_SCENES
    /* The timer restarts with every race, cold load or retry: it runs only while finished. */
    if (!(body.flags & SH2Z_VD_FINISHED)) finish_timer = 0;
    else if (finish_timer < 0xFFFFu) {
        ++finish_timer;
        if (fzx_drive_finish_hook) {
#if E1_FIELD && ENG_BAL_EARLY
            fzx_drive_field_run(); /* the hook reads the field: step it to this tick first */
#endif
            fzx_drive_finish_hook(finish_timer, ticks);
        }
    }
#endif
    if (ticks % CHECKPOINT_EVERY == 0u && ticks / CHECKPOINT_EVERY <= CHECKPOINTS) {
        const uint64_t h = fnv64_body(&body);
        fzx_e1_checkpoints[ticks / CHECKPOINT_EVERY - 1u][0] = (uint32_t)(h >> 32);
        fzx_e1_checkpoints[ticks / CHECKPOINT_EVERY - 1u][1] = (uint32_t)h;
        fzx_e1_report[8] = ticks / CHECKPOINT_EVERY;
    }
}
static void step_once(sh2z_vd_input_t *in) {
    fzx_phys_step(&course, &body, &fzx_phys_rules, in);
    after_step();
}
#else
static void step_once(sh2z_vd_input_t *in) {
#if ENG_ATTACKS
    sh2z_vd_contact_t contact;
#if E1_FIELD && ENG_BAL_EARLY
    /* Contact needs every rival at this tick: step the field's pending ticks first (the last tick of a
     * frame still steps after the kick). With ENG_ATK_DEFER, this tick's contact instead waits behind
     * the one field tick still queued when it provably gives the player nothing
     * (fzx_atk_player_clear); both then run after the slave's kick, in the original order. */
    bool deferred = false;
#if ENG_ATK_DEFER
    if (ahead_ok) {
        ahead_ok = 0;
        if (field_pending) ++fzx_atk_defer_report[6];
        contact = ahead_contact;
        fzx_atk_release_hooks();
        deferred = true;
        ++fzx_atk_defer_report[5];
    } else if (field_pending == 1u && !field_contacts) {
        ++fzx_atk_defer_report[3];
        int32_t pd, pl;
        fzx_field_player_line(&body, &pd, &pl);
        if (fzx_atk_player_clear(&body, pd, pl, true)) {
            field_bodies[field_pending] = body; field_contact[field_pending] = 1u; ++field_pending; ++field_contacts;
            memset(&contact, 0, sizeof(contact));
            deferred = true;
            ++fzx_atk_defer_report[0];
        }
#if ENG_ATK_LAG
        else {
            /* P1: not provably clear. The contact still waits behind the field tick (rivals exact); the player's share lags one tick. */
            field_bodies[field_pending] = body; field_contact[field_pending] = 2u; ++field_pending; ++field_contacts;
            memset(&contact, 0, sizeof(contact));
            deferred = true;
            ++fzx_atk_defer_report[7];
        }
#endif
    }
#endif
    if (!deferred) {
        APROF_T(fr);
        fzx_drive_field_run();
        APROF(10, fr);
#if ENG_ATK_DEFER
        ++fzx_atk_defer_report[1];
#endif
        /* Every racer-racer contact first (engine_attacks.c), then the player's step with its share. */
        fzx_atk_contact(&body, &contact);
    }
#else
    fzx_atk_contact(&body, &contact);
#endif
#if ENG_ATK_LAG
    if (lag_have) {   /* the player's share of the lagged contact of the tick before (P1) */
        lag_have = 0u;
        if (lag_share.push[0] | lag_share.push[1] | lag_share.push[2] | lag_share.damage | lag_share.hit | lag_share.regain) {
            for (unsigned c = 0; c < 3u; ++c) contact.push[c] += lag_share.push[c];
            contact.damage += lag_share.damage; contact.regain += lag_share.regain; contact.hit |= lag_share.hit;
            ++fzx_atk_defer_report[8];
        }
    }
#endif
#if ENG_PHYS_AHEAD
    if (!ahead_take(!(contact.push[0] | contact.push[1] | contact.push[2] | contact.damage | contact.hit | contact.regain)))
#endif
    {
        APROF_T(ps);
        phys_step_contact(&course, &body, &fzx_phys_rules, in, &contact);
        APROF(5, ps);
    }
#else
    APROF_T(ps);
    fzx_phys_step(&course, &body, &fzx_phys_rules, in);
    APROF(5, ps);
#endif
#if ENG_SND
    engine_snd_tick(&body, &course.vehicle, course.track.laps); /* reads the state only */
#endif
#if E1_FIELD && ENG_BAL_EARLY
    /* The field steps after the kick (fzx_drive_field_run) on this tick's player state. */
    if (field_pending >= FIELD_PENDING_MAX) fzx_drive_field_run();
#if ENG_ATTACKS
    field_contact[field_pending] = 0;
#endif
    field_bodies[field_pending++] = body;
#elif E1_FIELD
    {
        const uint16_t f0 = cpu_frt_count_get();
        fzx_field_tick(&body);
        fzx_field_sim_ticks += (uint16_t)(cpu_frt_count_get() - f0);
    }
#endif
    ++ticks;
#if defined(ENG_SCENES) && ENG_SCENES
    /* The timer restarts with every race, cold load or retry: it runs only while finished. */
    if (!(body.flags & SH2Z_VD_FINISHED)) finish_timer = 0;
    else if (finish_timer < 0xFFFFu) {
        ++finish_timer;
        if (fzx_drive_finish_hook) {
#if E1_FIELD && ENG_BAL_EARLY
            fzx_drive_field_run(); /* the hook reads the field: step it to this tick first */
#endif
            fzx_drive_finish_hook(finish_timer, ticks);
        }
    }
#endif
    if (ticks % CHECKPOINT_EVERY == 0u && ticks / CHECKPOINT_EVERY <= CHECKPOINTS) {
        const uint64_t h = fnv64_body(&body);
        fzx_e1_checkpoints[ticks / CHECKPOINT_EVERY - 1u][0] = (uint32_t)(h >> 32);
        fzx_e1_checkpoints[ticks / CHECKPOINT_EVERY - 1u][1] = (uint32_t)h;
        fzx_e1_report[8] = ticks / CHECKPOINT_EVERY;
    }
}
#endif

#if ENG_PHYS_AHEAD && ENG_ATTACKS
/* The pad word of this frame (the override the scripts poke, else the Saturn pad). */
static uint16_t pad_read(void) {
    uint16_t pad = 0;
    const uint32_t o = fzx_e1_pad_override;
    if (o & 0x10000u) pad = (uint16_t)o;
    else {
        smpc_peripheral_digital_t d = {0};
        smpc_peripheral_process();
        smpc_peripheral_digital_port(1, &d);
        if (d.connected) pad = d.pressed.raw; /* Yaul: "pressed" is the current state */
    }
    return pad;
}
#if ENG_ATTACKS
/* The prediction steps with the contact entry and a zero contact (the same call as a deferred tick's), never a second
 * physics entry: the zero contact is passed through an opaque pointer so the compiler does not clone the step for a constant
 * (a plain-step clone costs 30 KB of code, which the scenes image does not have). */
static const sh2z_vd_contact_t ahead_zero_contact;
static inline void ahead_step(sh2z_vd_body_t *b, const sh2z_vd_input_t *in) {
    const sh2z_vd_contact_t *z = &ahead_zero_contact;
    __asm__ volatile("" : "+r"(z));
    phys_step_contact(&course, b, &fzx_phys_rules, in, z);
}
#else
static inline void ahead_step(sh2z_vd_body_t *b, const sh2z_vd_input_t *in) { fzx_phys_step(&course, b, &fzx_phys_rules, in); }
#endif
void fzx_drive_ahead_prepare(uint32_t req) {
    ahead.valid = 0;
#if ENG_PICK_AHEAD
    ahead.view = 0; ahead.kept = 0;
#endif
    if (!req || req > AHEAD_MAX) return;
#if ENG_ATTACKS
    if (ATK_HUNTER_ON()) return;   /* the scripted hunter reads the field and the attack state: never predicted */
#endif
    sh2z_vd_body_t b = body;
    uint16_t pv = prev_n64;
    uint32_t k = 0, t = ticks;
    if (fzx_e1_driver) {
        for (; k < req && t < RACE_END_TICK; ++k, ++t) {
            uint16_t button = t >= RACE_START_TICK ? N64_A : 0u;
            signed char sx = 0;
            if (b.intro == 0) {
#if E1_DRIVER == 2
                sx = fzx_e1_driver == 2u ? fzx_drive_ahead(&course.track, &b, 0.1f, &button) : driver_stick_of(&b);
#else
                sx = driver_stick_of(&b);
#endif
                if (E1_DRIVER_BOOST > 0 && t % E1_DRIVER_BOOST == 0u) button |= N64_B;
            }
            sh2z_vd_input_t in;
            in.held = fzx_phys_buttons_n64(button);
            in.pressed = fzx_phys_buttons_n64((uint16_t)((pv ^ button) & button));
            in.released = fzx_phys_buttons_n64((uint16_t)((pv ^ button) & pv));
            in.stick_x = sx; in.stick_y = 0;
            pv = button;
            ahead_step(&b, &in);
            ahead.after[k] = b;
        }
    } else {
        FzxPadState ps = pad_state;
        sh2z_vd_input_t in;
        ahead.pad = pad_read();
        fzx_phys_pad(&ps, ahead.pad, &in);
        for (; k < req; ++k) {
            ahead_step(&b, &in);
            ahead.after[k] = b;
            in.pressed = 0; in.released = 0;
        }
    }
    ahead.req = req; ahead.n = k; ahead.cursor = 0; ahead.from_ticks = ticks; ahead.valid = 1;
    ++fzx_e1_ahead[0];
}
#endif
#if ENG_PHYS_AHEAD && !ENG_ATTACKS
/* The pad word of this frame (the override the scripts poke, else the Saturn pad). */
static uint16_t pad_read(void) {
    uint16_t pad = 0;
    const uint32_t o = fzx_e1_pad_override;
    if (o & 0x10000u) pad = (uint16_t)o;
    else {
        smpc_peripheral_digital_t d = {0};
        smpc_peripheral_process();
        smpc_peripheral_digital_port(1, &d);
        if (d.connected) pad = d.pressed.raw; /* Yaul: "pressed" is the current state */
    }
    return pad;
}

/* ENG_PHYS_AHEAD (claude/speed-5): the next frame's physics ticks are computed in the master's idle time at the end of
 * this frame (fzx_drive_ahead_prepare, called by the loop while VDP1's previous frame is still being drawn) on a copy of the
 * player's state, and the next fzx_drive_step takes them instead of stepping: the player's state, the tick count, the sound's
 * read, the field's copy, the finish hook and the checkpoints all happen there, in the same order and on the same states as
 * before, so nothing else sees a state ahead of the picture. A step that finds the prediction wrong (another tick count, the
 * race restarted, another pad word) throws the copy away and steps as before; the pad is read again at the step, so a held
 * button is never older than before. */
enum { AHEAD_MAX = 4u };
static struct {
    uint32_t valid, req, n, from_ticks;
    uint16_t pad;
    FzxPadState pad_after;
    uint16_t prev_after[AHEAD_MAX];
    sh2z_vd_body_t after[AHEAD_MAX];
#if ENG_PICK_AHEAD
    /* ENG_PICK_AHEAD: the camera of the state the prediction ends in, computed with the lens and orbit named here, and the
     * tick and outer bit the next frame will show; view = a camera was computed, kept = the step took the prediction. */
    fzx_engine_cam_t cam;
    uint32_t view, kept, limited, orbit_key, tick_after, outer_after;
    int32_t lens_key[3];
#endif
} ahead;
/* [0] prepared, [1] taken, [2] pad word differed, [3] tick count or start differed, [4] master FRT units spent preparing,
 * [5] the longest prepare (FRT units), [6] frames the loop did not prepare (VDP1 idle). */
volatile uint32_t fzx_e1_ahead[8];
void fzx_drive_ahead_prepare(uint32_t req) {
    ahead.valid = 0;
#if ENG_PICK_AHEAD
    ahead.view = 0; ahead.kept = 0;
#endif
    if (!req || req > AHEAD_MAX) return;
    sh2z_vd_body_t b = body;
    uint16_t pv = prev_n64;
    uint32_t k = 0, t = ticks;
    if (fzx_e1_driver) {
        for (; k < req && t < RACE_END_TICK; ++k, ++t) {
            uint16_t button = t >= RACE_START_TICK ? N64_A : 0u;
            signed char sx = 0;
            if (b.intro == 0) {
#if E1_DRIVER == 2
                sx = fzx_e1_driver == 2u ? fzx_drive_ahead(&course.track, &b, 0.1f, &button) : driver_stick_of(&b);
#else
                sx = driver_stick_of(&b);
#endif
                if (E1_DRIVER_BOOST > 0 && t % E1_DRIVER_BOOST == 0u) button |= N64_B;
            }
            sh2z_vd_input_t in;
            in.held = fzx_phys_buttons_n64(button);
            in.pressed = fzx_phys_buttons_n64((uint16_t)((pv ^ button) & button));
            in.released = fzx_phys_buttons_n64((uint16_t)((pv ^ button) & pv));
            in.stick_x = sx; in.stick_y = 0;
            pv = button;
            fzx_phys_step(&course, &b, &fzx_phys_rules, &in);
            ahead.after[k] = b; ahead.prev_after[k] = pv;
        }
    } else {
        FzxPadState ps = pad_state;
        sh2z_vd_input_t in;
        ahead.pad = pad_read();
        fzx_phys_pad(&ps, ahead.pad, &in);
        for (; k < req; ++k) {
            fzx_phys_step(&course, &b, &fzx_phys_rules, &in);
            ahead.after[k] = b;
            in.pressed = 0; in.released = 0;
        }
        ahead.pad_after = ps;
    }
    ahead.req = req; ahead.n = k; ahead.from_ticks = ticks; ahead.valid = 1;
    ++fzx_e1_ahead[0];
}
#endif

#if ENG_PHYS_AHEAD && !ENG_ATTACKS
void fzx_drive_step(uint32_t n) {
#if ENG_PHYS_AHEAD
    bool taken = false;
    if (ahead.valid) {
        const bool same = ahead.from_ticks == ticks && ahead.req == n;
        ahead.valid = 0;
        if (!same) ++fzx_e1_ahead[3];
        else if (!fzx_e1_driver && pad_read() != ahead.pad) ++fzx_e1_ahead[2];
        else {
            if (!fzx_e1_driver) pad_state = ahead.pad_after;
            for (uint32_t k = 0; k < ahead.n; ++k) {
                body = ahead.after[k];
                if (fzx_e1_driver) prev_n64 = ahead.prev_after[k];
                after_step();
                if (fzx_e1_driver && ticks == RACE_END_TICK) {
                    const uint64_t h = fnv64_body(&body);
                    fzx_e1_report[5] = ticks; fzx_e1_report[6] = (uint32_t)(h >> 32); fzx_e1_report[7] = (uint32_t)h;
                }
            }
            ++fzx_e1_ahead[1];
            taken = true;
#if ENG_PICK_AHEAD
            ahead.kept = 1;
#endif
        }
    }
    if (!taken)
#endif
    {
    if (fzx_e1_driver) {
        for (uint32_t k = 0; k < n && ticks < RACE_END_TICK; ++k) {
            uint16_t button = ticks >= RACE_START_TICK ? N64_A : 0u;
            signed char sx = 0;
            if (body.intro == 0) {
#if E1_DRIVER == 2
                sx = fzx_e1_driver == 2u ? fzx_drive_ahead(&course.track, &body, 0.1f, &button) : driver_stick();
#else
                sx = driver_stick();
#endif
                if (E1_DRIVER_BOOST > 0 && ticks % E1_DRIVER_BOOST == 0u) button |= N64_B;
            }
            sh2z_vd_input_t in;
            in.held = fzx_phys_buttons_n64(button);
            in.pressed = fzx_phys_buttons_n64((uint16_t)((prev_n64 ^ button) & button));
            in.released = fzx_phys_buttons_n64((uint16_t)((prev_n64 ^ button) & prev_n64));
            in.stick_x = sx; in.stick_y = 0;
            prev_n64 = button;
            step_once(&in);
            if (ticks == RACE_END_TICK) {
                const uint64_t h = fnv64_body(&body);
                fzx_e1_report[5] = ticks; fzx_e1_report[6] = (uint32_t)(h >> 32); fzx_e1_report[7] = (uint32_t)h;
            }
        }
    } else if (n) {
        const uint16_t pad = pad_read();
        sh2z_vd_input_t in;
        fzx_phys_pad(&pad_state, pad, &in);
        for (uint32_t k = 0; k < n; ++k) {
            step_once(&in);
            in.pressed = 0; in.released = 0; /* one edge per press: the later ticks hold */
        }
    }
    }
#else
void fzx_drive_step(uint32_t n) {
#if ENG_PHYS_AHEAD
    if (ahead.valid && !(ahead.from_ticks == ticks && ahead.req == n)) { ahead.valid = 0; ++fzx_e1_ahead[3]; }
#endif
    if (fzx_e1_driver) {
        for (uint32_t k = 0; k < n && ticks < RACE_END_TICK; ++k) {
            uint16_t button = ticks >= RACE_START_TICK ? N64_A : 0u;
            signed char sx = 0;
            if (body.intro == 0) {
#if E1_DRIVER == 2
                sx = fzx_e1_driver == 2u ? fzx_drive_ahead(&course.track, &body, 0.1f, &button) : driver_stick();
#else
                sx = driver_stick();
#endif
                if (E1_DRIVER_BOOST > 0 && ticks % E1_DRIVER_BOOST == 0u) button |= N64_B;
            }
#if ENG_ATTACKS
            /* fzx_e1_driver 2: the scripted hunter (engine_attacks.c), the 5-KO scenario. */
            if (ATK_HUNTER_ON() && body.intro == 0) {
                uint16_t hb; int8_t hs;
#if E1_FIELD && ENG_BAL_EARLY
                fzx_drive_field_run(); /* the hunter reads the field: step it to this tick first */
#endif
                (void)fzx_atk_hunter(&body, &hb, &hs);
                if (ticks >= RACE_START_TICK) button = hb;
                sx = hs;
            }
#endif
            sh2z_vd_input_t in;
            in.held = fzx_phys_buttons_n64(button);
            in.pressed = fzx_phys_buttons_n64((uint16_t)((prev_n64 ^ button) & button));
            in.released = fzx_phys_buttons_n64((uint16_t)((prev_n64 ^ button) & prev_n64));
            in.stick_x = sx; in.stick_y = 0;
            prev_n64 = button;
            step_once(&in);
            if (ticks == RACE_END_TICK) {
                const uint64_t h = fnv64_body(&body);
                fzx_e1_report[5] = ticks; fzx_e1_report[6] = (uint32_t)(h >> 32); fzx_e1_report[7] = (uint32_t)h;
            }
        }
    } else if (n) {
#if ENG_PHYS_AHEAD
        const uint16_t pad = pad_read();
        if (ahead.valid && pad != ahead.pad) { ahead.valid = 0; ++fzx_e1_ahead[2]; }
#else
        uint16_t pad = 0;
        const uint32_t o = fzx_e1_pad_override;
        if (o & 0x10000u) pad = (uint16_t)o;
        else {
            smpc_peripheral_digital_t d = {0};
            smpc_peripheral_process();
            smpc_peripheral_digital_port(1, &d);
            if (d.connected) pad = d.pressed.raw; /* Yaul: "pressed" is the current state */
        }
#endif
        sh2z_vd_input_t in;
        fzx_phys_pad(&pad_state, pad, &in);
        for (uint32_t k = 0; k < n; ++k) {
            step_once(&in);
            in.pressed = 0; in.released = 0; /* one edge per press: the later ticks hold */
        }
    }
#if ENG_PHYS_AHEAD
    ahead.valid = 0;
#endif
#endif
    fzx_e1_report[0] = ticks; fzx_e1_report[1] = (uint32_t)body.laps_done; fzx_e1_report[2] = (uint32_t)body.race_time;
    fzx_e1_report[3] = (body.flags & SH2Z_VD_FINISHED) ? 1u : 0u; fzx_e1_report[4] = (uint32_t)body.speed;
    fzx_e1_report[13] = (uint32_t)body.lap_times[0]; fzx_e1_report[14] = (uint32_t)body.lap_times[1];
    fzx_e1_report[15] = (uint32_t)body.lap_times[2];
}

uint32_t fzx_drive_tick(void) { return ticks; }
#if FZX_PHYS_TUBES
/* Whether the camera is outside the track surface (the road plan's outer bit; course.c
 * D_800F89D8): inside a pipe or tunnel span it is not, except over the span's joins, where the
 * tube opens into road. Read from the player's located span and t. */
uint8_t fzx_drive_outer(void) {
    const uint32_t n = body.node < course.track.count ? body.node : 0u;
    /* TRACK_FLAG_INSIDE is set on the pipe and tunnel presets and on no other shape (course.c D_800CF9F0,
     * D_800CFA10): the half-pipe and the cylinder are seen from outside. */
    if (course.nodes[n].shape != SH2Z_VD_SHAPE_PIPE && course.nodes[n].shape != SH2Z_VD_SHAPE_TUNNEL) return 1u;
    return (body.t < course.joins[n].prev_end || course.joins[n].next_start < body.t) ? 1u : 0u;
}
#endif
#if E1_CLOSEUP == 1 || E1_FIELD
/* Arm A (engine_closeup.c) poses the 3D model from the body itself. */
const sh2z_vd_body_t *fzx_drive_body(void) { return &body; }
#endif

/* ---- chase camera --------------------------------------------------------------
 * Fitted to the race camera (camera 0) against the car over ticks 600..6900 of the
 * reference R03 replay, in the car's forward/up frame: eye 98.869 back and 60.294 up,
 * look-at 24.190 ahead and 24.172 down (sd 1.6 to 3.5 units). The rotation is the
 * car frame pitched down by atan(84.466 / 123.059). First cut: rigid, no lag. */
#define QF(x, bits) ((int32_t)((x) * (double)(1 << (bits)) + ((x) >= 0 ? 0.5 : -0.5)))
#define CAM_DX 123.059
#define CAM_DY 84.466
#define CAM_LEN 149.2577 /* sqrt(DX^2 + DY^2) */
static const int32_t EYE_F = QF(-98.869, 16), EYE_U = QF(60.294, 16);
static const int32_t PITCH_C = QF(CAM_DX / CAM_LEN, 30), PITCH_S = QF(CAM_DY / CAM_LEN, 30);
static const int32_t ROLL_C = QF(0.920504853, 30), ROLL_S = QF(0.390731128, 30); /* cos, sin 23 degrees */
volatile uint32_t fzx_e1_roll_limited; /* frames whose camera roll was limited */
#if E1_CLOSEUP == 1
volatile uint32_t fzx_e1_cam_orbit; /* stills: see fzx_drive_camera */
#endif

static int32_t mul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }
static uint32_t isqrt64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return (uint32_t)r;
}

#if ENG_PICK_AHEAD
/* ENG_PICK_AHEAD: the camera of any body (the ahead stage computes the next frame's); BP is the body it reads. */
static void camera_of(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens, const sh2z_vd_body_t *cam_body, uint32_t *roll_limited) {
#define BP cam_body
#define ROLL_LIMITED_INC (++*roll_limited)
#else
void fzx_drive_camera(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens) {
#define BP (&body)
#define ROLL_LIMITED_INC (++fzx_e1_roll_limited)
#endif
#if E1_CLOSEUP == 1
    /* Stills only: fzx_e1_cam_orbit 1 swings the chase camera 135 degrees around the car's
     * up axis (a front three-quarter view), 2 by 90 degrees (side); 0 is the chase camera. */
    int32_t orbit_f[3];
    const int32_t *f = BP->body[0], *u = BP->body[1];
    if (fzx_e1_cam_orbit == 1u || fzx_e1_cam_orbit == 2u) {
        const int32_t oc = fzx_e1_cam_orbit == 1u ? -759250125 : 0, os = fzx_e1_cam_orbit == 1u ? 759250125 : 1073741824;
        for (unsigned i = 0; i < 3u; ++i) orbit_f[i] = mul30(oc, f[i]) + mul30(os, BP->body[2][i]);
        f = orbit_f;
    }
#else
    const int32_t *f = BP->body[0], *u = BP->body[1];
#endif
    int32_t cf[3], cu[3], r0[3];
    for (unsigned i = 0; i < 3u; ++i) {
        cam->eye[i] = BP->pos[i] + mul30(EYE_F, f[i]) + mul30(EYE_U, u[i]);
        cf[i] = mul30(PITCH_C, f[i]) - mul30(PITCH_S, u[i]);
        cu[i] = mul30(PITCH_S, f[i]) + mul30(PITCH_C, u[i]);
    }
    r0[0] = (int32_t)(((int64_t)cf[1] * cu[2] - (int64_t)cf[2] * cu[1]) >> 30);
    r0[1] = (int32_t)(((int64_t)cf[2] * cu[0] - (int64_t)cf[0] * cu[2]) >> 30);
    r0[2] = (int32_t)(((int64_t)cf[0] * cu[1] - (int64_t)cf[1] * cu[0]) >> 30);
    {
        /* Roll limit: the race camera never rolls past 23 degrees (reference camera 0,
         * whole R03 race: |roll| max 23.0), and the VDP2 floor mapping refuses some
         * views rolled further (fault 2 in the half-pipe). h is the right row made
         * level, v = h x cf the zero-roll up; past the limit the right row becomes
         * cos(L) h + sin(L) v on the same side, and up = right x forward. */
        const uint32_t hn = isqrt64((uint64_t)((int64_t)r0[0] * r0[0] + (int64_t)r0[2] * r0[2]));
        if (hn > (1u << 24)) {
            const int32_t h[3] = { (int32_t)(((int64_t)r0[0] << 30) / (int64_t)hn), 0,
                                   (int32_t)(((int64_t)r0[2] << 30) / (int64_t)hn) };
            int32_t v[3];
            v[0] = (int32_t)(((int64_t)h[1] * cf[2] - (int64_t)h[2] * cf[1]) >> 30);
            v[1] = (int32_t)(((int64_t)h[2] * cf[0] - (int64_t)h[0] * cf[2]) >> 30);
            v[2] = (int32_t)(((int64_t)h[0] * cf[1] - (int64_t)h[1] * cf[0]) >> 30);
            if ((int64_t)v[0] * cu[0] + (int64_t)v[1] * cu[1] + (int64_t)v[2] * cu[2] < 0) { v[0] = -v[0]; v[1] = -v[1]; v[2] = -v[2]; }
            const int32_t s = (int32_t)(((int64_t)r0[0] * v[0] + (int64_t)r0[1] * v[1] + (int64_t)r0[2] * v[2]) >> 30);
            if (s > ROLL_S || s < -ROLL_S) {
                const int32_t sl = s > 0 ? ROLL_S : -ROLL_S;
                for (unsigned i = 0; i < 3u; ++i) r0[i] = mul30(ROLL_C, h[i]) + mul30(sl, v[i]);
                cu[0] = (int32_t)(((int64_t)r0[1] * cf[2] - (int64_t)r0[2] * cf[1]) >> 30);
                cu[1] = (int32_t)(((int64_t)r0[2] * cf[0] - (int64_t)r0[0] * cf[2]) >> 30);
                cu[2] = (int32_t)(((int64_t)r0[0] * cf[1] - (int64_t)r0[1] * cf[0]) >> 30);
                ROLL_LIMITED_INC;
            }
        }
    }
    for (unsigned j = 0; j < 3u; ++j) { cam->rotation[0][j] = r0[j]; cam->rotation[1][j] = cu[j]; cam->rotation[2][j] = cf[j]; }
    for (unsigned i = 0; i < 3u; ++i) {
        const int64_t d = (int64_t)cam->rotation[i][0] * cam->eye[0] + (int64_t)cam->rotation[i][1] * cam->eye[1] +
                          (int64_t)cam->rotation[i][2] * cam->eye[2];
        cam->translation[i] = (int32_t)(-(d >> 34)); /* Q16 at 1/16 source scale */
    }
    cam->focal[0] = lens->focal[0]; cam->focal[1] = lens->focal[1]; cam->tan_half = lens->tan_half;
    const uint32_t n = isqrt64((uint64_t)((int64_t)cf[0] * cf[0] + (int64_t)cf[2] * cf[2]));
    if (n) {
        cam->xzn[0] = (int32_t)(((int64_t)cf[0] << 30) / (int64_t)n);
        cam->xzn[1] = (int32_t)(((int64_t)cf[2] << 30) / (int64_t)n);
    } else { cam->xzn[0] = 1 << 30; cam->xzn[1] = 0; }
}
#undef ROLL_LIMITED_INC
#undef BP
#if ENG_PICK_AHEAD
void fzx_drive_camera(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens) {
    uint32_t limited = 0;
    camera_of(cam, lens, &body, &limited);
    fzx_e1_roll_limited += limited;
}
#endif
#if ENG_PICK_AHEAD
#if !ENG_PHYS_AHEAD
#error ENG_PICK_AHEAD needs ENG_PHYS_AHEAD
#endif
/* The next frame's camera, tick and outer bit from the state the prediction ends in. False when there is no prediction. */
bool fzx_drive_ahead_view(const fzx_engine_cam_t *lens, const fzx_engine_cam_t **cam, uint32_t *tick, uint32_t *outer) {
    if (!ahead.valid || !ahead.n) return false;
    const sh2z_vd_body_t *b = &ahead.after[ahead.n - 1u];
#if ENG_PICK_AHEAD_CONTROL == 2
    b = &body; /* control (diagnostic only): the camera of the state before the prediction, used as if it were the predicted one:
                * the pick sees a stale view and the picture must differ */
#endif
    uint32_t limited = 0;
    camera_of(&ahead.cam, lens, b, &limited);
    ahead.limited = limited;
    ahead.lens_key[0] = lens->focal[0]; ahead.lens_key[1] = lens->focal[1]; ahead.lens_key[2] = lens->tan_half;
#if E1_CLOSEUP == 1
    ahead.orbit_key = fzx_e1_cam_orbit;
#else
    ahead.orbit_key = 0;
#endif
    ahead.tick_after = ahead.from_ticks + ahead.n;
    ahead.tick_after = ahead.tick_after ? ahead.tick_after : 1u;
#if FZX_PHYS_TUBES
    {   /* fzx_drive_outer() on the predicted state */
        const uint32_t n = b->node < course.track.count ? b->node : 0u;
        uint32_t o = 1u;
        if (course.nodes[n].shape == SH2Z_VD_SHAPE_PIPE || course.nodes[n].shape == SH2Z_VD_SHAPE_TUNNEL)
            o = (b->t < course.joins[n].prev_end || course.joins[n].next_start < b->t) ? 1u : 0u;
        ahead.outer_after = o;
    }
#else
    ahead.outer_after = 1u;
#endif
    ahead.view = 1;
    *cam = &ahead.cam; *tick = ahead.tick_after; *outer = ahead.outer_after;
    return true;
}
/* This frame's camera: the prediction's when the step took it (so the state is the one it was computed from) and the lens and
 * orbit are the same; else computed now. True: the camera, tick and outer bit are the ones the early pick used. */
bool fzx_drive_camera_ahead(fzx_engine_cam_t *cam, const fzx_engine_cam_t *lens) {
    const bool use = ahead.kept && ahead.view && ahead.lens_key[0] == lens->focal[0] && ahead.lens_key[1] == lens->focal[1] &&
                     ahead.lens_key[2] == lens->tan_half &&
#if E1_CLOSEUP == 1
                     ahead.orbit_key == fzx_e1_cam_orbit &&
#endif
                     ahead.tick_after == (ticks ? ticks : 1u);
    ahead.kept = 0; ahead.view = 0;
#if ENG_PICK_AHEAD_CONTROL == 1
    /* control (diagnostic only): every third early pick is declared unused, so the road job undoes it and picks again; the
     * picture must equal the default's */
    static uint32_t control_n;
    if (use && ++control_n % 3u == 0u) { fzx_drive_camera(cam, lens); return false; }
#endif
    if (!use) { fzx_drive_camera(cam, lens); return false; }
    *cam = ahead.cam; /* focal and tan_half are the lens's: the key matched */
    fzx_e1_roll_limited += ahead.limited;
    return true;
}
#endif


/* ---- player sprite (claude/racer-sprites 45b74b7 draw16 path, through the slot cache) --
 * SPRITE-RACERS-PLAN.md section 9.4 (SH2ZAM sprite-cache-1):
 * - the raw RSP1 set lives in the cartridge; its header and cell table are copied to
 *   work RAM for the pose, its CLUTs to the VDP1 CLUT partition (one 32 B entry per
 *   resident set, 60 reserved), beside the 320 B blob shadow;
 * - cells are drawn from an 89,600 B slot pool at the top of the texture partition;
 * - the master selects (get), emits and queues misses; the slave copies queued cells
 *   from the cartridge into their slots right after the track build (1 ms budget), and
 *   the master sees them READY the next frame. Slot states are read and written
 *   through the cache-through view on both CPUs. */
#define MODEL_SCALE_Q14 1638 /* the source's machine model scale 0.1, Q1.14 */
#define SHADOW_W 32u
#define SHADOW_H 16u
#define VDP1_VRAM_BASE 0x25C00000u
#define CT(p) ((__typeof__(p))((uintptr_t)(p) | CPU_CACHE_THROUGH))
#if E1_CLOSEUP == 2
/* Arm B: the near set's header and 128-cell table (2,216 B) need a larger work RAM copy. */
enum { CLUT_SETS = 60u, POOL_SLOTS = 90u, FILL_MAX = 32u, SET_TABLE_BYTES = 4096u, FILL_BUDGET_TICKS = 210u /* 1 ms at FRT/128 */ };
volatile uint32_t fzx_ab_cell[3] = { 0xffffu, 0u, 0u }; /* last cell, changes, last scale (Q16 screen px per cell px) */
volatile uint32_t fzx_ab_cell_frames[128];           /* frames per cell */
#else
enum { CLUT_SETS = 60u, POOL_SLOTS = 90u, FILL_MAX = 32u, SET_TABLE_BYTES = 2048u, FILL_BUDGET_TICKS = 210u /* 1 ms at FRT/128 */ };
#endif
static const uint32_t pool_sizes[4] = { 512u, 1024u, 1536u, 2304u }, pool_counts[4] = { 40u, 24u, 20u, 6u };
typedef struct {sh2z_rspr_set_t header;sh2z_rspr_cell_t cell;uint16_t clut[16];uint8_t pixels[SHADOW_W*SHADOW_H/2];} shadow_set_t;
static const sh2z_rspr_set_t *racer_set, *shadow_set; /* racer_set: header and cells in work RAM */
static uintptr_t set_cart;                            /* the whole raw set in the cartridge */
static uint32_t shadow_vram, clut_vram;
static uint16_t racer_pmod, shadow_pmod;
static sh2z_rspr_camera_t racer_camera, shadow_camera;
static uint32_t set_table[SET_TABLE_BYTES / 4u];
static sh2z_scache_slot_t pool_slots[POOL_SLOTS];
static sh2z_scache_t pool;
#if E2_ROSTER
typedef struct { uint16_t slot, cell; const sh2z_rspr_set_t *set; } fill_t; /* set NULL: the player's set */
#else
typedef struct { uint16_t slot, cell; } fill_t;
#endif
static fill_t fill_queue[FILL_MAX];
static volatile uint32_t fill_count, fill_done;
static uint32_t drawn_slot = UINT32_MAX, drawn_cell; /* the player's last drawn cell */
/* VDP1 map (byte offsets in VDP1 VRAM): [0..1] slot pool, [2] slots, [3..4] set CLUTs,
 * [5..6] shadow, [7] set in the cartridge, [8] set bytes, [9..10] texture partition,
 * [11..12] CLUT partition, [13] dbank stage end, [14] lowest E0 texture end allowed. */
volatile uint32_t fzx_e1_vram[16];
/* Cache: hits, misses, evictions, refused, previous-cell draws, fills, fill bytes,
 * fills left for the next frame, abandoned, slave fill ticks (last), max. */
volatile uint32_t fzx_e1_cache[12];
/* The player sprite this frame: drawn corner A x, y (-32768 none), origin x, its sub-pixel x. */
volatile uint32_t fzx_e1_sprite_pos[4];

static uint32_t cell_bytes(uint32_t cell) {
    const sh2z_rspr_cell_t *c = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1) + cell;
    return (uint32_t)c->width * c->height / 2u;
}

static bool shadow_build(void *vram) {
    static shadow_set_t t;memset(&t,0,sizeof(t));
    t.header.magic=SH2Z_RSPR_MAGIC;t.header.bytes=(uint32_t)sizeof(t);t.header.yaw_count=1;t.header.pitch_count=1;
    t.header.clut_count=1;t.header.clut_offset=(uint32_t)offsetof(shadow_set_t,clut);
    t.header.pixels_per_unit=(int32_t)((float)SHADOW_W*65536.0f/(1.4f*FZX_RACER_SPRITE_RADIUS));
    t.header.yaw[0][1]=16384;t.header.pitch[0][1]=16384;
    t.cell=(sh2z_rspr_cell_t){.width=SHADOW_W,.height=SHADOW_H,.origin_x=SHADOW_W/2,.origin_y=SHADOW_H/2,
        .pixel_offset=(uint32_t)offsetof(shadow_set_t,pixels),.clut=0};
    t.clut[1]=0x8000u;
    for(unsigned y=0;y<SHADOW_H;++y)for(unsigned x=0;x<SHADOW_W;++x) {
        const int dx=(int)(2*x+1)-(int)SHADOW_W,dy=(int)(2*y+1)-(int)SHADOW_H;
        if(dx*dx*(int)(SHADOW_H*SHADOW_H)+dy*dy*(int)(SHADOW_W*SHADOW_W)<=(int)(SHADOW_W*SHADOW_W*SHADOW_H*SHADOW_H))
            t.pixels[(y*SHADOW_W+x)/2]|=(uint8_t)((x&1u)?0x01u:0x10u);
    }
    memcpy(vram,&t,sizeof(t));
    shadow_set=(const sh2z_rspr_set_t *)vram;shadow_vram=(uint32_t)((uintptr_t)vram&0x7ffffu);
    return sh2z_rspr_valid(shadow_set);
}

bool fzx_drive_load(void *scratch, uint32_t scratch_bytes) {
    fzx_e1_report[12] = CSEL(phys_bytes,FZX_PHYSICS_PACK_BYTES);
    if (!fzx_texture_cd_load(CNAME(FZX_CF_PHYS,"PHYS.PAK"), CSEL(phys_bytes,FZX_PHYSICS_PACK_BYTES), CSEL(phys_hash,FZX_PHYSICS_PACK_HASH), pack_buf, scratch, scratch_bytes))
        { fzx_e1_status = 2; return false; }
    if (fzx_phys_load(&course, (const uint8_t *)pack_buf, CSEL(phys_bytes,FZX_PHYSICS_PACK_BYTES), 1) != 0) { fzx_e1_status = 3; return false; }
#ifndef FZX_P3_BASELINE
#if ENG_SCENES
    if (!fzx_machine_tune(&course, fzx_session.machine_id, fzx_session.slider_bits)) {
        fzx_e1_status = 11;
        return false;
    }
#endif
#endif
    body = course.start; prev_n64 = 0; ticks = 0;
#if ENG_PHYS_AHEAD
    ahead.valid = 0;
#if ENG_PICK_AHEAD
    ahead.view = 0; ahead.kept = 0;
#endif
#endif
    memset(&pad_state, 0, sizeof(pad_state));
#if E1_FIELD
#if E2_PACK
    if (!fzx_field_pack_load(scratch, scratch_bytes)) { fzx_e1_status = 10; return false; }
#endif
    if (!fzx_field_load(&course.track, &body)) { fzx_e1_status = 9; return false; }
#endif
    return true;
}

bool fzx_drive_sprites_load(const vdp1_vram_partitions_t *parts, uint32_t clut_used, uintptr_t cart_free,
                            uintptr_t cart_limit, void *scratch, uint32_t scratch_bytes) {
    /* The raw set into the cartridge (CPU stores; no DMA with a cartridge endpoint). */
    set_cart = (cart_free + 63u) & ~(uintptr_t)63u;
    if (set_cart + FZX_RACER_SPRITE_BYTES + 32u > cart_limit ||
        !fzx_texture_cd_load("RSPRITE.PAK", FZX_RACER_SPRITE_BYTES, FZX_RACER_SPRITE_HASH, (void *)set_cart, scratch, scratch_bytes))
        { fzx_e1_status = 4; return false; }
    const sh2z_rspr_set_t *full = (const sh2z_rspr_set_t *)set_cart;
    if (!sh2z_rspr_valid(full)) { fzx_e1_status = 5; return false; }
    const uint32_t table = (uint32_t)sizeof(sh2z_rspr_set_t) + (uint32_t)full->yaw_count * full->pitch_count * (uint32_t)sizeof(sh2z_rspr_cell_t);
    const uint32_t cluts = (uint32_t)full->clut_count * 32u;
    if (table > sizeof(set_table) || full->clut_count > CLUT_SETS) { fzx_e1_status = 5; return false; }
    memcpy(set_table, full, table);
    racer_set = (const sh2z_rspr_set_t *)(const void *)set_table;
    /* CLUT partition: E0's bank stages first, then the set CLUTs (60 reserved), then the shadow. */
    const uintptr_t clut_base = (uintptr_t)parts->clut_base, clut_end = clut_base + parts->clut_size;
    const uintptr_t cl = (clut_base + clut_used + 31u) & ~(uintptr_t)31u, cl_end = cl + CLUT_SETS * 32u;
#if ENG_CLUT_ALIGN
    /* The shadow's lookup table at a 32-byte VDP1 address: VDP1 ignores CMDCOLR's two low bits. */
    _Static_assert(!(offsetof(shadow_set_t, clut) & 7u), "shadow set layout");
    const uintptr_t sh = ((cl_end + offsetof(shadow_set_t, clut) + 31u) & ~(uintptr_t)31u) - offsetof(shadow_set_t, clut);
    const uintptr_t sh_end = sh + ((sizeof(shadow_set_t) + 7u) & ~7u);
#else
    const uintptr_t sh = (cl_end + 7u) & ~(uintptr_t)7u, sh_end = sh + ((sizeof(shadow_set_t) + 7u) & ~7u);
#endif
    if (sh_end > clut_end) { fzx_e1_status = 6; return false; }
    memcpy((void *)cl, (const void *)(set_cart + full->clut_offset), cluts);
    clut_vram = (uint32_t)(cl & 0x7ffffu);
    if (!shadow_build((void *)sh)) { fzx_e1_status = 6; return false; }
    /* Slot pool at the top of the texture partition. */
    const uint32_t tex_base = (uint32_t)((uintptr_t)parts->texture_base & 0x7ffffu);
    uint32_t pool_bytes = 0;
    for (unsigned k = 0; k < 4u; ++k) pool_bytes += pool_sizes[k] * pool_counts[k];
    const uint32_t pool_base = (tex_base + parts->texture_size - pool_bytes) & ~7u;
    uint32_t pool_end = 0;
    const uint32_t n = sh2z_scache_layout(pool_slots, POOL_SLOTS, pool_base, tex_base + parts->texture_size - pool_base,
                                          pool_sizes, pool_counts, 4u, &pool_end);
    if (!n || !sh2z_scache_init(&pool, CT(&pool_slots[0]), n, 2u /* vdp1-banks-1 double banks */)) { fzx_e1_status = 7; return false; }
    for (uint32_t i = 0; i < full->yaw_count * full->pitch_count; ++i) if (cell_bytes(i) > pool_sizes[3]) { fzx_e1_status = 8; return false; }
    fill_count = 0; fill_done = 0;
    {
        vdp1_cmdt_t mode_cmd; memset(&mode_cmd, 0, sizeof(mode_cmd));
        const vdp1_cmdt_draw_mode_t mode = {.end_code_disable = 1, .user_clipping_mode = 2, .color_mode = VDP1_CMDT_CM_CLUT_16};
        vdp1_cmdt_draw_mode_set(&mode_cmd, mode); racer_pmod = mode_cmd.cmd_pmod;
        const vdp1_cmdt_draw_mode_t half = {.end_code_disable = 1, .user_clipping_mode = 2, .color_mode = VDP1_CMDT_CM_CLUT_16,
                                            .cc_mode = VDP1_CMDT_CC_HALF_TRANSPARENT};
        vdp1_cmdt_draw_mode_set(&mode_cmd, half); shadow_pmod = mode_cmd.cmd_pmod;
    }
    fzx_e1_vram[0] = pool_base; fzx_e1_vram[1] = pool_end; fzx_e1_vram[2] = n;
    fzx_e1_vram[3] = (uint32_t)(cl & 0x7ffffu); fzx_e1_vram[4] = (uint32_t)(cl_end & 0x7ffffu);
    fzx_e1_vram[5] = (uint32_t)(sh & 0x7ffffu); fzx_e1_vram[6] = (uint32_t)(sh_end & 0x7ffffu);
    fzx_e1_vram[7] = (uint32_t)set_cart; fzx_e1_vram[8] = FZX_RACER_SPRITE_BYTES;
    fzx_e1_vram[9] = tex_base; fzx_e1_vram[10] = tex_base + parts->texture_size;
    fzx_e1_vram[11] = (uint32_t)(clut_base & 0x7ffffu); fzx_e1_vram[12] = (uint32_t)(clut_end & 0x7ffffu);
    fzx_e1_vram[13] = (uint32_t)((clut_base + clut_used) & 0x7ffffu); fzx_e1_vram[14] = pool_base - tex_base;
    fzx_e1_report[9] = 1; fzx_e1_status = 1;
    return true;
}

uint32_t fzx_drive_texture_floor(void) { return fzx_e1_vram[14]; }

#ifndef ENG_SCENES
#define ENG_SCENES 0
#endif
#if ENG_SCENES
/* Scenes image: race-local state back to the start of a race, with the packs, the
 * sprite set and the slot pool layout kept (a retry). The slot pool is emptied so
 * a retry fills it exactly as the first race did. */
#if E1_FIELD
static void field_state_reset(void);
#endif
void fzx_drive_reset(void) {
    body = course.start; prev_n64 = 0; ticks = 0; finish_timer = 0;
#if ENG_PHYS_AHEAD
    ahead.valid = 0;
#if ENG_PICK_AHEAD
    ahead.view = 0; ahead.kept = 0;
#endif
#endif
#if E1_FIELD && ENG_BAL_EARLY
    field_pending = 0; /* player states a left race never handed to the field */
#if ENG_ATTACKS
    field_contacts = 0;
#if ENG_ATK_DEFER
    ahead_ok = 0; /* a contact run ahead for a tick the left race never took (its held hooks are dropped) */
#if ENG_ATK_LAG
    lag_have = 0;
#endif
#endif
#endif
#endif
#if E1_FIELD
    /* The rival field back to its grid (the pack is resident; no CD read) and the per-car draw state. */
    (void)fzx_field_load(&course.track, &body);
    field_state_reset();
#endif
    memset(&pad_state, 0, sizeof(pad_state));
    memset((void *)fzx_e1_checkpoints, 0, sizeof(fzx_e1_checkpoints));
    memset((void *)fzx_e1_report, 0, sizeof(fzx_e1_report));
    fzx_e1_report[9] = 1; fzx_e1_report[12] = CSEL(phys_bytes,FZX_PHYSICS_PACK_BYTES);
    if (fzx_e1_status == 1) {
        sh2z_scache_t *const c = CT(&pool);
        const uint32_t n = c->count;
        for (uint32_t i = 0; i < n; ++i) {
            sh2z_scache_slot_t *const slot = CT(&pool_slots[i]);
            slot->key = SH2Z_SCACHE_NO_KEY; slot->last_use = 0; slot->state = SH2Z_SCACHE_FREE;
        }
        (void)sh2z_scache_init(c, CT(&pool_slots[0]), n, 2u);
    }
    fill_count = 0; fill_done = 0;
    drawn_slot = UINT32_MAX; drawn_cell = 0;
    memset((void *)fzx_e1_cache, 0, sizeof(fzx_e1_cache));
}
const sh2z_vd_body_t *fzx_drive_state(void) { return &body; }
#endif

/* Slave, right after the track build: copy queued cells into their slots. */
void fzx_drive_slave_fill(void) {
    if (fzx_e1_status != 1) return;
    const uint16_t t0 = cpu_frt_count_get();
    sh2z_scache_t *const c = CT(&pool);
    const sh2z_rspr_cell_t *const cells = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1);
    const uint32_t count = fill_count;
    uint32_t done = 0, bytes = 0;
    while (done < count) {
        if (done && (uint16_t)(cpu_frt_count_get() - t0) >= FILL_BUDGET_TICKS) break;
        const fill_t f = fill_queue[done];
        if (!sh2z_scache_live(c, f.slot)) { (void)sh2z_scache_abandon(c, f.slot); ++fzx_e1_cache[8]; ++done; continue; }
#if E2_ROSTER && ENG_ROSTER
        if (f.set) {
            /* The scenes image's roster stays compressed: the cell is decoded into its slot. */
            const sh2z_rspr_cell_t *const fc = sh2z_rspr_cells(f.set) + f.cell;
            volatile uint16_t *d16 = (volatile uint16_t *)(VDP1_VRAM_BASE + c->slots[f.slot].addr);
            if (!fzx_roster_fill(f.set, f.cell, d16)) { (void)sh2z_scache_abandon(c, f.slot); ++fzx_e1_cache[8]; ++done; continue; }
            (void)sh2z_scache_ready(c, f.slot);
            bytes += (uint32_t)fc->width * fc->height / 2u; ++done;
            continue;
        }
#endif
#if E2_ROSTER
        const sh2z_rspr_cell_t *const fc = f.set ? sh2z_rspr_cells(f.set) + f.cell : &cells[f.cell];
        const uintptr_t fbase = f.set ? (uintptr_t)f.set : set_cart;
        const uint32_t n = (uint32_t)fc->width * fc->height / 8u; /* longwords */
        const volatile uint32_t *src = (const volatile uint32_t *)(fbase + fc->pixel_offset);
#else
        const uint32_t n = (uint32_t)cells[f.cell].width * cells[f.cell].height / 8u; /* longwords */
        const volatile uint32_t *src = (const volatile uint32_t *)(set_cart + cells[f.cell].pixel_offset);
#endif
        volatile uint32_t *dst = (volatile uint32_t *)(VDP1_VRAM_BASE + c->slots[f.slot].addr);
        for (uint32_t i = 0; i < n; ++i) dst[i] = src[i];
        (void)sh2z_scache_ready(c, f.slot);
        bytes += n * 4u; ++done;
    }
    fill_done = done;
    fzx_e1_cache[5] += done; fzx_e1_cache[6] += bytes; fzx_e1_cache[7] = count - done;
    const uint32_t t = (uint16_t)(cpu_frt_count_get() - t0);
    fzx_e1_cache[9] = t; if (t > fzx_e1_cache[10]) fzx_e1_cache[10] = t;
}

#if E2_ROSTER
static void fill_push_set(uint32_t slot, uint32_t cell, const sh2z_rspr_set_t *set) {
    if (fill_count < FILL_MAX) {
        fill_queue[fill_count].slot = (uint16_t)slot; fill_queue[fill_count].cell = (uint16_t)cell; fill_queue[fill_count].set = set; ++fill_count;
    } else (void)sh2z_scache_abandon(&pool, slot);
}
static void fill_push(uint32_t slot, uint32_t cell) { fill_push_set(slot, cell, 0); }
#else
static void fill_push(uint32_t slot, uint32_t cell) {
    if (fill_count < FILL_MAX) { fill_queue[fill_count].slot = (uint16_t)slot; fill_queue[fill_count].cell = (uint16_t)cell; ++fill_count; }
    else (void)sh2z_scache_abandon(&pool, slot);
}
#endif

static bool sprite_camera(const fzx_view_t *view, const fzx_engine_cam_t *cam) {
    sh2z_rspr_camera_t c; memset(&c, 0, sizeof(c));
    for (unsigned i = 0; i < 3u; ++i) for (unsigned j = 0; j < 3u; ++j) {
        const int32_t n = view->rotation[i][j]; c.r[i][j] = (int16_t)((n >= 0 ? n + 32768 : -(-n + 32768)) / 65536);
    }
    for (unsigned i = 0; i < 3u; ++i) c.eye[i] = cam->eye[i];
    c.focal_x = view->focal[0]; c.focal_y = view->focal[1];
    c.center_x = (int16_t)((view->center[0] + 32768) >> 16); c.center_y = (int16_t)(((view->center[1] + 32768) >> 16) - 1);
    c.near_z = view->near_plane * 4;
    racer_camera = c; shadow_camera = c;
    return sh2z_rspr_camera_scale(&racer_camera, racer_set, MODEL_SCALE_Q14) &&
           sh2z_rspr_camera_scale(&shadow_camera, shadow_set, MODEL_SCALE_Q14);
}

unsigned fzx_drive_sprites(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view, const fzx_engine_cam_t *cam) {
    if (fzx_e1_status != 1 || capacity < 3u) { ++fzx_e1_report[11]; return 0; }
    fzx_e1_sprite_pos[0] = fzx_e1_sprite_pos[1] = (uint32_t)-32768;
    /* The slave's fills of the last job are in: drop them from the queue. */
    {
        const uint32_t done = *CT(&fill_done), count = fill_count;
        uint32_t k = 0;
        for (uint32_t i = done; i < count; ++i) fill_queue[k++] = fill_queue[i];
        fill_count = k; fill_done = 0;
    }
    sh2z_scache_frame(&pool);
    if (!sprite_camera(view, cam)) { ++fzx_e1_report[11]; return 0; }
    int16_t basis[9];
    int32_t pos[3];
    for (unsigned i = 0; i < 3u; ++i) {
        pos[i] = body.pos[i];
        for (unsigned j = 0; j < 3u; ++j) {
            const int32_t v = body.body[i][j];
            basis[i * 3u + j] = (int16_t)((v + (1 << 15)) >> 16); /* Q30 -> Q1.14 */
        }
    }
    unsigned out = 0;
    memset(&commands[out], 0, sizeof(commands[out])); vdp1_cmdt_user_clip_coord_set(&commands[out]);
    commands[out].cmd_xa = view->clip[0]; commands[out].cmd_ya = view->clip[1];
    commands[out].cmd_xc = view->clip[2] - 1; commands[out].cmd_yc = view->clip[3] - 1;
    ++out;
    {
        /* Shadow: down the up axis by 0.3 model radii (Q1.14 basis x Q2 = Q16). */
        const int32_t drop = (int32_t)(0.3f * FZX_RACER_SPRITE_RADIUS * 0.1f * 4.0f);
        int32_t ground[3], depth; sh2z_rspr_pick_t pick; sh2z_rspr_place_t place;
        const sh2z_rspr_pick_t zero = {0};
        for (unsigned k = 0; k < 3u; ++k) ground[k] = pos[k] - basis[3 + k] * drop;
        if (sh2z_rspr_pose(&pick, &place, &depth, &shadow_camera, shadow_set, ground, basis) &&
            sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], shadow_set, &zero, &place, shadow_vram, shadow_pmod)) ++out;
    }
#if E1_CLOSEUP == 1
    /* Arm A: the 3D model follows the shadow (engine_closeup.c); no sprite cell. */
    fzx_e1_report[10] = out - 1u;
    return out;
#endif
    {
        int32_t depth; sh2z_rspr_pick_t pick; sh2z_rspr_place_t place;
        if (sh2z_rspr_pose(&pick, &place, &depth, &racer_camera, racer_set, pos, basis)) {
#if E1_CLOSEUP == 2
            /* Arm B: which cells the player shows (look list: cell changes per run). */
            if (pick.cell != fzx_ab_cell[0]) ++fzx_ab_cell[1];
            fzx_ab_cell[0] = pick.cell; fzx_ab_cell[2] = (uint32_t)place.scale;
            if (pick.cell < 128u) fzx_ab_cell_frames[pick.cell] += 1u;
#endif
            /* Key = racer x 1024 + profile x 256 + cell (player 0, profile 0). */
            uint32_t slot = 0;
            const int r = sh2z_scache_get(&pool, pick.cell, cell_bytes(pick.cell), false, &slot);
            if (r == SH2Z_SCACHE_MISS) fill_push(slot, pick.cell);
            bool draw = r == SH2Z_SCACHE_HIT;
            if (!draw && r != SH2Z_SCACHE_REFUSED && drawn_slot != UINT32_MAX) {
                /* Not in yet: the previous cell, if its slot still holds it. */
                uint32_t again = 0;
                if (sh2z_scache_get(&pool, drawn_cell, cell_bytes(drawn_cell), false, &again) == SH2Z_SCACHE_HIT) {
                    pick.cell = (uint16_t)drawn_cell; slot = again; draw = true; ++fzx_e1_cache[4];
                }
            }
            if (draw && sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], racer_set, &pick, &place, 0u, racer_pmod)) {
                const sh2z_rspr_cell_t *cell = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1) + pick.cell;
                commands[out].cmd_srca = (uint16_t)(pool.slots[slot].addr >> 3);
                commands[out].cmd_colr = (uint16_t)((clut_vram + (uint32_t)cell->clut * 32u) >> 3);
                drawn_slot = slot; drawn_cell = pick.cell;
                /* The drawn position: the command's corner A (rotation-sprites-2 rounds it once). */
                fzx_e1_sprite_pos[0] = (uint32_t)(int32_t)commands[out].cmd_xa; fzx_e1_sprite_pos[1] = (uint32_t)(int32_t)commands[out].cmd_ya;
                fzx_e1_sprite_pos[2] = (uint32_t)(int32_t)place.x; fzx_e1_sprite_pos[3] = place.frac[0];
                ++out;
            } else ++fzx_e1_report[11];
            /* Prefetch: the cell for one frame (two ticks) of velocity ahead. */
            int32_t ahead[3], d2; sh2z_rspr_pick_t p2; sh2z_rspr_place_t q2;
            for (unsigned k = 0; k < 3u; ++k) ahead[k] = pos[k] + 2 * body.vel[k];
            if (sh2z_rspr_pose(&p2, &q2, &d2, &racer_camera, racer_set, ahead, basis) && p2.cell != pick.cell) {
                uint32_t s2 = 0;
                if (sh2z_scache_get(&pool, p2.cell, cell_bytes(p2.cell), true, &s2) == SH2Z_SCACHE_MISS) fill_push(s2, p2.cell);
            }
        } else ++fzx_e1_report[11];
    }
    fzx_e1_cache[0] = pool.hits; fzx_e1_cache[1] = pool.misses; fzx_e1_cache[2] = pool.evictions; fzx_e1_cache[3] = pool.refused;
    fzx_e1_report[10] = out - 1u;
    return out > 1u ? out : 0u;
}

#if E1_FIELD
/* ---- close-up 3D-N: every car of the field, far to near ---------------------------
 * Sprite cars draw through the slot cache as the player sprite does (one cell each,
 * the car's previous cell while a fill is pending); 3D cars copy the commands built
 * for them earlier (engine_main.c stages them while the slave builds the track).
 * Shadows: the player and the nearest FIELD_SHADOWS other cars. */
enum { FIELD_SHADOWS = 8u };
static uint16_t car_cell[FZX_FIELD_CARS];
#if E2_ROSTER
static uint8_t car_prof[FZX_FIELD_CARS];
#endif
static uint8_t car_has_cell[FZX_FIELD_CARS];
volatile uint32_t fzx_field_draw[8]; /* sprites drawn, 3D cars drawn, shadows, sprite skips, cells pending, commands, -, - */
#if ENG_SCENES
static void field_state_reset(void) {
    memset(car_cell, 0, sizeof(car_cell));
    memset(car_has_cell, 0, sizeof(car_has_cell));
#if E2_ROSTER
    memset(car_prof, 0, sizeof(car_prof));
#endif
    memset((void *)fzx_field_draw, 0, sizeof(fzx_field_draw));
}
#endif
#if defined(E2_PROF) && E2_PROF
/* Diagnostic: FRT totals over the run: [0] cull + pose + sort, [1] shadows, [2] sprite cache + emit, [3] frames. */
volatile uint32_t fzx_e2_emit_split[4];
#define ESPLIT(k, v) do { const uint16_t now_ = cpu_frt_count_get(); fzx_e2_emit_split[k] += (uint16_t)(now_ - (v)); (v) = now_; } while (0)
#else
#define ESPLIT(k, v) do { } while (0)
#endif
#ifndef E2_EMIT_PROF
#define E2_EMIT_PROF 0
#endif
#if E2_EMIT_PROF
/* Diagnostic (E2_EMIT_PROF=1; changes timing slightly): one row per fzx_drive_cars call, FRT units:
 * [0] physics tick (low 16 bits), [1] setup (fill queue, cache frame, camera), [2] cull, [3] pose and
 * sort, [4] shadow pose, [5] shadow emit, [6] cache lookup and fill queue, [7] sprite emit, [8] 3D
 * copies, [9] the whole call; counts: [10] sprites posed, [11] shadows drawn. */
enum { EMIT_PROF_ROWS = 4800u };
uint16_t fzx_emit_prof[EMIT_PROF_ROWS][12];
volatile uint32_t fzx_emit_prof_n;
#define EP(k) do { const uint16_t now_ = cpu_frt_count_get(); ep_acc[k] += (uint16_t)(now_ - ep_t); ep_t = now_; } while (0)
#else
#define EP(k) do { } while (0)
#endif
#if E2_CULL
/* Run-time words: the smallest car drawn (pixels of the set's largest cell extent) and the
 * smallest sprite scale (Q16) that still gets a shadow. */
volatile uint32_t fzx_e2_min_px = 4, fzx_e2_shadow_scale = 16384;
static uint32_t cull_extent;
static int32_t cull_dot(const int16_t row[3], const int32_t d[3]) {
    const int64_t sum = (int64_t)row[0] * d[0] + (int64_t)row[1] * d[1] + (int64_t)row[2] * d[2];
    return (int32_t)(sum >> 16);
}
#ifndef E2_FASTSPRITE
#define E2_FASTSPRITE 0
#endif
#ifndef E2_EMIT_POSE
#define E2_EMIT_POSE 0
#endif
#ifndef E2_EMIT_YAW
#define E2_EMIT_YAW 0
#endif
#if E2_EMIT_POSE || E2_EMIT_YAW
#include "sh2zam/rotation_sprite_experimental.h"
#endif
#if E2_EMIT_YAW
/* E2_EMIT_YAW (exact; needs E2_EMIT_POSE): the yaw bin climbed from each car's previous bin instead of
 * scanning every bin (SH2ZAM rotation-sprites-3 sh2z_rspr_yaw_climb), for sets whose bins form a strictly
 * convex polygon around the origin (sh2z_rspr_yaw_convex, checked once per set; otherwise the scan). */
volatile int32_t fzx_emit_yaw_convex = -1; /* the racer set: -1 not checked, 0 scan, 1 climb */
static uint8_t yaw_prev[FZX_FIELD_CARS];
#endif
#if E2_FASTSPRITE
/* E2_FASTSPRITE (render only, rivals): sh2z_rspr_pose with the cull's camera-space terms reused
 * (no second transform), the cell chosen by the same yaw/pitch rule, and the screen-up vector
 * normalised with one division and an octagonal length (within 4%) instead of a square root and
 * two divisions. With E2_EMIT_POSE (exact against this form) each division is started before
 * independent work and collected after it; with E2_EMIT_YAW and a convex set the yaw bin is climbed. */
static uint32_t fast_abs(int32_t v) { return v < 0 ? 0u - (uint32_t)v : (uint32_t)v; }
#if E2_EMIT_POSE
static inline void fast_div_start(sh2z_divu_t *t, int64_t n, int32_t d) {
    const uint64_t raw = (uint64_t)n;
    sh2z_divu_start64(t, (int32_t)(uint32_t)(raw >> 32), (uint32_t)raw, d);
}
#endif
static bool fast_pose(sh2z_rspr_pick_t *pick, sh2z_rspr_place_t *place, const int32_t d[3], int32_t tz,
                      int64_t fqx, int64_t fqy, const int16_t b[3][3], const sh2z_rspr_set_t *set, int64_t scale_num
#if E2_EMIT_YAW
                      , uint8_t *yaw_hint
#endif
                      ) {
#if E2_EMIT_POSE
    sh2z_divu_t dv;
    fast_div_start(&dv, scale_num, tz);
#endif
    int32_t x = -cull_dot(b[2], d), y = -cull_dot(b[1], d), z = -cull_dot(b[0], d);
#if E2_EMIT_POSE
    if (!x && !y && !z) { (void)sh2z_divu_finish(&dv, NULL); return false; }
#else
    if (!x && !y && !z) return false;
#endif
    uint32_t largest = fast_abs(x);
    if (fast_abs(y) > largest) largest = fast_abs(y);
    if (fast_abs(z) > largest) largest = fast_abs(z);
    while (largest >= 32768u) { x >>= 1; y >>= 1; z >>= 1; largest >>= 1; }
    uint8_t mirror = 0;
    if ((set->flags & SH2Z_RSPR_MIRROR) && x < 0) { x = -x; mirror = 1; }
    unsigned yaw = 0, pitch = 0; int32_t best = 0, top = 0;
#if E2_EMIT_YAW
    if (yaw_hint) { yaw = sh2z_rspr_yaw_climb(set, *yaw_hint, x, z, &best); *yaw_hint = (uint8_t)yaw; }
    else
#endif
    for (unsigned k = 0; k < set->yaw_count; ++k) {
        const int32_t v = set->yaw[k][0] * x + set->yaw[k][1] * z;
        if (k == 0 || v > best) { best = v; yaw = k; }
    }
    const int32_t h = best >> 14;
    for (unsigned k = 0; k < set->pitch_count; ++k) {
        const int32_t v = set->pitch[k][0] * y + set->pitch[k][1] * h;
        if (k == 0 || v > top) { top = v; pitch = k; }
    }
    const int16_t *r0 = racer_camera.r[0], *r1 = racer_camera.r[1];
#if E2_EMIT_POSE
    const int32_t scale = sh2z_divu_finish(&dv, NULL);
    fast_div_start(&dv, fqx, tz);
    const int32_t ux = (int32_t)(((int64_t)r0[0] * b[1][0] + (int64_t)r0[1] * b[1][1] + (int64_t)r0[2] * b[1][2]) >> 14);
    const int32_t uy = (int32_t)(((int64_t)r1[0] * b[1][0] + (int64_t)r1[1] * b[1][1] + (int64_t)r1[2] * b[1][2]) >> 14);
    const uint32_t a = fast_abs(ux), c = fast_abs(uy), hi = a > c ? a : c, lo = a > c ? c : a;
    const uint32_t len = hi + ((lo * 3u) >> 3) - (hi >> 5); /* octagonal |(ux, uy)| */
    const int32_t qx = sh2z_divu_finish(&dv, NULL);
    fast_div_start(&dv, fqy, tz);
    const int32_t qy = sh2z_divu_finish(&dv, NULL);
    const int64_t ax = (int64_t)racer_camera.center_x * 65536 + qx, ay = (int64_t)racer_camera.center_y * 65536 - qy;
    const int32_t sx = (int32_t)(ax >> 16), sy = (int32_t)(ay >> 16);
    if (sx < -1023 || sx > 1023 || sy < -1023 || sy > 1023) return false;
    if (scale < 1 || scale > (1 << 24)) return false;
    if (!len) return false;
    fast_div_start(&dv, (int64_t)1 << 30, (int32_t)len);
    const int32_t inv = sh2z_divu_finish(&dv, NULL); /* Q16 of 16384 / len */
#else
    const int32_t qx = sh2z_divu_div64(fqx, tz, NULL), qy = sh2z_divu_div64(fqy, tz, NULL);
    const int64_t ax = (int64_t)racer_camera.center_x * 65536 + qx, ay = (int64_t)racer_camera.center_y * 65536 - qy;
    const int32_t sx = (int32_t)(ax >> 16), sy = (int32_t)(ay >> 16);
    if (sx < -1023 || sx > 1023 || sy < -1023 || sy > 1023) return false;
    const int32_t scale = sh2z_divu_div64(scale_num, tz, NULL);
    if (scale < 1 || scale > (1 << 24)) return false;
    const int32_t ux = (int32_t)(((int64_t)r0[0] * b[1][0] + (int64_t)r0[1] * b[1][1] + (int64_t)r0[2] * b[1][2]) >> 14);
    const int32_t uy = (int32_t)(((int64_t)r1[0] * b[1][0] + (int64_t)r1[1] * b[1][1] + (int64_t)r1[2] * b[1][2]) >> 14);
    const uint32_t a = fast_abs(ux), c = fast_abs(uy), hi = a > c ? a : c, lo = a > c ? c : a;
    const uint32_t len = hi + ((lo * 3u) >> 3) - (hi >> 5); /* octagonal |(ux, uy)| */
    if (!len) return false;
    const int32_t inv = sh2z_divu_div64((int64_t)1 << 30, (int32_t)len, NULL); /* Q16 of 16384 / len */
#endif
    pick->cell = (uint16_t)(pitch * set->yaw_count + yaw); pick->mirror = mirror; pick->reserved = 0;
    place->x = (int16_t)sx; place->y = (int16_t)sy; place->scale = scale;
    place->frac[0] = (uint16_t)(ax & 0xffff); place->frac[1] = (uint16_t)(ay & 0xffff);
    place->up[0] = (int16_t)(((int64_t)ux * inv) >> 16);
    place->up[1] = (int16_t)((-(int64_t)uy * inv) >> 16);
    return true;
}
#endif
#endif

#ifndef E2_EMIT_SETUP
#define E2_EMIT_SETUP 0
#endif
#ifndef E2_EMIT_LINK
#define E2_EMIT_LINK 0
#endif
#ifndef E2_EMIT_CULL
#define E2_EMIT_CULL 0
#endif
#if E2_EMIT_SETUP
/* E2_EMIT_SETUP (exact): sprite_camera with the two scale numerators kept while the focal length is
 * unchanged. sh2z_rspr_camera_scale validates its set and divides in 64 bits (software) on every call;
 * its result depends only on the focal length and the two sets, which never change after the load. */
static bool sprite_camera_memo(const fzx_view_t *view, const fzx_engine_cam_t *cam) {
    static int32_t memo_focal;
    static bool memo_set, memo_ok;
    static int64_t memo_racer, memo_shadow;
    sh2z_rspr_camera_t c; memset(&c, 0, sizeof(c));
    for (unsigned i = 0; i < 3u; ++i) for (unsigned j = 0; j < 3u; ++j) {
        const int32_t n = view->rotation[i][j]; c.r[i][j] = (int16_t)((n >= 0 ? n + 32768 : -(-n + 32768)) / 65536);
    }
    for (unsigned i = 0; i < 3u; ++i) c.eye[i] = cam->eye[i];
    c.focal_x = view->focal[0]; c.focal_y = view->focal[1];
    c.center_x = (int16_t)((view->center[0] + 32768) >> 16); c.center_y = (int16_t)(((view->center[1] + 32768) >> 16) - 1);
    c.near_z = view->near_plane * 4;
    racer_camera = c; shadow_camera = c;
    if (!memo_set || memo_focal != c.focal_y) {
        const bool ok = sh2z_rspr_camera_scale(&racer_camera, racer_set, MODEL_SCALE_Q14) &&
                        sh2z_rspr_camera_scale(&shadow_camera, shadow_set, MODEL_SCALE_Q14);
        memo_set = true; memo_focal = c.focal_y; memo_ok = ok;
        memo_racer = racer_camera.scale_num; memo_shadow = shadow_camera.scale_num;
        return ok;
    }
    racer_camera.scale_num = memo_racer; shadow_camera.scale_num = memo_shadow;
    return memo_ok;
}
#define FIELD_CAMERA(view, cam) sprite_camera_memo((view), (cam))
#else
#define FIELD_CAMERA(view, cam) sprite_camera((view), (cam))
#endif
#if E2_EMIT_LINK
fzx_emit_seg_t fzx_emit_segs[2u * FZX_FIELD_K_MAX + 2u];
unsigned fzx_emit_nsegs;
#endif
#if E2_ROSTER
/* ---- E2_ROSTER: each rival from its own machine's sets ------------------------------
 * The private roster pack (directory of 32-byte entries, sets 2,048-aligned) goes whole into
 * the cartridge after the player's set; its FNV-1a over the file is checked at load (the
 * build refused it unless its SHA-256 matched). Each rival's character (the reference GP's
 * stock family 0..29) picks its near (20 yaw x 3 pitch) and far (8 yaw) sets; guest and Super
 * families are never bound. Each set's CLUT goes to the CLUT slots after the player's. */
#include "roster_identity.h"
#include "rivals_field.h"
typedef struct { const sh2z_rspr_set_t *set; int64_t scale_num; uint16_t clut; uint8_t convex; } roster_set_t;
static roster_set_t roster[FZX_FIELD_CARS][2]; /* [id][0 near, 1 far] */
static int32_t roster_focal;
/* [0] status (1 ok, 2 load refused or no room, 3 family out of range, 4 set missing or invalid,
 * 5 CLUT room), [1] sets bound, [2] near draws, [3] far draws, [4] cartridge base, [5] bytes,
 * [6] sets whose yaw bins climb (E2_EMIT_YAW). */
volatile uint32_t fzx_e2_roster[8];
volatile uint32_t fzx_e2_far_scale = 0x5000; /* below this near-set scale (Q16) a rival draws its far set */
static uint32_t be32(const uint8_t *e) { return ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3]; }
static const sh2z_rspr_set_t *roster_find(const uint8_t *pak, const char key[8], const char prof[4]) {
    for (uint32_t i = 0; i < FZX_ROSTER_SETS; ++i) {
        const uint8_t *e = pak + 32u * i;
        if (memcmp(e, key, 8) || memcmp(e + 8, prof, 4)) continue;
        const uint32_t off = be32(e + 12), len = be32(e + 16);
        if ((off & 2047u) || off + len > FZX_ROSTER_BYTES) return 0;
        const sh2z_rspr_set_t *set = (const sh2z_rspr_set_t *)(const void *)(pak + off);
        return sh2z_rspr_valid(set) && set->bytes == len ? set : 0;
    }
    return 0;
}
bool fzx_drive_roster_load(void *scratch, uint32_t scratch_bytes, uintptr_t cart_limit) {
    const uintptr_t base = (set_cart + FZX_RACER_SPRITE_BYTES + 2047u) & ~(uintptr_t)2047u;
#if ENG_ROSTER
    /* Scenes image: only the field's families, compressed (engine_roster.c, ROSTERZ.PAK). */
    {
        uint8_t fams[FZX_FIELD_CARS]; unsigned nf = 0;
        for (uint32_t id = 1; id < FZX_FIELD_CARS; ++id) {
            const int32_t fam = CSEL(field,fzx_rivals_field)[id][5];
            if (fam < 0 || fam > 29) { fzx_e2_roster[0] = 3; return false; }
            bool seen = false;
            for (unsigned j = 0; j < nf; ++j) if (fams[j] == (uint8_t)fam) seen = true;
            if (!seen) fams[nf++] = (uint8_t)fam;
        }
        extern volatile uint32_t fzx_scn_roster[8];
        extern uint32_t video_fields_get(void);
        const uint32_t r0 = video_fields_get();
        const bool ok = fzx_roster_select_load(fams, nf, base, cart_limit, scratch, scratch_bytes);
        fzx_scn_roster[0] = video_fields_get() - r0; /* the roster's share of LOAD, in fields */
        if (!ok) { fzx_e2_roster[0] = 2; return false; }
        fzx_e2_roster[4] = (uint32_t)base; fzx_e2_roster[5] = fzx_roster_report[2];
    }
#else
    fzx_e2_roster[4] = (uint32_t)base; fzx_e2_roster[5] = FZX_ROSTER_BYTES;
    if (base + FZX_ROSTER_BYTES > cart_limit ||
        !fzx_texture_cd_load("ROSTER.PAK", FZX_ROSTER_BYTES, FZX_ROSTER_HASH, (void *)base, scratch, scratch_bytes)) { fzx_e2_roster[0] = 2; return false; }
    const uint8_t *pak = (const uint8_t *)base;
#endif
    uint32_t next = racer_set->clut_count, bound = 0;
    volatile uint8_t *const clut_cpu = (volatile uint8_t *)(VDP1_VRAM_BASE + clut_vram);
    static const char prof[2][4] = { { 'n', 'e', 'a', 'r' }, { 'f', 'a', 'r', 0 } };
    for (uint32_t id = 1; id < FZX_FIELD_CARS; ++id) {
        const int32_t fam = CSEL(field,fzx_rivals_field)[id][5];
        if (fam < 0 || fam > 29) { fzx_e2_roster[0] = 3; return false; }
        const char key[8] = { 'c', (char)('0' + fam / 10), (char)('0' + fam % 10), 's', '1', 0, 0, 0 };
        for (unsigned k = 0; k < 2u; ++k) {
#if ENG_ROSTER
            (void)key; (void)prof;
            const sh2z_rspr_set_t *set = fzx_roster_set((uint32_t)fam, k);
#else
            const sh2z_rspr_set_t *set = roster_find(pak, key, prof[k]);
#endif
            if (!set) { fzx_e2_roster[0] = 4; return false; }
            if (next + set->clut_count > CLUT_SETS) { fzx_e2_roster[0] = 5; return false; }
            const uint8_t *src = (const uint8_t *)set + set->clut_offset;
            for (uint32_t b = 0; b < (uint32_t)set->clut_count * 32u; b += 2u)
                *(volatile uint16_t *)(clut_cpu + next * 32u + b) = (uint16_t)((src[b] << 8) | src[b + 1]);
            roster[id][k].set = set; roster[id][k].clut = (uint16_t)next; roster[id][k].scale_num = 0;
#if E2_EMIT_YAW
            roster[id][k].convex = sh2z_rspr_yaw_convex(set) ? 1u : 0u;
            fzx_e2_roster[6] += roster[id][k].convex; /* sets that climb */
#endif
            next += set->clut_count; ++bound;
            const sh2z_rspr_cell_t *cells = sh2z_rspr_cells(set);
            for (uint32_t c = 0; c < (uint32_t)set->yaw_count * set->pitch_count; ++c) {
                if (k == 0 && cells[c].width > cull_extent) cull_extent = cells[c].width;
                if (k == 0 && cells[c].height > cull_extent) cull_extent = cells[c].height;
                if ((uint32_t)cells[c].width * cells[c].height / 2u > pool_sizes[3]) { fzx_e2_roster[0] = 4; return false; }
            }
        }
    }
    roster_focal = 0; fzx_e2_roster[1] = bound; fzx_e2_roster[0] = 1;
    return true;
}
static uint32_t set_cell_bytes(const sh2z_rspr_set_t *set, uint32_t cell) {
    const sh2z_rspr_cell_t *c = sh2z_rspr_cells(set) + cell;
    return (uint32_t)c->width * c->height / 2u;
}
#endif

#ifndef E2_EMIT_HINT
#define E2_EMIT_HINT 0
#endif
#ifndef E2_EMIT_CMD
#define E2_EMIT_CMD 0
#endif
#ifndef E2_EMIT_SHADOW
#define E2_EMIT_SHADOW 0
#endif
#if E2_EMIT_HINT
/* E2_EMIT_HINT (exact): each car remembers the slot of its last lookup and of its last drawn cell, and a
 * lookup probes that slot before sh2z_scache_get scans the pool (every read cache-through). A key is held
 * by at most one slot that is not free (a slot is claimed only after the scan found no holder, and a
 * key's size never changes), so a probe that finds the key gives the scan's answer; it stamps and counts
 * as the scan does. */
static uint8_t hint_cur[FZX_FIELD_CARS], hint_drawn[FZX_FIELD_CARS];
static int hint_get(uint32_t key, uint32_t bytes, uint32_t *slot, uint8_t *hint) {
    const uint32_t h = *hint;
    if (h < pool.count) {
        sh2z_scache_slot_t *s = &pool.slots[h];
        const uint32_t state = s->state;
        if (state != SH2Z_SCACHE_FREE && s->key == key && s->bytes >= bytes) {
            if (s->last_use < pool.frame) s->last_use = pool.frame;
            *slot = h;
            if (state == SH2Z_SCACHE_READY) { ++pool.hits; return SH2Z_SCACHE_HIT; }
            return SH2Z_SCACHE_PENDING;
        }
    }
    const int r = sh2z_scache_get(&pool, key, bytes, false, slot);
    if (r != SH2Z_SCACHE_REFUSED) *hint = (uint8_t)*slot;
    return r;
}
#define CACHE_GET(key, bytes, slot, hint) hint_get((key), (bytes), (slot), (hint))
#else
#define CACHE_GET(key, bytes, slot, hint) sh2z_scache_get(&pool, (key), (bytes), false, (slot))
#endif
#if E2_EMIT_CMD
/* E2_EMIT_CMD (exact): each command by SH2ZAM rotation-sprites-3 sh2z_rspr_emit_cell, the library's
 * sh2z_rspr_emit_valid with the slot's texture word and the CLUT word written in the same pass and the
 * corners in 32 x 32 -> 64 products. The set-level checks stay here (EMIT_SET_OK). */
#include "sh2zam/rotation_sprite_experimental.h"
#define EMIT_SET_OK(set, cell) ((set)->bytes <= 0x80000u && (cell) < (uint32_t)(set)->yaw_count * (set)->pitch_count)
#endif
#if E2_EMIT_SHADOW
/* E2_EMIT_SHADOW (exact): sh2z_rspr_pose for the one-cell shadow set. Its only selection refusal (all
 * three model-space terms 0) is kept; the cell is always 0, never mirrored. The screen-up vector depends
 * only on the camera rows and the car's up row, so with an exact car pose (up given) it is the car's own:
 * no square root and two divisions fewer. Under E2_FASTSPRITE a rival's up is the octagonal estimate, so
 * up is NULL there and the library's exact form is computed here. */
static int32_t shadow_dot(const int16_t row[3], const int32_t d[3]) {
    const int64_t sum = (int64_t)row[0] * d[0] + (int64_t)row[1] * d[1] + (int64_t)row[2] * d[2];
    return (int32_t)(sum >> 16);
}
static bool shadow_place(sh2z_rspr_place_t *place, const int32_t ground[3], const int16_t *basis, const int16_t up[2]) {
    const sh2z_rspr_camera_t *c = &shadow_camera;
    const int32_t d[3] = { (int32_t)((uint32_t)ground[0] - (uint32_t)c->eye[0]), (int32_t)((uint32_t)ground[1] - (uint32_t)c->eye[1]),
                           (int32_t)((uint32_t)ground[2] - (uint32_t)c->eye[2]) };
    const int32_t tz = shadow_dot(c->r[2], d);
    if (tz <= c->near_z || tz <= 0) return false;
    if (!shadow_dot(basis + 6, d) && !shadow_dot(basis + 3, d) && !shadow_dot(basis, d)) return false;
    const int32_t qx = sh2z_divu_div64((int64_t)c->focal_x * shadow_dot(c->r[0], d), tz, NULL);
    const int32_t qy = sh2z_divu_div64((int64_t)c->focal_y * shadow_dot(c->r[1], d), tz, NULL);
    const int64_t ax = (int64_t)c->center_x * 65536 + qx, ay = (int64_t)c->center_y * 65536 - qy;
    const int32_t x = (int32_t)(ax >> 16), y = (int32_t)(ay >> 16);
    if (x < -1023 || x > 1023 || y < -1023 || y > 1023) return false;
    const int32_t scale = sh2z_divu_div64(c->scale_num, tz, NULL);
    if (scale < 1 || scale > (1 << 24)) return false;
    int16_t u[2];
    if (up) { u[0] = up[0]; u[1] = up[1]; }
    else {
        const int16_t *r0 = c->r[0], *r1 = c->r[1];
        const int32_t ux = (int32_t)(((int64_t)r0[0] * basis[3] + (int64_t)r0[1] * basis[4] + (int64_t)r0[2] * basis[5]) >> 14);
        const int32_t uy = (int32_t)(((int64_t)r1[0] * basis[3] + (int64_t)r1[1] * basis[4] + (int64_t)r1[2] * basis[5]) >> 14);
        uint32_t v = (uint32_t)(ux * ux) + (uint32_t)(uy * uy), root = 0, bit = UINT32_C(1) << 30;
        while (bit > v) bit >>= 2;
        while (bit) { if (v >= root + bit) { v -= root + bit; root = (root >> 1) + bit; } else root >>= 1; bit >>= 2; }
        if (!root) return false;
        u[0] = (int16_t)sh2z_divu_div64((int64_t)16384 * ux, (int32_t)root, NULL);
        u[1] = (int16_t)sh2z_divu_div64(-(int64_t)16384 * uy, (int32_t)root, NULL);
    }
    place->x = (int16_t)x; place->y = (int16_t)y; place->scale = scale;
    place->frac[0] = (uint16_t)(ax & 0xffff); place->frac[1] = (uint16_t)(ay & 0xffff);
    place->up[0] = u[0]; place->up[1] = u[1];
    return true;
}
#endif

unsigned fzx_drive_cars(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view, const fzx_engine_cam_t *cam,
                        const fzx_field_car_t *cars, unsigned n, const vdp1_cmdt_t *const *staged, const uint16_t *staged_n) {
    if (fzx_e1_status != 1 || capacity < 2u) { ++fzx_e1_report[11]; return 0; }
#if E2_EMIT_PROF
    const uint16_t ep_start = cpu_frt_count_get();
    uint16_t ep_t = ep_start;
    uint32_t ep_acc[10] = {0}, ep_posed = 0;
#endif
    fzx_e1_sprite_pos[0] = fzx_e1_sprite_pos[1] = (uint32_t)-32768;
    {
        const uint32_t done = *CT(&fill_done), count = fill_count;
        uint32_t k = 0;
        for (uint32_t i = done; i < count; ++i) fill_queue[k++] = fill_queue[i];
        fill_count = k; fill_done = 0;
    }
#if E2_EMIT_LINK
    fzx_emit_nsegs = 0;
#endif
    sh2z_scache_frame(&pool);
    if (!FIELD_CAMERA(view, cam)) { ++fzx_e1_report[11]; return 0; }
#if E2_EMIT_CMD
    /* The library's per-call checks that never change for the shadow set, and its two words. */
    static uint16_t shadow_colr, shadow_srca;
    static const sh2z_rspr_cell_t *shadow_cell;
    static bool shadow_fits, shadow_ready;
    if (!shadow_ready) {
        static sh2z_rspr_cell_t sc;
        sc = *sh2z_rspr_cells(shadow_set); shadow_cell = &sc;
        shadow_colr = (uint16_t)((shadow_vram + shadow_set->clut_offset + (uint32_t)sc.clut * 32u) >> 3);
        shadow_srca = (uint16_t)((shadow_vram + sc.pixel_offset) >> 3);
        shadow_fits = !(shadow_vram & 7u) && shadow_vram <= 0x7ffffu && shadow_set->bytes <= 0x80000u - shadow_vram;
        shadow_ready = true;
    }
#endif
    EP(1);
#if defined(E2_PROF) && E2_PROF
    uint16_t es = cpu_frt_count_get(); uint32_t sh_ticks = 0;
    ++fzx_e2_emit_split[3];
#endif
#if E2_ROSTER
    if (racer_camera.focal_y != roster_focal) {
        roster_focal = racer_camera.focal_y;
        for (uint32_t id = 1; id < FZX_FIELD_CARS; ++id) for (unsigned k = 0; k < 2u; ++k)
            roster[id][k].scale_num = (int64_t)racer_camera.focal_y * MODEL_SCALE_Q14 * 65536 / roster[id][k].set->pixels_per_unit;
    }
    typedef struct { sh2z_rspr_pick_t pick; sh2z_rspr_place_t place; int32_t depth; uint8_t ok, prof; } item_t;
#else
    typedef struct { sh2z_rspr_pick_t pick; sh2z_rspr_place_t place; int32_t depth; uint8_t ok; } item_t;
#endif
    static item_t items[FZX_FIELD_CARS];
    uint8_t order_[FZX_FIELD_CARS];
#if E2_CULL
    /* Cheap reject before any sprite work (render only): behind the near plane, past the
     * size where the car would be under fzx_e2_min_px pixels, or off the view rectangle by
     * more than the set's largest cell extent at that depth. The player and 3D cars always
     * pass. Survivors are posed and sorted; the rest never reach the command list. */
    if (!cull_extent) {
        const sh2z_rspr_cell_t *cells = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1);
        const uint32_t count = (uint32_t)racer_set->yaw_count * racer_set->pitch_count;
        for (uint32_t c = 0; c < count; ++c) {
            if (cells[c].width > cull_extent) cull_extent = cells[c].width;
            if (cells[c].height > cull_extent) cull_extent = cells[c].height;
        }
    }
    const int64_t ext = racer_camera.scale_num * (int64_t)cull_extent;
#if E2_EMIT_CULL
    /* E2_EMIT_CULL (exact): the far depth (a 64-bit software division) kept while its inputs are
     * unchanged; each edge test as one 32 x 32 -> 64 product scaled by 2^16 instead of a 64 x 32
     * product; the vertical tests only after the horizontal ones pass. */
    static int64_t memo_ext = -1, memo_far;
    static uint32_t memo_px;
    const uint32_t min_px = fzx_e2_min_px ? fzx_e2_min_px : 1u;
    if (ext != memo_ext || min_px != memo_px) { memo_ext = ext; memo_px = min_px; memo_far = ext / ((int64_t)min_px << 16); }
    const int64_t far_tz = memo_far;
    const int32_t cx = racer_camera.center_x, cy = racer_camera.center_y;
    const int32_t lx32 = cx - view->clip[0], rx32 = view->clip[2] - cx, ty32 = cy - view->clip[1], by32 = view->clip[3] - cy;
#else
    const int64_t far_tz = ext / ((int64_t)(fzx_e2_min_px ? fzx_e2_min_px : 1u) << 16);
    const int32_t cx = racer_camera.center_x, cy = racer_camera.center_y;
    const int64_t lx = (int64_t)(cx - view->clip[0]) << 16, rx = (int64_t)(view->clip[2] - cx) << 16;
    const int64_t ty_ = (int64_t)(cy - view->clip[1]) << 16, by = (int64_t)(view->clip[3] - cy) << 16;
#endif
    uint32_t c_behind = 0, c_far = 0, c_off = 0;
#endif
    for (unsigned i = 0; i < n; ++i) {
        item_t *it = &items[i];
#if E2_CULL
        if (cars[i].id != 0 && !cars[i].is3d) {
            const int32_t d[3] = { (int32_t)((uint32_t)cars[i].pos[0] - (uint32_t)racer_camera.eye[0]),
                                   (int32_t)((uint32_t)cars[i].pos[1] - (uint32_t)racer_camera.eye[1]),
                                   (int32_t)((uint32_t)cars[i].pos[2] - (uint32_t)racer_camera.eye[2]) };
            const int32_t tz = cull_dot(racer_camera.r[2], d);
            uint8_t reason = 0;
            int64_t qx = 0, qy = 0;
            if (tz <= racer_camera.near_z || tz <= 0) reason = 1;
            else if (tz > far_tz) reason = 2;
            else {
#if E2_EMIT_CULL
                qx = (int64_t)racer_camera.focal_x * cull_dot(racer_camera.r[0], d);
                if (qx - ext > (int64_t)rx32 * tz * 65536 || -qx - ext > (int64_t)lx32 * tz * 65536) reason = 3;
                else {
                    qy = (int64_t)racer_camera.focal_y * cull_dot(racer_camera.r[1], d);
                    if (qy - ext > (int64_t)ty32 * tz * 65536 || -qy - ext > (int64_t)by32 * tz * 65536) reason = 3;
                }
#else
                qx = (int64_t)racer_camera.focal_x * cull_dot(racer_camera.r[0], d);
                qy = (int64_t)racer_camera.focal_y * cull_dot(racer_camera.r[1], d);
                if (qx - ext > rx * tz || -qx - ext > lx * tz || qy - ext > ty_ * tz || -qy - ext > by * tz) reason = 3;
#endif
            }
            if (reason) {
                it->ok = 0; it->depth = INT32_MAX;
                if (reason == 1) ++c_behind; else if (reason == 2) ++c_far; else ++c_off;
                unsigned j = i; /* culled: far end of the order, never drawn */
                while (j > 0 && items[order_[j - 1]].depth < INT32_MAX) { order_[j] = order_[j - 1]; --j; }
                order_[j] = (uint8_t)i;
                EP(2);
                continue;
            }
            EP(2);
#if E2_EMIT_POSE && !E2_FASTSPRITE
            {
                /* SH2ZAM rotation-sprites-3: sh2z_rspr_pose from the cull's terms (identical outputs). */
                sh2z_rspr_terms_t terms;
                terms.d[0] = d[0]; terms.d[1] = d[1]; terms.d[2] = d[2]; terms.tz = tz; terms.nx = qx; terms.ny = qy;
#if E2_EMIT_YAW
                if (fzx_emit_yaw_convex < 0) fzx_emit_yaw_convex = sh2z_rspr_yaw_convex(racer_set) ? 1 : 0;
                uint8_t *const hint = fzx_emit_yaw_convex > 0 ? &yaw_prev[cars[i].id] : NULL;
#else
                uint8_t *const hint = NULL;
#endif
                int32_t depth = 0;
                it->ok = sh2z_rspr_pose_terms(&it->pick, &it->place, &depth, &racer_camera, racer_set, &terms,
                                              (const int16_t *)(const void *)cars[i].basis, hint) ? 1u : 0u;
            }
            it->depth = it->ok ? tz : INT32_MAX;
            {
                const int32_t key = it->depth;
                unsigned j = i;
                while (j > 0 && items[order_[j - 1]].depth < key) { order_[j] = order_[j - 1]; --j; }
                order_[j] = (uint8_t)i;
            }
#if E2_EMIT_PROF
            ep_posed += it->ok; EP(3);
#endif
            continue;
#endif
#if E2_FASTSPRITE
            /* The cull's camera-space terms reused: one pose without re-transforming the car. */
#if E2_EMIT_YAW
#define YAW_HINT(convex, id) , ((convex) ? &yaw_prev[(id)] : NULL)
#else
#define YAW_HINT(convex, id)
#endif
#if E2_ROSTER
            {
                const roster_set_t *rs = roster[cars[i].id];
                const int32_t near_scale = sh2z_divu_div64(rs[0].scale_num, tz, NULL);
                it->prof = near_scale < (int32_t)fzx_e2_far_scale ? 1u : 0u;
                it->ok = fast_pose(&it->pick, &it->place, d, tz, qx, qy, cars[i].basis, rs[it->prof].set, rs[it->prof].scale_num
                                   YAW_HINT(rs[it->prof].convex, cars[i].id)) ? 1u : 0u;
            }
#else
#if E2_EMIT_YAW
            if (fzx_emit_yaw_convex < 0) fzx_emit_yaw_convex = sh2z_rspr_yaw_convex(racer_set) ? 1 : 0;
#endif
            it->ok = fast_pose(&it->pick, &it->place, d, tz, qx, qy, cars[i].basis, racer_set, racer_camera.scale_num
                               YAW_HINT(fzx_emit_yaw_convex > 0, cars[i].id)) ? 1u : 0u;
#endif
            it->depth = it->ok ? tz : INT32_MAX;
            {
                const int32_t key = it->depth;
                unsigned j = i;
                while (j > 0 && items[order_[j - 1]].depth < key) { order_[j] = order_[j - 1]; --j; }
                order_[j] = (uint8_t)i;
            }
            continue;
#endif
        }
#endif
        int16_t basis[9];
        memcpy(basis, cars[i].basis, sizeof(basis));
        it->ok = sh2z_rspr_pose(&it->pick, &it->place, &it->depth, &racer_camera, racer_set, cars[i].pos, basis) ? 1u : 0u;
        if (!it->ok) it->depth = cars[i].is3d ? 0 : INT32_MAX; /* behind the camera: no sprite; 3D cars last */
        const int32_t key = it->depth;
        unsigned j = i;
        while (j > 0 && items[order_[j - 1]].depth < key) { order_[j] = order_[j - 1]; --j; }
        order_[j] = (uint8_t)i;
#if E2_EMIT_PROF
        ep_posed += it->ok; EP(3);
#endif
    }
    /* Shadows: the player and the nearest FIELD_SHADOWS others (nearest = end of the order). */
    uint8_t shadow[FZX_FIELD_CARS];
    {
        unsigned others = 0;
        for (unsigned k = n; k-- > 0;) {
            const unsigned i = order_[k];
#if E2_CULL
            shadow[i] = (uint8_t)(items[i].ok && (cars[i].id == 0 ||
                (others < FIELD_SHADOWS && items[i].place.scale >= (int32_t)fzx_e2_shadow_scale)));
#else
            shadow[i] = (uint8_t)(items[i].ok && (cars[i].id == 0 || others < FIELD_SHADOWS));
#endif
            if (cars[i].id != 0 && items[i].ok) ++others;
        }
    }
    ESPLIT(0, es);
    EP(3);
    unsigned out = 0, sprites = 0, solid = 0, shadows = 0;
    memset(&commands[out], 0, sizeof(commands[out])); vdp1_cmdt_user_clip_coord_set(&commands[out]);
    commands[out].cmd_xa = view->clip[0]; commands[out].cmd_ya = view->clip[1];
    commands[out].cmd_xc = view->clip[2] - 1; commands[out].cmd_yc = view->clip[3] - 1;
    ++out;
    const int32_t drop = (int32_t)(0.3f * FZX_RACER_SPRITE_RADIUS * 0.1f * 4.0f);
    const sh2z_rspr_pick_t zero = {0};
#if E2_EMIT_CMD
    (void)zero; /* the shadows' emit takes the cell directly */
#endif
#if E2_EMIT_LINK
    /* The commands written here are commands[0..out); link_extra counts the staged 3D commands listed
     * in place, so out + link_extra is the list length the copy-in path would have had. */
    unsigned link_extra = 0, seg_start = 0;
#define LIST_USED (out + link_extra)
#else
#define LIST_USED out
#endif
    for (unsigned k = 0; k < n; ++k) {
        const unsigned i = order_[k];
        const fzx_field_car_t *c = &cars[i];
#if defined(E2_PROF) && E2_PROF
        const uint16_t s0_ = cpu_frt_count_get();
#endif
        if (shadow[i] && LIST_USED < capacity) {
            int32_t ground[3], depth; sh2z_rspr_pick_t pick; sh2z_rspr_place_t place;
            int16_t basis[9]; memcpy(basis, c->basis, sizeof(basis));
            for (unsigned a = 0; a < 3u; ++a) ground[a] = c->pos[a] - basis[3 + a] * drop;
#if E2_EMIT_PROF || E2_EMIT_CMD || E2_EMIT_SHADOW
#if E2_EMIT_SHADOW
            (void)pick; (void)depth;
#if E2_FASTSPRITE
            const bool exact_up = c->id == 0 || c->is3d; /* only the rivals' sprite poses are estimates */
#else
            const bool exact_up = true;
#endif
            const bool posed = shadow_place(&place, ground, basis, exact_up ? items[i].place.up : NULL);
#else
            const bool posed = sh2z_rspr_pose(&pick, &place, &depth, &shadow_camera, shadow_set, ground, basis);
#endif
            EP(4);
#if E2_EMIT_CMD
            if (posed && shadow_fits && sh2z_rspr_emit_cell((uint16_t *)(void *)&commands[out], shadow_cell, 0u, &place, shadow_pmod,
                                                            shadow_colr, shadow_srca)) { ++out; ++shadows; }
#else
            if (posed && sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], shadow_set, &zero, &place, shadow_vram, shadow_pmod)) { ++out; ++shadows; }
#endif
            EP(5);
#else
            if (sh2z_rspr_pose(&pick, &place, &depth, &shadow_camera, shadow_set, ground, basis) &&
                sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], shadow_set, &zero, &place, shadow_vram, shadow_pmod)) { ++out; ++shadows; }
#endif
        }
#if defined(E2_PROF) && E2_PROF
        sh_ticks += (uint16_t)(cpu_frt_count_get() - s0_);
#endif
        if (c->is3d) {
            const unsigned m = staged_n[c->slot];
#if E2_EMIT_LINK
            if (m && LIST_USED + m <= capacity) {
                if (out > seg_start) { fzx_emit_segs[fzx_emit_nsegs].src = &commands[seg_start]; fzx_emit_segs[fzx_emit_nsegs++].n = out - seg_start; }
                fzx_emit_segs[fzx_emit_nsegs].src = staged[c->slot]; fzx_emit_segs[fzx_emit_nsegs++].n = m;
                seg_start = out; link_extra += m; ++solid;
            }
#else
            if (m && out + m <= capacity) { memcpy(&commands[out], staged[c->slot], m * sizeof(vdp1_cmdt_t)); out += m; ++solid; }
#endif
            EP(8);
            continue;
        }
        if (!items[i].ok || LIST_USED >= capacity) { EP(8); continue; }
#if E2_ROSTER
        if (c->id != 0) {
            /* Key: rival id x 1024 + profile x 256 + cell; while a fill is pending, the rival's
             * previous cell if its slot still holds it. */
            const roster_set_t *rs = &roster[c->id][items[i].prof];
            sh2z_rspr_pick_t pick = items[i].pick;
            const sh2z_rspr_set_t *set = rs->set;
            uint32_t slot = 0, clut = rs->clut;
            const uint32_t key = ((uint32_t)c->id << 10) | ((uint32_t)items[i].prof << 8) | pick.cell;
            const int r = CACHE_GET(key, set_cell_bytes(set, pick.cell), &slot, &hint_cur[c->id]);
            if (r == SH2Z_SCACHE_MISS) fill_push_set(slot, pick.cell, set);
            bool draw = r == SH2Z_SCACHE_HIT;
            if (!draw && r != SH2Z_SCACHE_REFUSED && car_has_cell[c->id]) {
                const roster_set_t *ps = &roster[c->id][car_prof[c->id]];
                const uint32_t pkey = ((uint32_t)c->id << 10) | ((uint32_t)car_prof[c->id] << 8) | car_cell[c->id];
                uint32_t again = 0;
                if (CACHE_GET(pkey, set_cell_bytes(ps->set, car_cell[c->id]), &again, &hint_drawn[c->id]) == SH2Z_SCACHE_HIT &&
                    ps->set == set) { pick.cell = car_cell[c->id]; slot = again; draw = true; clut = ps->clut; ++fzx_e1_cache[4]; }
            }
            if (!draw) { ++fzx_field_draw[4]; continue; }
#if E2_EMIT_CMD
            const sh2z_rspr_cell_t *cell = sh2z_rspr_cells(set) + pick.cell;
            if (EMIT_SET_OK(set, pick.cell) &&
                sh2z_rspr_emit_cell((uint16_t *)(void *)&commands[out], cell, pick.mirror, &items[i].place, racer_pmod,
                                    (uint16_t)((clut_vram + (clut + (uint32_t)cell->clut) * 32u) >> 3),
                                    (uint16_t)(pool.slots[slot].addr >> 3))) {
#else
            if (sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], set, &pick, &items[i].place, 0u, racer_pmod)) {
                const sh2z_rspr_cell_t *cell = sh2z_rspr_cells(set) + pick.cell;
                commands[out].cmd_srca = (uint16_t)(pool.slots[slot].addr >> 3);
                commands[out].cmd_colr = (uint16_t)((clut_vram + (clut + (uint32_t)cell->clut) * 32u) >> 3);
#endif
#if E2_EMIT_HINT
                hint_drawn[c->id] = (uint8_t)slot;
#endif
                car_cell[c->id] = pick.cell; car_prof[c->id] = items[i].prof; car_has_cell[c->id] = 1;
                ++fzx_e2_roster[2 + items[i].prof];
                ++out; ++sprites;
            } else ++fzx_field_draw[3];
            continue;
        }
#endif
        sh2z_rspr_pick_t pick = items[i].pick;
        uint32_t slot = 0;
        const int r = CACHE_GET(pick.cell, cell_bytes(pick.cell), &slot, &hint_cur[c->id]);
        if (r == SH2Z_SCACHE_MISS) fill_push(slot, pick.cell);
        bool draw = r == SH2Z_SCACHE_HIT;
        if (!draw && r != SH2Z_SCACHE_REFUSED && car_has_cell[c->id]) {
            uint32_t again = 0;
            if (CACHE_GET(car_cell[c->id], cell_bytes(car_cell[c->id]), &again, &hint_drawn[c->id]) == SH2Z_SCACHE_HIT) {
                pick.cell = car_cell[c->id]; slot = again; draw = true; ++fzx_e1_cache[4];
            }
        }
        EP(6);
        if (!draw) { ++fzx_field_draw[4]; continue; }
#if E2_EMIT_CMD
        const sh2z_rspr_cell_t *cell = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1) + pick.cell;
        if (EMIT_SET_OK(racer_set, pick.cell) &&
            sh2z_rspr_emit_cell((uint16_t *)(void *)&commands[out], cell, pick.mirror, &items[i].place, racer_pmod,
                                (uint16_t)((clut_vram + (uint32_t)cell->clut * 32u) >> 3), (uint16_t)(pool.slots[slot].addr >> 3))) {
#else
        if (sh2z_rspr_emit_valid((uint16_t *)(void *)&commands[out], racer_set, &pick, &items[i].place, 0u, racer_pmod)) {
            const sh2z_rspr_cell_t *cell = (const sh2z_rspr_cell_t *)(const void *)(racer_set + 1) + pick.cell;
            commands[out].cmd_srca = (uint16_t)(pool.slots[slot].addr >> 3);
            commands[out].cmd_colr = (uint16_t)((clut_vram + (uint32_t)cell->clut * 32u) >> 3);
#endif
#if E2_EMIT_HINT
            hint_drawn[c->id] = (uint8_t)slot;
#endif
            car_cell[c->id] = pick.cell; car_has_cell[c->id] = 1;
            if (c->id == 0) { fzx_e1_sprite_pos[0] = (uint32_t)(int32_t)items[i].place.x; fzx_e1_sprite_pos[1] = (uint32_t)(int32_t)items[i].place.y; }
            ++out; ++sprites;
        } else ++fzx_field_draw[3];
        EP(7);
    }
#undef LIST_USED
#if E2_EMIT_LINK
    if (out > seg_start) { fzx_emit_segs[fzx_emit_nsegs].src = &commands[seg_start]; fzx_emit_segs[fzx_emit_nsegs++].n = out - seg_start; }
    out += link_extra;
#endif
#if defined(E2_PROF) && E2_PROF
    {
        const uint16_t now_ = cpu_frt_count_get();
        fzx_e2_emit_split[1] += sh_ticks; fzx_e2_emit_split[2] += (uint16_t)((uint16_t)(now_ - es) - sh_ticks);
    }
#endif
    fzx_e1_cache[0] = pool.hits; fzx_e1_cache[1] = pool.misses; fzx_e1_cache[2] = pool.evictions; fzx_e1_cache[3] = pool.refused;
    fzx_field_draw[0] = sprites; fzx_field_draw[1] = solid; fzx_field_draw[2] = shadows; fzx_field_draw[5] = out - 1u;
#if E2_CULL
    fzx_e2_report[2] = sprites + solid; if (sprites + solid > fzx_e2_report[3]) fzx_e2_report[3] = sprites + solid;
    fzx_e2_report[4] += c_behind; fzx_e2_report[5] += c_far; fzx_e2_report[6] += c_off; fzx_e2_report[7] = shadows;
    fzx_e2_report[8] += c_behind + c_far + c_off;
#endif
    fzx_e1_report[10] = out - 1u;
#if E2_EMIT_PROF
    if (fzx_emit_prof_n < EMIT_PROF_ROWS) {
        uint16_t *row = fzx_emit_prof[fzx_emit_prof_n++];
        row[0] = (uint16_t)ticks;
        for (unsigned k = 1; k < 9u; ++k) row[k] = (uint16_t)ep_acc[k];
        row[9] = (uint16_t)(cpu_frt_count_get() - ep_start);
        row[10] = (uint16_t)ep_posed; row[11] = (uint16_t)shadows;
    }
#endif
    return out > 1u ? out : 0u;
}
#endif

/* ---- HUD ------------------------------------------------------------------------ */
void fzx_drive_hud(void) {
    fzx_drive_hud_text();
    dbgio_flush();
}

/* The HUD text is formatted before the frame's track wait (fzx_drive_hud_prepare, in the master's idle time on a
 * frame the slave's track holds up) and written to the dbgio buffer after it (fzx_drive_hud_text), so the previous
 * frame's flush, which sits in the wait for the present, still sends the previous frame's text. The strings are the
 * ones dbgio_printf formats, from the same state. */
static char hud_str[3][48];
static uint32_t hud_ready;
void fzx_drive_hud_prepare(void) {
    const uint32_t kmh = (uint32_t)(((int64_t)(body.speed < 0 ? -body.speed : body.speed) * 216) / (65536 * 10));
    const uint32_t ms = body.race_time > 0 ? (uint32_t)body.race_time : 0u;
    const uint32_t laps = (uint32_t)course.track.laps;
    uint32_t lap = (uint32_t)(body.laps_done > 0 ? body.laps_done : 1);
    if (lap > laps) lap = laps;
    snprintf(hud_str[0], sizeof(hud_str[0]), "[2;2H%4lu KM/H  LAP %lu/%lu  %lu'%02lu\"%02lu ", (unsigned long)kmh, (unsigned long)lap,
             (unsigned long)laps, (unsigned long)(ms / 60000u), (unsigned long)((ms / 1000u) % 60u),
             (unsigned long)((ms / 10u) % 100u));
    if (body.flags & SH2Z_VD_FINISHED) snprintf(hud_str[1], sizeof(hud_str[1]), "[4;14HFINISH");
    else if (body.intro > 0) snprintf(hud_str[1], sizeof(hud_str[1]), "[4;14H%s", body.intro > 180 ? "      " : body.intro > 120 ? "  3   " : body.intro > 60 ? "  2   " : "  1   ");
    else snprintf(hud_str[1], sizeof(hud_str[1]), "[4;14H      ");
    hud_ready = 1u;
}

void fzx_drive_hud_text(void) {
    if (!hud_ready) fzx_drive_hud_prepare();
    hud_ready = 0u;
    dbgio_puts(hud_str[0]);
    dbgio_puts(hud_str[1]);
}
