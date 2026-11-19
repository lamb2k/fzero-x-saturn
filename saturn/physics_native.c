/* E1 native player physics glue; see physics_native.h. MIT; see ../LICENSE. */
#include "physics_native.h"

#include <string.h>

/* Compile-time conversions; every value below folds to an integer constant. */
#define Q16(x) ((int32_t) ((x) * 65536.0 + ((x) >= 0 ? 0.5 : -0.5)))
#define Q30(x) ((int32_t) ((x) * 1073741824.0 + ((x) >= 0 ? 0.5 : -0.5)))

/* The race code's constants (Racer_UpdateFromControls, Racer_HitWall, the road shape
 * handlers and Racer_Update), as the plays-right table pins them. */
const sh2z_vd_rules_t fzx_phys_rules = {
    .dead_zone = Q30(0.1),
    .turn_spin = Q30(0.2),
    .lean_turn_mix = Q30(0.4),
    .grip_turn_mix = Q30(0.125),
    .damp_side = Q30(0.996),
    .damp_up = Q30(0.94),
    .damp_fwd = Q30(0.997),
    .damp_spin = Q30(0.997),
    .damp_crashed = Q30(0.98305196),
    .damp_rough = Q30(0.987),
    .damp_brake = Q30(0.987),
    .mine_radius = Q16(30.0),
    .mine_kick = Q16(15.0),
    .mine_damage = Q16(12.5),
    .ramp_gain = Q30(0.3),
    .ramp_base = Q16(0.5),
    .ramp_decay = Q30(0.8),
    .ramp_release = Q16(0.5),
    .gravity = Q16(1.0),
    .hover_spring = Q30(0.2),
    .hover_top = Q16(13.0),
    .hover_rise_gain = Q30(0.05),
    .hover_crashed = Q16(10.0),
    .double_tap = 15,
    .side_force = Q16(5.0),
    .side_fade = Q16(0.8333333),
    .side_cut = Q16(6.0),
    .spin_force = Q16(0.5),
    .spin_fade = Q16(0.25),
    .spin_cut = Q16(2.0),
    .lean_force = Q16(1.0),
    .lean_fade = Q16(0.125),
    .lean_cut = Q16(8.0),
    .lean_ice = Q30(0.2),
    .grip_tap = 30,
    .tap_window = 27,
    .contact_grip = 60,
    .slide_speed = Q16(8.0),
    .slide_ticks = 100,
    .turn_cost = Q30(0.5),
    .lean_cost = Q30(0.16),
    .speed_cap = Q16(138.9),
    .boost_ticks = 100,
    .air_road = Q16(40.0),
    .air_walled = Q16(145.0),
    .air_tunnel = Q16(210.0),
    .ground_band = Q16(15.0),
    .blend_band = Q16(30.0),
    .edge_widen = Q16(57.5),
    .edge_low = Q16(-100.0),
    .edge_damage = Q16(2.0),
    .tilt_follow = Q30(0.19),
    .tilt_input = Q30(0.15),
    .wall_damage = Q30(0.7),
    .wall_min = Q16(0.01),
    .wall_keep = Q30(0.998),
    .spin_bounce = Q16(2.0),
    .spin_steer = Q30(0.2),
    .spin_ticks = 150,
    .fall_damp = Q30(0.94),
    .fall_floor = Q16(-2750.0),
    .world_edge = Q16(20000.0),
    .control_tick = 40,
    .clock_tick = 40,
    .hover_tick = 250,
    .clock_num = 50,
    .clock_den = 3,
};

typedef struct {
    const uint8_t* p;
    const uint8_t* end;
    int bad;
} Reader;

static int32_t word(Reader* r) {
    uint32_t v;
    if (r->end - r->p < 4) {
        r->bad = 1;
        return 0;
    }
    v = ((uint32_t) r->p[0] << 24) | ((uint32_t) r->p[1] << 16) | ((uint32_t) r->p[2] << 8) | r->p[3];
    r->p += 4;
    return (int32_t) v;
}

static void words(Reader* r, int32_t* out, int n) {
    int i;
    for (i = 0; i < n; i++) out[i] = word(r);
}

int fzx_phys_load(FzxPhysCourse* c, const uint8_t* data, size_t size, int zones) {
    Reader r = { data, data + size, 0 };
    sh2z_vd_vehicle_t* v = &c->vehicle;
    sh2z_vd_body_t* b = &c->start;
    uint32_t nn, nz, nr, nm, i;

    memset(c, 0, sizeof(*c));
    if ((uint32_t) word(&r) != FZX_PHYS_MAGIC || (uint32_t) word(&r) != FZX_PHYS_VERSION) return -1;
    nn = (uint32_t) word(&r);
    nz = (uint32_t) word(&r);
    nr = (uint32_t) word(&r);
    nm = (uint32_t) word(&r);
    if (nn < 4 || nn > FZX_PHYS_MAX_NODES || nz > FZX_PHYS_MAX_ZONES || nr > FZX_PHYS_MAX_RAMPS ||
        nm > FZX_PHYS_MAX_MINES)
        return -2;
    c->track.length = word(&r);
    c->track.laps = word(&r);
    v->force_low = word(&r);
    v->force_slide = word(&r);
    v->force_high = word(&r);
    v->crossover = word(&r);
    v->boost_mul = word(&r);
    v->plate_mul = word(&r);
    v->slide_max = word(&r);
    v->slide_decay = word(&r);
    v->slew_base = word(&r);
    v->turn_free = word(&r);
    v->turn_grip = word(&r);
    v->top_mix = word(&r);
    v->accel_cutoff = word(&r);
    v->grip_limit = word(&r);
    v->slide_limit = word(&r);
    v->wall_bounce = word(&r);
    v->slide_mul = word(&r);
    v->bump = word(&r);
    v->slew_gain = word(&r);
    v->slew_slope = word(&r);
    v->max_energy = word(&r);
    v->boost_cost = word(&r);
    v->recharge = word(&r);
    v->hover_base = word(&r);
    v->top_force = word(&r);
    for (i = 0; i < nn; i++) {
        sh2z_vd_node_t* n = &c->nodes[i];
        int32_t packed;
        words(&r, n->pos, 3);
        words(&r, n->up, 3);
        n->tension = word(&r);
        n->half_left = word(&r);
        n->half_right = word(&r);
        n->length = word(&r);
        n->start = word(&r);
        words(&r, &n->mark_pos[0][0], 9);
        words(&r, n->mark_len, 3);
        packed = word(&r);
        n->shape = (uint8_t) (packed & 0xFF);
        n->zones_by_side = (uint8_t) ((packed >> 8) & 0xFF);
        packed = word(&r);
        n->zone_first = (uint16_t) packed;
        n->zone_count = (uint16_t) ((uint32_t) packed >> 16);
        packed = word(&r);
        n->ramp_first = (uint16_t) packed;
        n->ramp_count = (uint16_t) ((uint32_t) packed >> 16);
        packed = word(&r);
        n->mine_first = (uint16_t) packed;
        n->mine_count = (uint16_t) ((uint32_t) packed >> 16);
        if (n->length <= 0 || (uint32_t) n->zone_first + n->zone_count > nz ||
            (uint32_t) n->ramp_first + n->ramp_count > nr || (uint32_t) n->mine_first + n->mine_count > nm)
            return -3;
        if (!zones) n->zone_count = n->ramp_count = n->mine_count = 0;
    }
    for (i = 0; i < nz; i++) {
        c->zones[i].kind = (uint32_t) word(&r);
        c->zones[i].t0 = word(&r);
        c->zones[i].t1 = word(&r);
        words(&r, c->zones[i].lo, 3);
        words(&r, c->zones[i].hi, 3);
    }
    for (i = 0; i < nr; i++) {
        words(&r, c->ramps[i].pos, 3);
        words(&r, &c->ramps[i].axis[0][0], 9);
        words(&r, c->ramps[i].dim, 3);
    }
    for (i = 0; i < nm; i++) words(&r, c->mines[i].pos, 3);
    /* Starting state. */
    b->flags = (uint32_t) word(&r);
    b->node = (uint32_t) word(&r);
    b->intro = word(&r);
    b->lap = word(&r);
    b->laps_done = word(&r);
    b->race_ticks = word(&r);
    b->race_time = word(&r);
    words(&r, b->pos, 3);
    words(&r, b->vel, 3);
    words(&r, &b->body[0][0], 9);
    words(&r, &b->road[0][0], 9);
    words(&r, b->gravity_up, 3);
    words(&r, b->tilt_up, 3);
    words(&r, b->ground_up, 3);
    words(&r, b->fwd, 3);
    words(&r, b->spos, 3);
    words(&r, b->disp, 3);
    words(&r, b->ground, 3);
    words(&r, b->bvel, 3);
    b->t = word(&r);
    b->prop = word(&r);
    b->fwd_mag = word(&r);
    b->height = word(&r);
    b->half_left = word(&r);
    b->half_right = word(&r);
    b->lap_dist = word(&r);
    b->laps_dist = word(&r);
    b->race_dist = word(&r);
    b->speed = word(&r);
    b->energy = word(&r);
    b->hover = word(&r);
    b->accel_force = word(&r);
    b->spin_steer = word(&r);
    b->wall_bounce = v->wall_bounce;
    if (r.bad || r.p != r.end || b->node >= nn) return -4;
    c->track.nodes = c->nodes;
    c->track.zones = c->zones;
    c->track.ramps = c->ramps;
    c->track.mines = c->mines;
    c->track.count = nn;
    return 0;
}

uint16_t fzx_phys_buttons_n64(uint16_t n64) {
    uint16_t b = 0;
    if (n64 & 0x8000u) b |= SH2Z_VD_BTN_ACCEL;
    if (n64 & 0x4000u) b |= SH2Z_VD_BTN_BOOST;
    if (n64 & 0x2000u) b |= SH2Z_VD_BTN_LEAN_L;
    if (n64 & 0x0010u) b |= SH2Z_VD_BTN_LEAN_R;
    if (n64 & 0x0004u) b |= SH2Z_VD_BTN_BRAKE;
    if (n64 & ~(0x8000u | 0x4000u | 0x2000u | 0x0010u | 0x0004u)) b |= SH2Z_VD_BTN_OTHER;
    return b;
}

/* Proposed Saturn digital pad mapping (physics_native.h). At one call per 30 Hz frame
 * the stick reaches full lock in 4 frames (133 ms). */
#define FZX_PAD_RAMP 16

void fzx_phys_pad(FzxPadState* s, uint16_t pad, sh2z_vd_input_t* in) {
    uint16_t held = 0;
    int target = 0;
    int x = s->stick_x;
    if (pad & FZX_PAD_A) held |= SH2Z_VD_BTN_ACCEL;
    if (pad & FZX_PAD_B) held |= SH2Z_VD_BTN_BOOST;
    if (pad & FZX_PAD_C) held |= SH2Z_VD_BTN_BRAKE;
    if (pad & FZX_PAD_L) held |= SH2Z_VD_BTN_LEAN_L;
    if (pad & FZX_PAD_R) held |= SH2Z_VD_BTN_LEAN_R;
    if (pad & (FZX_PAD_X | FZX_PAD_Y | FZX_PAD_Z | FZX_PAD_START)) held |= SH2Z_VD_BTN_OTHER;
    if ((pad & (FZX_PAD_LEFT | FZX_PAD_RIGHT)) == FZX_PAD_RIGHT) target = 63;
    else if ((pad & (FZX_PAD_LEFT | FZX_PAD_RIGHT)) == FZX_PAD_LEFT) target = -63;
    if (target == 0 || (target > 0) != (x > 0)) x = 0;
    if (target > 0) x = x + FZX_PAD_RAMP > target ? target : x + FZX_PAD_RAMP;
    if (target < 0) x = x - FZX_PAD_RAMP < target ? target : x - FZX_PAD_RAMP;
    in->stick_x = (int8_t) x;
    in->stick_y = (int8_t) ((pad & FZX_PAD_UP) ? -63 : (pad & FZX_PAD_DOWN) ? 63 : 0);
    in->pressed = (uint16_t) (held & ~s->held);
    in->released = (uint16_t) (s->held & ~held);
    in->held = held;
    s->held = held;
    s->stick_x = (int8_t) x;
}
