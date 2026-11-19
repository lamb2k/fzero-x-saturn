/* Native rival field on HOST (E2): the SH2ZAM line field against the baked course line.
 * MIT; see ../LICENSE. Host headers and SH2ZAM only: no original game code is linked.
 *
 *   fzx-rivals-gp --pack RIVALS.PAK --length Q8 --config FIELD.txt --seed N --report OUT.json
 *                 [--player-trace TRACE.bin --start-tick T] [--ticks MAX] [--lod 0|1] [--hash-every N]
 *                 [--control none|no-avoid|no-contact|stuck]
 *
 * FIELD.txt has one line per vehicle: id grid(1..) target_lap_ms chase(255 none) max_energy aggression.
 * Vehicle 0 is the player. With --player-trace it is external (float32 dist, lateral, speed per tick
 * from a reference run); without it, vehicle 0 is one more agent.
 * The report has the same shape as fzx-gp-ref's, so one script computes the bands for both. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sh2zam/line_field_experimental.h"

#define MAXV 30
#define MAXN 4096
static void die(const char* m) { fprintf(stderr, "fzx-rivals-gp: %s\n", m); exit(2); }
static sh2z_line_node_t nodes[MAXN];
static uint32_t be32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint16_t be16(const unsigned char* p) { return (uint16_t)((p[0] << 8) | p[1]); }

#define U(x) ((int32_t)((x) * 256))   /* whole units to Q8 */

int main(int argc, char** argv) {
    const char *pack = 0, *config = 0, *report = 0, *trace = 0, *control = "none";
    long length = 0; unsigned seed = 1; int max_ticks = 14000, start_tick = 420, lod = 1, hash_every = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--pack")) pack = argv[i + 1];
        else if (!strcmp(argv[i], "--length")) length = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--config")) config = argv[i + 1];
        else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--report")) report = argv[i + 1];
        else if (!strcmp(argv[i], "--player-trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--start-tick")) start_tick = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) max_ticks = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--lod")) lod = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--hash-every")) hash_every = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--control")) control = argv[i + 1];
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
    const sh2z_line_t L = { nodes, count_nodes, (int32_t)length };

    /* Tuning (Q8 units; see generated/RIVALS-NATIVE-PLAN.md for where each value comes from). */
    sh2z_field_params_t P;
    memset(&P, 0, sizeof P);
    P.pace_gain = 3277;            /* 0.05 per unit, as the source's distance term */
    P.pace_max = 81920;            /* 1.25 x pace */
    P.chase_gain = 655;            /* 0.01 per unit, as the source's chase term */
    P.steer_gain = 3277;
    P.margin = U(6);
    P.avoid_long = U(92); P.avoid_lat = U(69); P.avoid_offset = U(92);   /* the source's 92 / 69 / 92 */
    P.pass_hold = 60;
    P.contact_long = U(46); P.contact_lat = U(40);
    P.bump_keep = 16384;
    P.hit_energy = 32768; P.side_energy = U(2); P.attack_energy = U(10);
    P.wall_energy = U(3); P.wall_keep = 62259;                           /* keep 95% */
    P.attack_long = U(184); P.attack_lat = U(92);                        /* the source's 184 / 92 */
    P.boost_speed = U(6); P.boost_energy = U(10); P.boost_min_share = 19661; P.boost_min_cap = 0; P.boost_behind = U(150);
    P.cruise = U(40);
    P.lod_near[0] = lod ? U(3000) : INT32_MAX; P.lod_near[1] = lod ? U(8000) : INT32_MAX; P.lod_near[2] = lod ? U(16000) : INT32_MAX;
    P.boost_ticks = 90; P.laps = 3; P.boost_from_lap = 1;
    if (!strcmp(control, "no-avoid")) { P.avoid_long = 0; }
    if (!strcmp(control, "no-contact")) { P.contact_long = 0; }

    static sh2z_vehicle_t veh[MAXV];
    static sh2z_field_brain_t brain[MAXV];
    static uint8_t order[MAXV], slot[MAXV], rank_order[MAXV];
    static int grid[MAXV], target_ms[MAXV];
    memset(veh, 0, sizeof veh); memset(brain, 0, sizeof brain);
    f = fopen(config, "r");
    if (!f) die("cannot open config");
    int count = 0, id, g, ms, chase, aggr; double maxe;
    uint32_t rs = seed * 2654435761u + 12345u;   /* set-up variation, separate from the field's generator */
    #define RND() (rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5, rs)
    while (fscanf(f, "%d %d %d %d %lf %d", &id, &g, &ms, &chase, &maxe, &aggr) == 6) {
        if (id != count || count >= MAXV) die("config ids must run 0..n-1");
        sh2z_vehicle_t* v = &veh[id]; sh2z_field_brain_t* b = &brain[id];
        sh2z_field_grid_slot(&L, (uint32_t)(g - 1), U(146), U(100), U(50), 4, &v->t);   /* the source grid: 146 back, 100 per place, 4 columns of 100 */
        grid[id] = g; target_ms[id] = ms;
        v->max_energy = v->energy = (int32_t)(maxe * 256);
        v->accel = 40; v->brake = 96; v->top_speed = U(60); v->grip = U(3);
        v->armor = (uint16_t)(256 * 178 / (maxe > 1 ? maxe : 178));   /* lighter bodies take more damage */
        v->flags = 0;
        const int lap_ticks = (ms > 0 ? ms : 36000) * 60 / 1000;
        b->pace = (int32_t)(length / lap_ticks);
        b->lane = (int32_t)((int)(RND() % 61) - 30) * 256;
        b->chase = (int16_t)(chase == 255 ? -1 : chase);
        b->chase_lead = (int16_t)(chase == 0 ? (int)(RND() % 400) - 10 : 30);   /* the source's offset ranges */
        b->aggression = (uint8_t)aggr;
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
    sh2z_field_t F;
    sh2z_field_init(&F, &L, &P, veh, brain, order, slot, rank_order, (uint32_t)count, seed);
    F.focus = 0;

    static int prev_rank[MAXV];
    int rank_change_ticks = 0, rank_swaps = 0, ticks = 0, offtrack = 0, stuck = 0, through = 0;
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
            veh[0].t.node = sh2z_line_locate(&L, veh[0].t.dist, veh[0].t.node);
            veh[0].kick_lat = veh[0].kick_speed = 0;
            veh[0].energy = veh[0].max_energy;   /* the trace carries its own outcome */
        }
        if (t == start_tick) sh2z_field_start(&F);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        sh2z_field_step(&F);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (F.started) ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
        ticks = t + 1;
        if (hf && F.started && (t - start_tick) % hash_every == 0) fprintf(hf, "%d %08x\n", t - start_tick, (unsigned)sh2z_field_hash(&F));
        if (!F.started) continue;
        int32_t lead = INT32_MIN, last = INT32_MAX; int changed = 0;
        for (int i = 0; i < count; ++i) {
            const sh2z_vehicle_t* v = &veh[i];
            sh2z_line_sample_t at;
            sh2z_line_sample(&L, v->t.node, v->t.dist, &at);
            if (!(v->flags & SH2Z_VEH_EXTERNAL) && (v->t.lateral > at.half_width || v->t.lateral < -at.half_width)) ++offtrack;
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
            if ((veh[a].flags | veh[b2].flags) & (SH2Z_VEH_STOPPED | SH2Z_VEH_EXTERNAL)) continue;
            int32_t g2 = sh2z_track_near_gap(&L, &veh[a].t, &veh[b2].t); if (g2 < 0) g2 = -g2;
            int32_t dl = veh[a].t.lateral - veh[b2].t.lateral; if (dl < 0) dl = -dl;
            if (g2 < P.contact_long / 4 && dl < P.contact_lat / 4 && P.contact_long) ++through;
        }
        rank_change_ticks += changed;
        if (lead > last) { const double s = (lead - last) / 256.0; spread_sum += s; ++spread_n; if (s > spread_max) spread_max = s; }
        if ((t - start_tick) % 600 == 599 && marks < 32) spread_at[marks++] = lead > last ? (lead - last) / 256.0 : 0;
        if (F.finished + F.retired >= (uint32_t)count) break;
        if (trace && t >= trace_ticks + 3000) break;
    }
    if (hf) fclose(hf);
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
    fprintf(o, "],\n \"racers\": [\n");
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
