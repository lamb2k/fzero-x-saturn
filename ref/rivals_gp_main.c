/* Native rival field on HOST (E2): the SH2ZAM line field against the baked course line.
 * MIT; see ../LICENSE. Host headers and SH2ZAM only: no original game code is linked.
 *
 *   fzx-rivals-gp --pack RIVALS.PAK --length Q8 --config FIELD.txt --seed N --report OUT.json
 *                 [--player-trace TRACE.bin --start-tick T] [--ticks MAX] [--lod 0|1] [--hash-every N]
 *                 [--control none|no-avoid|no-contact|stuck] [--zones ZONES.txt] [--wrap MASK:VALUE]
 *
 * FIELD.txt has one line per vehicle: id grid(1..) target_lap_ms chase(255 none) max_energy aggression
 * [finish_ms [first_lap_cap]]: line-field-2's finish target from the second lap on and line-field-3's
 * first-lap chase cap, Q8 per tick (0 or absent = none).
 * Vehicle 0 is the player. With --player-trace it is external (float32 dist, lateral, speed per tick
 * from a reference run); without it, vehicle 0 is one more agent.
 * The report has the same shape as fzx-gp-ref's, so one script computes the bands for both.
 * C-118: --player-phys PHYS.bin (fzx-gp-ref --trace-phys) drives vehicle 0 physically after its finish. Through the
 * contact pass on the reference finish tick (the first PHYS record with the finished flag) the replay is the one
 * above, unchanged. From the next tick, vehicle 0's line state comes from the reference's continuing pose: its
 * distance is the finish distance plus the segment walk's lapDistance moved since (wrapped once round the lap),
 * its lateral the pose's (in a pipe or cylinder the arc round the line, from lateral and up, as the engine's tube
 * pose), its speed the distance moved since the previous tick. Neighbour choice, avoidance and contact all read that
 * one state. Its official finish (tick, place, lap times) stays as recorded at the finish. A missing pose (the
 * PHYS file ends, the segment walk has no segment, the car retired) or an ambiguous one (a step of more than a
 * quarter lap, a tube pose on the line's axis) keeps the last pose and makes the run unscorable (report
 * "player_phys"). --p0-dump FILE writes vehicle 0's input each tick (int32 tick, dist, lap, lateral, speed).
 * line-field-4: --zones takes fzx-rivals' zone table (start end lat_lo lat_hi kind, Q8); --wrap makes nodes whose
 * flags match MASK:VALUE tubes (the course's pipes and cylinders: 0x180:0x80). Tuning names plate_ticks,
 * slip_steer, rough_keep and recharge set the surface; --dash Q8 sets the final dash (fzx-rivals writes
 * ZONES.txt.dash). None of them: line-field-3 step for step. */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sh2zam/line_field_experimental.h"

#define MAXV 30
#define MAXN 4096
static void die(const char* m) { fprintf(stderr, "fzx-rivals-gp: %s\n", m); exit(2); }
static sh2z_line_node_t nodes[MAXN];
static sh2z_field_zone_t zones[256];
static sh2z_field_surface_t surface;
static sh2z_field_behave_t behave;   /* line-field-5 (--behave 1) */
#ifndef DROP_LAT
#define DROP_LAT 117965    /* 1.8 of the radius round (about 103 degrees; the reference falls at 105 and 151) */
#endif
#ifndef LAUNCH_CHANCE
#define LAUNCH_CHANCE 4096   /* per launch out of a tube on a drop node (tuning seeds: 2 in 12, reference 2) */
#endif
#ifndef DROP_CHANCE
#define DROP_CHANCE 16     /* per step beyond it (HOST tuning seeds 1-6, 101-106: 3 falls in 12 races, reference 4) */
#endif
static uint32_t be32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const unsigned char* p) { return (uint16_t)((p[0] << 8) | p[1]); }

#define U(x) ((int32_t)((x) * 256))   /* whole units to Q8 */

int main(int argc, char** argv) {
    const char *pack = 0, *config = 0, *report = 0, *trace = 0, *control = "none", *tune = 0, *zone_file = 0, *wrap = 0;
    const char *phys_file = 0, *p0_dump = 0;   /* C-118 */
    int tube_angles = 0; long tube_ticks = 0, tube_bin[4] = {0, 0, 0, 0};
    /* line-field-5: --sources 1 adds rivals' energy losses by source (contact, wall, boost, other), wall incidents
     * and the near-edge share, as tools judge the reference's (fzx-gp-ref --diag). */
    int use_behave = 0; double tube_clamp = 0;
    const char* fixture = "";
    const char* drops_file = NULL;
    int sources = 0; double src_amount[4] = {0, 0, 0, 0}; long src_n[4] = {0, 0, 0, 0}, wall_inc = 0, edge_ticks = 0, road_ticks = 0;
    int32_t tune_accel = 40, tune_grip = U(6), tune_lane = 30;
    long length = 0, dash = 0; unsigned seed = 1; int max_ticks = 14000, start_tick = 420, lod = 1, hash_every = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--pack")) pack = argv[i + 1];
        else if (!strcmp(argv[i], "--length")) length = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--config")) config = argv[i + 1];
        else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--report")) report = argv[i + 1];
        else if (!strcmp(argv[i], "--player-trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--player-phys")) phys_file = argv[i + 1];
        else if (!strcmp(argv[i], "--p0-dump")) p0_dump = argv[i + 1];
        else if (!strcmp(argv[i], "--start-tick")) start_tick = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) max_ticks = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--lod")) lod = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--hash-every")) hash_every = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--control")) control = argv[i + 1];
        else if (!strcmp(argv[i], "--tune")) tune = argv[i + 1];
        else if (!strcmp(argv[i], "--zones")) zone_file = argv[i + 1];
        else if (!strcmp(argv[i], "--wrap")) wrap = argv[i + 1];
        else if (!strcmp(argv[i], "--dash")) dash = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--tube-angles")) tube_angles = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--tube-clamp")) tube_clamp = atof(argv[i + 1]);   /* C-095 control: angles held below this (degrees) */   /* 1: tube_angles over pipe and cylinder nodes */
        else if (!strcmp(argv[i], "--sources")) sources = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--behave")) use_behave = atoi(argv[i + 1]);   /* line-field-5 behaviours, defaults below */
        else if (!strcmp(argv[i], "--fixture")) fixture = argv[i + 1];
        else if (!strcmp(argv[i], "--drops")) drops_file = argv[i + 1];   /* line-field-5 drop nodes (tools/rivals_drops.py) */   /* C-100 fixtures: relabel, stuck, suppress */
        else die("unknown argument");
    }
    if (!pack || !config || !report || length <= 0) die("usage: see rivals_gp_main.c");
    FILE* f = fopen(pack, "rb");
    if (!f) die("cannot open pack");
    static unsigned char buf[MAXN * 48];
    const size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n == 0 || n % 48) die("pack size is not nodes x 48");
    const uint32_t count_nodes = (uint32_t)(n / 48);
    for (uint32_t i = 0; i < count_nodes; ++i) {
        const unsigned char* p = buf + 16 * i;
        nodes[i].start = (int32_t)be32(p); nodes[i].lateral = (int16_t)be16(p + 4); nodes[i].speed_cap = be16(p + 6);
        nodes[i].half_width = be16(p + 8); nodes[i].flags = be16(p + 10); nodes[i].inv_span = be32(p + 12);
    }
    if (drops_file) {   /* line-field-5: node flag 0x0200 on the drop ranges (the engine sets it from rivals_drops.h) */
        FILE* df = fopen(drops_file, "r");
        if (!df) die("cannot open drops");
        char ln[256];
        while (fgets(ln, sizeof ln, df)) {
            unsigned a, b2;
            if (ln[0] == '#' || sscanf(ln, "%u %u", &a, &b2) != 2) continue;
            for (unsigned k = a; k <= b2 && k < count_nodes; ++k) nodes[k].flags |= 0x0200u;
        }
        fclose(df);
    }
    const sh2z_line_t L = { nodes, count_nodes, (int32_t)length };

    /* Tuning (Q8 units; see generated/RIVALS-NATIVE-PLAN.md for where each value comes from). */
    sh2z_field_params_t P;
    memset(&P, 0, sizeof P);
    P.pace_gain = 3277;            /* 0.05 per unit, as the source's distance term */
    P.pace_max = 81920;            /* 1.25 x pace */
    P.chase_gain = 655;            /* 0.01 per unit, as the source's chase term */
    P.steer_gain = 9830;           /* 0.15 */
    P.margin = U(6);
    P.avoid_long = U(92); P.avoid_lat = U(69); P.avoid_offset = U(92);   /* the source's 92 / 69 / 92 */
    P.avoid_ticks = 0;             /* the source's flat trigger: distanceToRacerAhead < 92, no closing look-ahead (cpu.c) */
    P.pass_hold = 60;
    P.contact_long = U(46); P.contact_lat = U(40);
    P.bump_keep = 16384;
    P.hit_energy = 32768; P.side_energy = U(2); P.attack_energy = U(10);
    P.wall_energy = U(3); P.wall_keep = 65405;   /* keep 0.998 a tick on the edge (the source wall routine scales velocity by 0.998) */
    P.attack_long = U(184); P.attack_lat = U(92);                        /* the source's 184 / 92 */
    P.boost_speed = U(6); P.boost_energy = U(10); P.boost_min_share = 19661; P.boost_min_cap = 0; P.boost_behind = U(150);
    P.cruise = U(40);
    P.rank_lead = U(8);
    P.lod_near[0] = lod ? U(3000) : INT32_MAX; P.lod_near[1] = lod ? U(8000) : INT32_MAX; P.lod_near[2] = lod ? U(16000) : INT32_MAX;
    P.boost_ticks = 90; P.laps = 3; P.boost_from_lap = 1;
    /* line-field-4 surface (zones, tubes); the values are the engine's (engine_field.c). */
    surface.plate_ticks = 60; surface.slip_steer = 16384; surface.rough_keep = 58982; surface.recharge = 512;
    if (zone_file) {
        FILE* zf = fopen(zone_file, "r");
        if (!zf) die("cannot open zones");
        long a, b, lo, hi; int k;
        while (surface.zone_count < 256 && fscanf(zf, "%ld %ld %ld %ld %d", &a, &b, &lo, &hi, &k) == 5) {
            sh2z_field_zone_t* z = &zones[surface.zone_count++];
            z->start = (int32_t)a; z->end = (int32_t)b; z->lat_lo = (int32_t)lo; z->lat_hi = (int32_t)hi; z->kind = (uint32_t)k;
        }
        fclose(zf);
        surface.zones = zones;
    }
    if (wrap) {
        unsigned m = 0, v = 0;
        if (sscanf(wrap, "%i:%i", &m, &v) != 2) die("--wrap MASK:VALUE");
        surface.wrap_mask = (uint16_t)m; surface.wrap_value = (uint16_t)v;
    }
    if (zone_file || wrap) P.surface = &surface;
    P.dash_from = (int32_t)dash; P.dash_pace = 72090;   /* 1.10 */
    /* line-field-5 defaults (claude/line-field-5; see the lane's FROZEN-FALLS.txt and step-1 report): the source's tube
     * bias pick (1/64 a decision; half the picks hold to an exit, the rest 20..35 decisions), cylinders hold their
     * angle, pipes end rolls near their exit; air nodes (shape 0x180), open-edge road (0x000); the KO window 120. */
    if (use_behave) {
        behave.roll_chance = 1024; behave.roll_latch = 32768; behave.roll_min = 20; behave.roll_span = 16;
        behave.roll_lead = 26214;  /* 0.4 of speed */
        behave.hold_lead = 7864;   /* 0.12 of speed */
        behave.roll_well = 52429;  /* 0.8 radians round a pipe */
        behave.hold_mask = 0x01c0; behave.hold_value = 0x00c0; behave.exit_mask = 0x01c0; behave.exit_value = 0x0080;
        behave.roll_exit_ahead = U(3000);
        behave.lat_accel = 192;
        behave.air_mask = 0x01c0; behave.air_value = 0x0180; behave.open_mask = 0x01c0; behave.open_value = 0x0000;
        behave.air_spread = 2600; behave.fall_margin = U(30); behave.fall_ticks = 95;
        behave.ko_window = 120; behave.ko_near = U(1500); behave.ko_floor = 26;
        behave.drop_mask = 0x0200; behave.drop_value = 0x0200; behave.drop_lat = DROP_LAT; behave.drop_chance = DROP_CHANCE; behave.launch_chance = LAUNCH_CHANCE;
        behave.contact_norm = 36045;   /* 0.55: racer.c's impact, 1.1 x the closing speed along the line between the two, shared */
        P.behave = &behave;
    }
    /* Tuning overrides for HOST experiments only: --tune "name=value,..." (integers, the field's own units). */
    if (tune) {
        char tb[1024]; strncpy(tb, tune, sizeof tb - 1); tb[sizeof tb - 1] = 0;
        for (char* tok = strtok(tb, ","); tok; tok = strtok(NULL, ",")) {
            char* eq = strchr(tok, '='); if (!eq) die("--tune name=value"); *eq = 0; const int32_t val = (int32_t)atol(eq + 1);
            #define T(n) if (!strcmp(tok, #n)) { P.n = val; continue; }
            T(pace_gain) T(pace_max) T(chase_gain) T(steer_gain) T(avoid_long) T(avoid_lat) T(avoid_offset) T(avoid_ticks) T(pass_hold) T(contact_long) T(contact_lat)
            T(bump_keep) T(hit_energy) T(side_energy) T(attack_energy) T(wall_energy) T(wall_keep) T(boost_speed) T(boost_energy) T(boost_behind) T(cruise) T(rank_lead)
            #undef T
            if (!strcmp(tok, "boost_ticks")) { P.boost_ticks = (int16_t)val; continue; }
            #define B(n) if (!strcmp(tok, #n)) { behave.n = val; continue; }
            B(gap_reach_ticks) B(drop_lat) B(drop_chance) B(launch_chance) B(contact_norm)
            B(roll_chance) B(roll_latch) B(roll_lead) B(roll_well) B(hold_lead) B(roll_exit_ahead) B(lat_accel) B(air_spread) B(fall_margin) B(fall_ticks) B(ko_window) B(ko_near) B(ko_floor)
            #undef B
            if (!strcmp(tok, "roll_min")) { behave.roll_min = (int16_t)val; continue; }
            if (!strcmp(tok, "roll_span")) { behave.roll_span = (int16_t)val; continue; }
            if (!strcmp(tok, "open_mask")) { behave.open_mask = (uint16_t)val; continue; }
            if (!strcmp(tok, "hold_mask")) { behave.hold_mask = (uint16_t)val; continue; }
            if (!strcmp(tok, "plate_ticks")) { surface.plate_ticks = val; continue; }
            if (!strcmp(tok, "slip_steer")) { surface.slip_steer = val; continue; }
            if (!strcmp(tok, "rough_keep")) { surface.rough_keep = val; continue; }
            if (!strcmp(tok, "recharge")) { surface.recharge = val; continue; }
            if (!strcmp(tok, "dash_pace")) { P.dash_pace = val; continue; }
            if (!strcmp(tok, "accel")) { tune_accel = val; continue; }
            if (!strcmp(tok, "grip")) { tune_grip = val; continue; }
            if (!strcmp(tok, "lane")) { tune_lane = val; continue; }
            /* Distance level of detail (whole units): full rate inside lod0, 1/2 inside lod1, 1/4 inside lod2. */
            if (!strcmp(tok, "lod0")) { P.lod_near[0] = U(val); continue; }
            if (!strcmp(tok, "lod1")) { P.lod_near[1] = U(val); continue; }
            if (!strcmp(tok, "lod2")) { P.lod_near[2] = U(val); continue; }
            die("unknown tuning name");
        }
    }
    if (!strcmp(control, "no-avoid")) { P.avoid_long = 0; P.avoid_ticks = 0; }
    if (!strcmp(control, "no-contact")) { P.contact_long = 0; }

    static sh2z_vehicle_t veh[MAXV];
    static sh2z_field_brain_t brain[MAXV];
    static uint8_t order[MAXV], slot[MAXV], rank_order[MAXV];
    static int grid[MAXV], target_ms[MAXV];
    memset(veh, 0, sizeof veh); memset(brain, 0, sizeof brain);
    f = fopen(config, "r");
    if (!f) die("cannot open config");
    int count = 0, id, g, ms, chase, aggr, finish, cap1; double maxe;
    char row[256];
    uint32_t rs = seed * 2654435761u + 12345u;   /* set-up variation, separate from the field's generator */
    #define RND() (rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5, rs)
    while (fgets(row, sizeof row, f)) {
        finish = cap1 = 0;
        if (sscanf(row, "%d %d %d %d %lf %d %d %d", &id, &g, &ms, &chase, &maxe, &aggr, &finish, &cap1) < 6) break;
        if (id != count || count >= MAXV) die("config ids must run 0..n-1");
        sh2z_vehicle_t* v = &veh[id]; sh2z_field_brain_t* b = &brain[id];
        sh2z_field_grid_slot(&L, (uint32_t)(g - 1), U(146), U(100), U(50), 4, &v->t);   /* the source grid: 146 back, 100 per place, 4 columns of 100 */
        grid[id] = g; target_ms[id] = ms;
        v->max_energy = v->energy = (int32_t)(maxe * 256);
        v->accel = tune_accel; v->brake = 96; v->top_speed = U(60); v->grip = tune_grip;
        v->armor = (uint16_t)(256 * 178 / (maxe > 1 ? maxe : 178));   /* lighter bodies take more damage */
        v->flags = 0;
        const int lap_ticks = (ms > 0 ? ms : 36000) * 60 / 1000;
        b->pace = (int32_t)(length / lap_ticks);
        b->pace += (int32_t)((int64_t)b->pace * ((int)(RND() % 81) - 40) / 10000);   /* +-0.4% skill variation: no two rivals tie for long */
        b->lane = (int32_t)((int)(RND() % (uint32_t)(2 * tune_lane + 1)) - tune_lane) * 256;
        b->chase = (int16_t)(chase == 255 ? -1 : chase);
        b->chase_lead = (int16_t)(chase == 0 ? (int)(RND() % 400) - 10 : 30);   /* the source's offset ranges */
        b->aggression = (uint8_t)aggr;
        b->finish_ticks = finish > 0 ? finish * 60 / 1000 : 0;   /* line-field-2: race ticks */
        b->first_lap_cap = cap1 > 0 ? cap1 : 0;                  /* line-field-3: Q8 per tick */
        count++;
    }
    fclose(f);
    if (count < 2) die("need at least two vehicles");
    if (!strcmp(control, "stuck")) { veh[count - 1].top_speed = 0; }

    /* Player trace (external vehicle 0). */
    static float tr[20000][3]; int trace_ticks = 0;
    if (trace) {
        f = fopen(trace, "rb");
        if (!f) die("cannot open trace");
        trace_ticks = (int)fread(tr, sizeof tr[0], 20000, f);
        fclose(f);
        veh[0].flags |= SH2Z_VEH_EXTERNAL;
    }
    /* C-118: the reference's physical pose after the finish (see the header). */
    enum { PHYS_MAX = 24000 };
    static double ph_total[PHYS_MAX]; static float ph_lat[PHYS_MAX], ph_up[PHYS_MAX]; static unsigned char ph_bad[PHYS_MAX];
    int ph_n = 0, ph_switch = -1, ph_ticks = 0, ph_tube = 0, ph_missing = 0, ph_ambiguous = 0, ph_on = 0;
    int32_t ph_lap_tick[4] = {0, 0, 0, 0}, ph_finish_tick = 0; uint8_t ph_finish_seq = 0;
    if (phys_file) {
        if (!trace) die("--player-phys needs --player-trace");
        f = fopen(phys_file, "rb");
        if (!f) die("cannot open player-phys");
        static float ph_ld[PHYS_MAX]; static int32_t ph_seg[PHYS_MAX], ph_flags[PHYS_MAX];
        unsigned char rec[48];
        while (ph_n < PHYS_MAX && fread(rec, sizeof rec, 1, f) == 1) {
            int32_t ri[4]; float rf[8];
            memcpy(ri, rec, sizeof ri); memcpy(rf, rec + 16, sizeof rf);
            if (ri[0] != ph_n) die("player-phys ticks are not consecutive from 0");
            ph_seg[ph_n] = ri[1]; ph_flags[ph_n] = ri[2]; ph_ld[ph_n] = rf[0]; ph_lat[ph_n] = rf[1]; ph_up[ph_n] = rf[2];
            ++ph_n;
        }
        fclose(f);
        for (int k = 0; k < ph_n && k < trace_ticks; ++k) if (ph_flags[k] & (1 << 25)) { ph_switch = k; break; }
        if (ph_switch >= 0) {
            const double lap_len = (double)length / 256.0;
            ph_total[ph_switch] = tr[ph_switch][0];
            for (int k = ph_switch + 1; k < ph_n; ++k) {
                double d = (double)ph_ld[k] - (double)ph_ld[k - 1];
                if (d < -lap_len / 2) d += lap_len;
                else if (d > lap_len / 2) d -= lap_len;
                ph_bad[k] = (unsigned char)((fabs(d) > lap_len / 4) ? 2 : (ph_seg[k] < 0 || (ph_flags[k] & (1 << 18))) ? 1 : 0);
                ph_total[k] = ph_total[k - 1] + d;
            }
        }
    }
    FILE* p0f = p0_dump ? fopen(p0_dump, "wb") : NULL;
    if (p0_dump && !p0f) die("cannot write p0-dump");
    sh2z_field_t F;
    sh2z_field_init(&F, &L, &P, veh, brain, order, slot, rank_order, (uint32_t)count, seed);
    F.focus = 0;

    static int prev_rank[MAXV];
    int rank_change_ticks = 0, rank_swaps = 0, ticks = 0, offtrack = 0, stuck = 0, through = 0;
    /* C-100 (line-field-5 part 2). The off-track population is every non-external agent on every race tick (the
     * accepted E2 exact predicate: offtrack_ticks). It is split into three exclusive classes, precedence fallen >
     * airborne > grounded: fallen = SH2Z_VEH_FALLING or brain.fall_tick >= 0 (set once at the fall start, never reset:
     * the fall, then the retirement fall_ticks later, then the stopped car to the end of the run); airborne =
     * SH2Z_VEH_AIR; grounded = the rest (retired-without-fall and finished agents included). offtrack_ticks = the sum
     * of the three out-of-bounds counts; the revised predicate is the grounded count alone.
     * Geometry trace, independent of the agents' flags: the node under each agent at the start of its last step (the
     * position it held before the step, as the library samples it) and the node after it, from the pack's flags.
     * air_unexplained: AIR set but the step began on no air node (or on a tube, or the agent was finished/retired);
     * air_missing: the step began on an air node, the agent active, but no AIR and no fall; fall_suppressed: the step
     * began on an air node and ended beyond half width + fall_margin on an open-edge node, but the agent is not falling. */
    static long ot_elig[3], ot_oob[3];
    static long geo_unexplained, geo_missing, geo_suppressed;
    static int32_t pre_node[MAXV], pre_dist[MAXV];
    static uint16_t step_node_flags[MAXV]; static int stepped_any[MAXV];
    static int air_run[MAXV]; static long air_eps, air_landed, air_fell, air_ticks, air_max, air_out_eps;
    static int air_out[MAXV];
    static long fall_eps, fall_retired, fall_open, fall_max; static int fall_start_t[MAXV];
    static long fall_drop; static int pre_flags_drop[MAXV];   /* falls that start on (or launch from) a drop node */
    static int fix_done, fix_agent = -1, fix_ticks;
    static int slow_ticks[MAXV];
    double spread_sum = 0, spread_max = 0; int spread_n = 0; double spread_at[32]; int marks = 0;
    double speed_sum[MAXV] = {0}; int speed_n[MAXV] = {0};
    struct timespec t0, t1; double ns = 0;
    FILE* hf = NULL;
    if (hash_every) { char hp[4096]; snprintf(hp, sizeof hp, "%s.hash", report); hf = fopen(hp, "w"); }
    for (int i = 0; i < count; ++i) prev_rank[i] = i;
    for (int t = 0; t < max_ticks; ++t) {
        if (trace) {
            const int k = t < trace_ticks ? t : trace_ticks - 1;
            const int64_t total = (int64_t)((double)tr[k][0] * 256.0);
            int32_t lap = 0; int64_t d = total;
            while (d < 0) { d += length; --lap; }
            while (d >= length) { d -= length; ++lap; }
            veh[0].t.dist = (int32_t)d; veh[0].t.lap = lap; veh[0].t.lateral = (int32_t)(tr[k][1] * 256.0f);
            veh[0].t.speed = k > 0 && t < trace_ticks ? (int32_t)(((double)tr[k][0] - (double)tr[k - 1][0]) * 256.0) : 0;
            if (veh[0].t.speed < 0) veh[0].t.speed = 0;
            /* The reference's race distance can jump in one tick (over a jump, with the zones and jumps set
             * up): no vehicle covers more than 80 units a tick, so a jump is not a closing speed. */
            if (veh[0].t.speed > U(80)) veh[0].t.speed = U(80);
            veh[0].t.node = sh2z_line_locate(&L, veh[0].t.dist, veh[0].t.node);
            veh[0].kick_lat = veh[0].kick_speed = 0;
            veh[0].energy = veh[0].max_energy;   /* the trace carries its own outcome */
            /* C-118: from the contact pass after the reference finish tick, the physical pose. */
            if (ph_switch >= 0 && t > ph_switch) {
                if (!ph_on) {
                    ph_on = 1;
                    for (int k = 0; k < 4; ++k) ph_lap_tick[k] = brain[0].lap_tick[k];
                    ph_finish_tick = brain[0].finish_tick; ph_finish_seq = brain[0].finish_seq;
                }
                static sh2z_track_state_t ph_last; static int ph_have;
                ++ph_ticks;
                int bad = t >= ph_n ? 1 : ph_bad[t];
                sh2z_track_state_t s0 = veh[0].t;
                if (!bad) {
                    const int64_t pt = (int64_t)(ph_total[t] * 256.0);
                    int32_t plap = 0; int64_t pd = pt;
                    while (pd < 0) { pd += length; --plap; }
                    while (pd >= length) { pd -= length; ++plap; }
                    s0.dist = (int32_t)pd; s0.lap = plap;
                    s0.node = sh2z_line_locate(&L, s0.dist, ph_have ? ph_last.node : veh[0].t.node);
                    sh2z_line_sample_t at;
                    sh2z_line_sample(&L, s0.node, s0.dist, &at);
                    const double lat = ph_lat[t], up = ph_up[t];
                    if (surface.wrap_mask && (at.flags & surface.wrap_mask) == surface.wrap_value && at.half_width > 0) {
                        const int cyl = (at.flags & 0x01c0u) == 0x00c0u;
                        if (lat * lat + up * up < 1e-6) bad = 2;
                        else {
                            const double a = atan2(fabs(lat), cyl ? up : -up);
                            s0.lateral = (int32_t)((lat < 0 ? -a : a) * at.half_width);
                            ++ph_tube;
                        }
                    } else s0.lateral = (int32_t)(lat * 256.0);
                    const double dv = (ph_total[t] - ph_total[t - 1]) * 256.0;
                    s0.speed = dv < 0 ? 0 : dv > U(80) ? U(80) : (int32_t)dv;
                }
                if (bad) {
                    if (bad == 2) ++ph_ambiguous; else ++ph_missing;
                    s0 = ph_have ? ph_last : veh[0].t;
                    s0.speed = 0;
                }
                veh[0].t = s0; ph_last = s0; ph_have = 1;
            }
            if (p0f) { const int32_t pr[5] = { t, veh[0].t.dist, veh[0].t.lap, veh[0].t.lateral, veh[0].t.speed }; fwrite(pr, sizeof pr, 1, p0f); }
        }
        static int32_t e_before[MAXV]; static uint16_t f_before[MAXV];
        for (int i = 0; i < count; ++i) { e_before[i] = veh[i].energy; f_before[i] = veh[i].flags; pre_node[i] = veh[i].t.node; pre_dist[i] = veh[i].t.dist; pre_flags_drop[i] = (nodes[veh[i].t.node].flags & 0x0200u) != 0; }
        if (t == start_tick) sh2z_field_start(&F);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        sh2z_field_step(&F);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (ph_on) {   /* C-118: vehicle 0's official record stays the one made at its finish */
            for (int k = 0; k < 4; ++k) brain[0].lap_tick[k] = ph_lap_tick[k];
            brain[0].finish_tick = ph_finish_tick; brain[0].finish_seq = ph_finish_seq;
        }
        if (F.started) ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
        ticks = t + 1;
        if (hf && F.started && (t - start_tick) % hash_every == 0) fprintf(hf, "%d %08x\n", t - start_tick, (unsigned)sh2z_field_hash(&F));
        if (!F.started) continue;
        /* C-100 fixtures: each must fail the geometry trace or the grounded check (see the comment at ot_elig). */
        if (*fixture && t - start_tick > 600) {
            if (!strcmp(fixture, "relabel") && !fix_done) {   /* a grounded agent pushed off the road and flagged AIR */
                for (int i = 1; i < count && !fix_done; ++i) {
                    sh2z_vehicle_t* v = &veh[i];
                    if (v->flags & (SH2Z_VEH_AIR | SH2Z_VEH_FALLING | SH2Z_VEH_FINISHED | SH2Z_VEH_RETIRED) || brain[i].fall_tick >= 0) continue;
                    sh2z_line_sample_t at; sh2z_line_sample(&L, v->t.node, v->t.dist, &at);
                    if (P.surface && surface.wrap_mask && (at.flags & surface.wrap_mask) == surface.wrap_value) continue;
                    if ((at.flags & 0x01c0u) == 0x0180u) continue;
                    v->t.lateral = at.half_width + U(10); v->flags |= SH2Z_VEH_AIR; fix_done = 1; fix_agent = i;
                }
            } else if (!strcmp(fixture, "stuck")) {   /* after a landing, AIR stays set for 30 ticks */
                if (fix_agent < 0) {
                    for (int i = 1; i < count; ++i)
                        if ((f_before[i] & SH2Z_VEH_AIR) && !(veh[i].flags & (SH2Z_VEH_AIR | SH2Z_VEH_FALLING)) && brain[i].fall_tick < 0) { fix_agent = i; break; }
                }
                if (fix_agent > 0 && fix_ticks < 30) { veh[fix_agent].flags |= SH2Z_VEH_AIR; ++fix_ticks; }
            } else if (!strcmp(fixture, "suppress")) {   /* the first fall is undone, every tick */
                for (int i = 1; i < count; ++i) {
                    if (!(veh[i].flags & SH2Z_VEH_FALLING) || (fix_agent >= 0 && fix_agent != i)) continue;
                    fix_agent = i; veh[i].flags = (uint16_t)((veh[i].flags & ~SH2Z_VEH_FALLING) | SH2Z_VEH_AIR); brain[i].fall_tick = -1; ++fix_ticks;
                }
            } else if (!strcmp(fixture, "wedge")) {
                /* C-118 control: a CPU-CPU wedge held after the player's finish. At the first tick vehicle 0 is driven
                 * physically, the leading CPU still racing (A) and the next one by distance (B) are set 20 apart on one
                 * lateral, and from then on both are held at speed 0 (top speed 0). The stuck check must still count them. */
                static int wedge_b = -1;
                if (ph_on && fix_agent < 0) {
                    int32_t best = INT32_MIN, second = INT32_MIN;
                    for (int i = 1; i < count; ++i) {
                        if (veh[i].flags & (SH2Z_VEH_FINISHED | SH2Z_VEH_RETIRED | SH2Z_VEH_FALLING | SH2Z_VEH_STOPPED)) continue;
                        const int32_t tot = sh2z_track_total(&L, &veh[i].t);
                        if (tot > best) { second = best; wedge_b = fix_agent; best = tot; fix_agent = i; }
                        else if (tot > second) { second = tot; wedge_b = i; }
                    }
                    if (fix_agent > 0 && wedge_b > 0) {
                        sh2z_vehicle_t *a = &veh[fix_agent], *b = &veh[wedge_b];
                        b->t = a->t; b->t.dist -= U(20);
                        if (b->t.dist < 0) { b->t.dist += (int32_t)length; --b->t.lap; }
                        b->t.node = sh2z_line_locate(&L, b->t.dist, a->t.node);
                    } else fix_agent = 0;
                }
                if (fix_agent > 0 && wedge_b > 0) {
                    veh[fix_agent].top_speed = veh[wedge_b].top_speed = 0; veh[fix_agent].t.speed = veh[wedge_b].t.speed = 0; ++fix_ticks;
                }
            } else if (strcmp(fixture, "relabel") && strcmp(fixture, "stuck") && strcmp(fixture, "suppress")) die("--fixture relabel|stuck|suppress|wedge");
        }
        if (sources) for (int i = 1; i < count; ++i) {
            const sh2z_vehicle_t* v = &veh[i];
            if (f_before[i] & (SH2Z_VEH_RETIRED | SH2Z_VEH_FINISHED)) continue;
            const int32_t loss = e_before[i] - v->energy;
            const int src = (v->flags & SH2Z_VEH_TOUCH) ? 0 : (v->flags & SH2Z_VEH_WALL) ? 1 : (v->flags & SH2Z_VEH_BOOST) ? 2 : 3;
            if (loss > 0) { src_amount[src] += loss / 256.0; ++src_n[src]; }
            if ((v->flags & SH2Z_VEH_WALL) && !(v->flags & SH2Z_VEH_TOUCH) && !(f_before[i] & SH2Z_VEH_WALL)) ++wall_inc;   /* as the reference: nobody near */
            sh2z_line_sample_t at; sh2z_line_sample(&L, v->t.node, v->t.dist, &at);
            const int tb = P.surface && surface.wrap_mask && (at.flags & surface.wrap_mask) == surface.wrap_value;
            if (!tb && t - start_tick > 300) { ++road_ticks; if (at.half_width - (v->t.lateral < 0 ? -v->t.lateral : v->t.lateral) < U(30)) ++edge_ticks; }
        }
        int32_t lead = INT32_MIN, last = INT32_MAX; int changed = 0;
        for (int i = 0; i < count; ++i) {
            const sh2z_vehicle_t* v = &veh[i];
            sh2z_line_sample_t at;
            sh2z_line_sample(&L, v->t.node, v->t.dist, &at);
            const int32_t room = (P.surface && surface.wrap_mask && (at.flags & surface.wrap_mask) == surface.wrap_value)
                               ? (int32_t)(((int64_t)at.half_width * 411775) >> 17) + 1 : at.half_width;   /* a tube: half its round */
            if (!(v->flags & SH2Z_VEH_EXTERNAL)) {
                /* The accepted E2 exact predicate (offtrack_ticks) and its C-100 partition. */
                const int oob = v->t.lateral > room || v->t.lateral < -room;
                const int cls = ((v->flags & SH2Z_VEH_FALLING) || brain[i].fall_tick >= 0) ? 2 : (v->flags & SH2Z_VEH_AIR) ? 1 : 0;
                ++ot_elig[cls];
                if (oob) { ++ot_oob[cls]; ++offtrack; }
                /* Geometry trace (the library steps an agent when it sets brain.last_tick to the field tick). */
                if (use_behave) {
                    const sh2z_field_behave_t* bh = &behave;
                    const int fin = (f_before[i] & (SH2Z_VEH_FINISHED | SH2Z_VEH_RETIRED)) != 0;
                    if (brain[i].last_tick == F.tick && !(f_before[i] & SH2Z_VEH_FALLING) && !(f_before[i] & SH2Z_VEH_STOPPED)) {
                        sh2z_line_sample_t pre;
                        sh2z_line_sample(&L, pre_node[i], pre_dist[i], &pre);
                        const int pre_tube = P.surface && surface.wrap_mask && (pre.flags & surface.wrap_mask) == surface.wrap_value;
                        step_node_flags[i] = (uint16_t)((((pre.flags & bh->air_mask) == bh->air_value) && !pre_tube && !fin) ? 1u : 0u);
                        stepped_any[i] = 1;
                        if (step_node_flags[i] && !(v->flags & SH2Z_VEH_FALLING)) {
                            const int now_tube = P.surface && surface.wrap_mask && (at.flags & surface.wrap_mask) == surface.wrap_value && at.half_width > 0;
                            const int32_t al = v->t.lateral < 0 ? -v->t.lateral : v->t.lateral;
                            if (!now_tube && (at.flags & bh->open_mask) == bh->open_value && al > at.half_width + bh->fall_margin) ++geo_suppressed;
                        }
                    }
                    const int geo_air = stepped_any[i] && step_node_flags[i];
                    if ((v->flags & SH2Z_VEH_AIR) && !geo_air) ++geo_unexplained;
                    if (geo_air && !(v->flags & (SH2Z_VEH_AIR | SH2Z_VEH_FALLING | SH2Z_VEH_RETIRED | SH2Z_VEH_FINISHED))) ++geo_missing;
                    /* Episodes: AIR runs (landed or fell) and falls (retired or still falling at the end). */
                    if (v->flags & SH2Z_VEH_AIR) { if (!air_run[i]) { ++air_eps; air_out[i] = 0; } ++air_run[i]; ++air_ticks; if (oob) air_out[i] = 1; }
                    else if (air_run[i]) {
                        if (v->flags & SH2Z_VEH_FALLING) ++air_fell; else ++air_landed;
                        if (air_run[i] > air_max) air_max = air_run[i];
                        air_out_eps += air_out[i];
                        air_run[i] = 0;
                    }
                    if ((v->flags & SH2Z_VEH_FALLING) && !(f_before[i] & SH2Z_VEH_FALLING)) { ++fall_eps; fall_start_t[i] = t; if ((at.flags & 0x0200u) || (pre_flags_drop[i])) ++fall_drop; }
                    if ((v->flags & SH2Z_VEH_RETIRED) && (f_before[i] & SH2Z_VEH_FALLING)) {
                        ++fall_retired; if (t - fall_start_t[i] > fall_max) fall_max = t - fall_start_t[i];
                    }
                }
            }
            if (tube_angles && !(v->flags & (SH2Z_VEH_EXTERNAL | SH2Z_VEH_FINISHED | SH2Z_VEH_RETIRED)) &&
                ((at.flags & 0x01c0u) == 0x0080u || (at.flags & 0x01c0u) == 0x00c0u) && at.half_width) {
                /* the lateral is an arc of radius half_width round the line (the engine's tube pose) */
                double a = fabs((double)v->t.lateral) / at.half_width * 57.29578;   /* both Q8 */
                while (a > 180.0) a = fabs(a - 360.0);
                if (tube_clamp > 0 && a > tube_clamp) a = tube_clamp;
                ++tube_bin[a < 30 ? 0 : a < 60 ? 1 : a < 90 ? 2 : 3]; ++tube_ticks;
            }
            if (v->rank != prev_rank[i]) { changed = 1; ++rank_swaps; }
            prev_rank[i] = v->rank;
            if (!(v->flags & (SH2Z_VEH_FINISHED | SH2Z_VEH_RETIRED))) {
                const int32_t tot = sh2z_track_total(&L, &v->t);
                if (tot > lead) lead = tot;
                if (tot < last) last = tot;
                speed_sum[i] += v->t.speed / 256.0; speed_n[i]++;
                if (!(v->flags & SH2Z_VEH_EXTERNAL) && t - start_tick > 300) {
                    slow_ticks[i] = v->t.speed < brain[i].pace / 10 ? slow_ticks[i] + 1 : 0;
                    if (slow_ticks[i] == 121) ++stuck;
                }
            }
        }
        /* No vehicle inside another after contact resolution. */
        for (int a = 0; a < count; ++a) for (int b2 = a + 1; b2 < count; ++b2) {
            if ((veh[a].flags | veh[b2].flags) & (SH2Z_VEH_STOPPED | SH2Z_VEH_RETIRED | SH2Z_VEH_EXTERNAL)) continue;
            if (brain[a].last_tick != F.tick || brain[b2].last_tick != F.tick) continue;   /* both current this tick */
            int32_t g2 = sh2z_track_near_gap(&L, &veh[a].t, &veh[b2].t); if (g2 < 0) g2 = -g2;
            int32_t dl = veh[a].t.lateral - veh[b2].t.lateral; if (dl < 0) dl = -dl;
            if (g2 < U(46) / 4 && dl < U(40) / 4) ++through;   /* a fixed box, whatever the field's own contact settings */
        }
        rank_change_ticks += changed;
        if (lead > last) { const double s = (lead - last) / 256.0; spread_sum += s; ++spread_n; if (s > spread_max) spread_max = s; }
        if ((t - start_tick) % 600 == 599 && marks < 32) spread_at[marks++] = lead > last ? (lead - last) / 256.0 : 0;
        if (F.finished + F.retired >= (uint32_t)count) break;
        if (trace && t >= trace_ticks + 3000) break;
    }
    if (hf) fclose(hf);
    if (p0f) fclose(p0f);
    for (int i = 0; i < count; ++i) if (veh[i].flags & SH2Z_VEH_FALLING) ++fall_open;
    FILE* o = fopen(report, "w");
    if (!o) die("cannot write report");
    const int race_ticks = ticks - start_tick;
    fprintf(o, "{\n \"tier\": \"HOST\", \"role\": \"native field (SH2ZAM line-field-1)\", \"seed\": %u, \"lod\": %d, \"control\": \"%s\", \"external_player\": %d,\n", seed, lod, control, trace ? 1 : 0);
    fprintf(o, " \"ticks\": %d, \"race_start_tick\": %d, \"course_length\": %.3f, \"finished\": %u, \"retired\": %u, \"contacts\": %u,\n", ticks, start_tick, length / 256.0, (unsigned)F.finished, (unsigned)F.retired, (unsigned)F.contacts);
    fprintf(o, " \"rank_change_ticks\": %d, \"rank_swaps\": %d, \"spread_mean\": %.1f, \"spread_max\": %.1f,\n", rank_change_ticks, rank_swaps / 2, spread_n ? spread_sum / spread_n : 0.0, spread_max);
    fprintf(o, " \"offtrack_ticks\": %d, \"stuck_events\": %d, \"through_ticks\": %d, \"agent_steps\": %u, \"host_ns_per_tick\": %.1f, \"final_hash\": \"%08x\",\n",
            offtrack, stuck, through, (unsigned)F.steps, race_ticks > 0 ? ns / race_ticks : 0.0, (unsigned)sh2z_field_hash(&F));
    fprintf(o, " \"spread_every_600_ticks\": [");
    for (int i = 0; i < marks; ++i) fprintf(o, "%s%.0f", i ? "," : "", spread_at[i]);
    fprintf(o, "],\n");
    if (use_behave) fprintf(o, " \"falls\": %u, \"kos\": %u,\n",(unsigned)F.falls, (unsigned)F.kos);
    /* C-100: the partition (eligible and out-of-bounds ticks per class: grounded, airborne, fallen), the revised count
     * (grounded out-of-bounds), the geometry trace and the episodes. */
    fprintf(o, " \"offtrack_split\": {\"eligible\": [%ld,%ld,%ld], \"oob\": [%ld,%ld,%ld], \"old\": %d, \"revised\": %ld},\n",
            ot_elig[0], ot_elig[1], ot_elig[2], ot_oob[0], ot_oob[1], ot_oob[2], offtrack, ot_oob[0]);
    if (use_behave) fprintf(o, " \"geometry\": {\"air_unexplained\": %ld, \"air_missing\": %ld, \"fall_suppressed\": %ld}, \"fixture\": \"%s\", \"fixture_agent\": %d,\n"
                               " \"episodes\": {\"air\": %ld, \"air_landed\": %ld, \"air_fell\": %ld, \"air_open_at_end\": %ld, \"air_ticks\": %ld, \"air_max_ticks\": %ld, \"air_out_of_bounds\": %ld,"
                               " \"falls\": %ld, \"falls_on_drop_nodes\": %ld, \"falls_retired\": %ld, \"falls_open_at_end\": %ld, \"fall_max_ticks\": %ld},\n",
                            geo_unexplained, geo_missing, geo_suppressed, fixture, fix_agent,
                            air_eps, air_landed, air_fell, air_eps - air_landed - air_fell, air_ticks, air_max, air_out_eps,
                            fall_eps, fall_drop, fall_retired, fall_open, fall_max);
    if (sources) fprintf(o, " \"damage\": {\"contact\": [%ld, %.1f], \"wall\": [%ld, %.1f], \"boost\": [%ld, %.1f], \"other\": [%ld, %.1f]}, \"wall_incidents\": %ld, \"near_edge_share\": %.4f,\n",
                         src_n[0], src_amount[0], src_n[1], src_amount[1], src_n[2], src_amount[2], src_n[3], src_amount[3], wall_inc,
                         road_ticks ? (double)edge_ticks / road_ticks : 0.0);
    if (tube_angles) fprintf(o, " \"tube_ticks\": %ld, \"tube_angle_bins\": [%ld,%ld,%ld,%ld],\n", tube_ticks, tube_bin[0], tube_bin[1], tube_bin[2], tube_bin[3]);
    if (phys_file) fprintf(o, " \"player_phys\": {\"switch_race_tick\": %d, \"ticks\": %d, \"tube_ticks\": %d, \"missing\": %d, \"ambiguous\": %d, \"scorable\": %d},\n",
                           ph_switch >= 0 ? ph_switch - start_tick : -1, ph_ticks, ph_tube, ph_missing, ph_ambiguous, ph_missing + ph_ambiguous == 0);
    fprintf(o, " \"racers\": [\n");
    for (int i = 0; i < count; ++i) {
        const sh2z_field_brain_t* b = &brain[i];
        fprintf(o, "  {\"id\": %d, \"grid\": %d, \"target_lap_ms\": %d, \"chase\": %d, \"lap_end_ticks\": [%d,%d,%d], \"finish_tick\": %d, \"final_pos\": %d,"
                   " \"retire_tick\": %d, \"mean_speed\": %.3f, \"last_energy\": %.1f}%s\n",
                i, grid[i], target_ms[i], b->chase, (int)b->lap_tick[0], (int)b->lap_tick[1], (int)b->lap_tick[2], (int)b->finish_tick,
                b->finish_tick >= 0 ? b->finish_seq + 1 : -1, (int)b->retire_tick, speed_n[i] ? speed_sum[i] / speed_n[i] : 0.0,
                veh[i].energy / 256.0, i + 1 < count ? "," : "");
    }
    fprintf(o, " ]\n}\n");
    fclose(o);
    printf("rivals-gp: seed %u ticks %d finished %u retired %u contacts %u rank-change ticks %d steps %u\n", seed, ticks, (unsigned)F.finished,
           (unsigned)F.retired, (unsigned)F.contacts, rank_change_ticks, (unsigned)F.steps);
    return 0;
}
