/* Rival line bake and HOST rival model check (diagnostic, claude/rivals-native).
 * MIT; see ../LICENSE. Host headers only; the game code is behind ref.h and rivals_bake.c.
 *
 *   fzx-rivals --course RECORD --rom ROM --cpu-line LINE --inputs-hex WORDS --pace BASE,STEP
 *              --header OUT.h --report OUT.json [--rivals N]
 *
 * RECORD, ROM and LINE come from the owner's ROM at asset-build time (tools/rivals_assets.py) and are
 * never committed. The player is replayed through the original race code; rivals run the native
 * fixed-point model (SH2ZAM line follower) against the recorded player. Outputs: a generated header
 * for the Saturn diagnostic (line nodes and per-node world poses) and a JSON report of the play checks. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ref.h"
#include "sh2zam/line_follower_experimental.h"

int rb_segment_count(void);
float rb_course_length(void);
void rb_segment(int index, float out[5]);
void rb_sample(int index, float t, float out[13]);
void rb_player(float out[3]);

#define MAX_NODES 4096
#define MAX_TICKS 16384
#define MAX_RIVALS 29
#define STEP_UNITS 64.0   /* bake spacing along the course, whole units */

static void die(const char* m) { fprintf(stderr, "fzx-rivals: %s\n", m); exit(2); }

typedef struct { int32_t pos[3]; int16_t right[3], up[3]; uint16_t yaw, pitch, roll, pad; } Pose;
static sh2z_line_node_t nodes[MAX_NODES];
static Pose poses[MAX_NODES];
static double node_dist[MAX_NODES];
static int node_seg[MAX_NODES];
static double node_t[MAX_NODES];
static int node_count;
static int16_t cpu_line[0x200];

typedef struct { float dist, lateral, speed; int race_time; } PlayerTick;
static PlayerTick player[MAX_TICKS];

static uint16_t ang16(double rad) { double t = rad / (2.0 * M_PI); t -= floor(t); return (uint16_t)lround(t * 65536.0) ; }
static int16_t q14(double v) { long r = lround(v * 16384.0); if (r > 32767) r = 32767; if (r < -32768) r = -32768; return (int16_t)r; }

static unsigned char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) die("cannot open input");
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = malloc(*n ? *n : 1);
    if (!b || fread(b, 1, *n, f) != *n) die("short read");
    fclose(f);
    return b;
}

/* Bake nodes every STEP_UNITS along every segment, plus each segment start. */
static void bake(double speed_k) {
    const int segs = rb_segment_count();
    const double length = rb_course_length();
    float s[5], p[13];
    node_count = 0;
    for (int i = 0; i < segs; ++i) {
        rb_segment(i, s);
        const int n = (int)ceil(s[1] / STEP_UNITS);
        for (int k = 0; k < n; ++k) {
            const double t = (double)k / n;
            rb_sample(i, (float)t, p);
            double d = k == 0 ? s[0] : p[12];
            if (node_count && d <= node_dist[node_count - 1] + 1.0 / 128) continue; /* keep starts strictly increasing */
            if (node_count == MAX_NODES) die("too many nodes");
            node_dist[node_count] = d; node_seg[node_count] = i; node_t[node_count] = t;
            sh2z_line_node_t* nd = &nodes[node_count];
            Pose* ps = &poses[node_count];
            const int half = t >= 0.5 ? 2 : 0;
            const int16_t cap = cpu_line[(i * 4 + half) & 0x1ff], lat = cpu_line[(i * 4 + half + 1) & 0x1ff];
            nd->start = (int32_t)lround(d * 256.0);
            nd->lateral = lat;
            const double capq4 = cap > 0 ? cap * speed_k * 256.0 / 16.0 : 65535.0;   /* Q8 per tick -> Q4 */
            nd->speed_cap = (uint16_t)(capq4 > 65535.0 ? 65535.0 : capq4);
            const double hw = fmin(s[2], s[3]);
            nd->half_width = (uint16_t)(hw > 0 ? (hw > 65535 ? 65535 : hw) : 0);
            nd->flags = (uint16_t)s[4];
            for (int a = 0; a < 3; ++a) {
                ps->pos[a] = (int32_t)lround(p[a] * 16.0);
                ps->right[a] = q14(-p[9 + a]);
                ps->up[a] = q14(p[6 + a]);
            }
            ps->yaw = ang16(atan2(p[3], p[5]));
            ps->pitch = ang16(atan2(p[4], hypot(p[3], p[5])));
            ps->roll = ang16(atan2(p[10], p[7]));   /* + = left side higher */
            ps->pad = 0;
            node_count++;
        }
    }
    if (node_count < 2 || nodes[0].start != 0) die("bake must start at distance 0");
    const int32_t len_q8 = (int32_t)lround(length * 256.0);
    for (int i = 0; i < node_count; ++i) {
        const int32_t next = i + 1 < node_count ? nodes[i + 1].start : len_q8;
        const int32_t span = next - nodes[i].start;
        if (span < 2 || span > 65536) die("node span out of range");
        nodes[i].inv_span = sh2z_line_node_inv_span(span);
    }
}

static void put32(FILE* f, uint32_t v) { fputc((int)(v >> 24), f); fputc((int)(v >> 16) & 255, f); fputc((int)(v >> 8) & 255, f); fputc((int)v & 255, f); }
static void put16(FILE* f, uint16_t v) { fputc(v >> 8, f); fputc(v & 255, f); }

/* RIVALS.PAK (big-endian, the SH-2's layouts): node_count x 16-byte sh2z_line_node_t, then
 * node_count x 32-byte fzx_rival_node_pose_t. The header carries sizes, hash and lap targets. */
static void write_pack(const char* header_path, int32_t len_q8, const int32_t* lap_ticks, int nrivals, const char* pace) {
    char pak[4096];
    snprintf(pak, sizeof pak, "%.*sRIVALS.PAK", (int)(strrchr(header_path, '/') ? strrchr(header_path, '/') - header_path + 1 : 0), header_path);
    FILE* f = fopen(pak, "wb");
    if (!f) die("cannot write pack");
    for (int i = 0; i < node_count; ++i) {
        put32(f, (uint32_t)nodes[i].start); put16(f, (uint16_t)nodes[i].lateral); put16(f, nodes[i].speed_cap);
        put16(f, nodes[i].half_width); put16(f, nodes[i].flags); put32(f, nodes[i].inv_span);
    }
    for (int i = 0; i < node_count; ++i) {
        const Pose* p = &poses[i];
        for (int a = 0; a < 3; ++a) put32(f, (uint32_t)p->pos[a]);
        for (int a = 0; a < 3; ++a) put16(f, (uint16_t)p->right[a]);
        for (int a = 0; a < 3; ++a) put16(f, (uint16_t)p->up[a]);
        put16(f, p->yaw); put16(f, p->pitch); put16(f, p->roll); put16(f, 0);
    }
    fclose(f);
    size_t n;
    unsigned char* b = read_file(pak, &n);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    free(b);
    FILE* o = fopen(header_path, "w");
    if (!o) die("cannot write header");
    fprintf(o, "/* Generated by ref/rivals_main.c from the owner's ROM (course record and CPU line);"
               " regenerate, never edit, never commit. */
#ifndef FZX_RIVALS_COURSE_H
#define FZX_RIVALS_COURSE_H
");
    fprintf(o, "#define FZX_RIVALS_LINE_LENGTH %ld
#define FZX_RIVALS_NODE_COUNT %du
#define FZX_RIVALS_PACK_BYTES %luu
"
               "#define FZX_RIVALS_PACK_HASH 0x%08lxu
#define FZX_RIVALS_PACE \"%s\"
",
            (long)len_q8, node_count, (unsigned long)n, (unsigned long)h, pace);
    fprintf(o, "static const int32_t fzx_rivals_lap_ticks[%d] = {", nrivals);
    for (int i = 0; i < nrivals; ++i) fprintf(o, "%ld,", (long)lap_ticks[i]);
    fprintf(o, "};
#endif
");
    fclose(o);
}

int main(int argc, char** argv) {
    const char *course = 0, *rom = 0, *line = 0, *hex = 0, *header = 0, *report = 0, *pace = "33000,300";
    int nriv = MAX_RIVALS;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--course")) course = argv[i + 1];
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--cpu-line")) line = argv[i + 1];
        else if (!strcmp(argv[i], "--inputs-hex")) hex = argv[i + 1];
        else if (!strcmp(argv[i], "--header")) header = argv[i + 1];
        else if (!strcmp(argv[i], "--report")) report = argv[i + 1];
        else if (!strcmp(argv[i], "--pace")) pace = argv[i + 1];
        else if (!strcmp(argv[i], "--rivals")) nriv = atoi(argv[i + 1]);
        else die("unknown argument");
    }
    if (!course || !rom || !line || !hex || !header || !report || nriv < 1 || nriv > MAX_RIVALS) die("usage: see rivals_main.c");
    size_t n;
    unsigned char* rec = read_file(course, &n);
    if (n != 0x7E0) die("course record must be 0x7E0 bytes");
    unsigned char* r = read_file(rom, &n);
    if (n < REF_ROM_BOOT_SIZE) die("short ROM");
    ref_set_rom_boot(r); /* z64 order (tools/rivals_assets.py normalizes) */
    unsigned char* lb = read_file(line, &n);
    if (n != 0x400) die("CPU line must be 0x400 bytes");
    for (int i = 0; i < 0x200; ++i) cpu_line[i] = (int16_t)((lb[2 * i] << 8) | lb[2 * i + 1]);
    long base, stepms;
    if (sscanf(pace, "%ld,%ld", &base, &stepms) != 2) die("--pace BASE,STEP (ms)");
    if (ref_load_course(rec, 1) != 0 || ref_race_init() != 0) die("race set-up failed");

    /* Replay the player once through the original code, recording line-space state. */
    FILE* f = fopen(hex, "r");
    if (!f) die("cannot open inputs");
    unsigned int w;
    int ticks = 0;
    double sum_d = 0, sum_v = 0;
    float prev = 0;
    while (ticks < MAX_TICKS && fscanf(f, "%x", &w) == 1) {
        ref_tick((unsigned short)(w >> 16), (signed char)(unsigned char)(w >> 8), (signed char)(unsigned char)w);
        float pl[3];
        RefState st;
        rb_player(pl); ref_state(&st);
        player[ticks].dist = pl[0]; player[ticks].lateral = pl[1]; player[ticks].speed = pl[2];
        player[ticks].race_time = st.race_time;
        if (ticks && st.race_time > 0 && pl[2] > 1.0f && pl[0] - prev > 0 && pl[0] - prev < 400) { sum_d += pl[0] - prev; sum_v += pl[2]; }
        prev = pl[0];
        ticks++;
    }
    fclose(f);
    if (sum_v <= 0) die("player never moved: cannot calibrate speed units");
    const double speed_k = sum_d / sum_v;   /* distance per tick per speed unit */
    bake(speed_k);
    const double length = rb_course_length();
    const int32_t len_q8 = (int32_t)lround(length * 256.0);
    const sh2z_line_t L = { nodes, (uint32_t)node_count, len_q8 };

    /* Rival model (POC-1 crude follower plus the pace controller). */
    static sh2z_track_state_t rv[MAX_RIVALS];
    static int32_t lap_ticks[MAX_RIVALS], expect[MAX_RIVALS], lane[MAX_RIVALS], pace_v[MAX_RIVALS];
    static int lap_done_tick[MAX_RIVALS][4];
    const sh2z_follow_params_t fp = { 24, 48, 3277, 64, 6 * 256 };
    int start_tick = -1;
    for (int i = 0; i < MAX_RIVALS; ++i) lap_ticks[i] = (int32_t)((base + stepms * i) * 60 / 1000);
    for (int i = 0; i < nriv; ++i) {
        pace_v[i] = (int32_t)((int64_t)len_q8 / lap_ticks[i]);
        memset(&rv[i], 0, sizeof rv[i]);
        const int32_t back = (int32_t)((i / 2 + 1) * 40 * 256);   /* two-wide grid, 40 units apart */
        rv[i].dist = len_q8 - back; rv[i].lap = -1;
        lane[i] = (i & 1) ? 30 * 256 : -30 * 256;
        rv[i].lateral = lane[i];
        rv[i].node = sh2z_line_locate(&L, rv[i].dist, 0);
        expect[i] = -back;
        for (int k = 0; k < 4; ++k) lap_done_tick[i][k] = -1;
    }
    uint8_t order[MAX_RIVALS];
    for (int i = 0; i < nriv; ++i) order[i] = (uint8_t)i;
    int rank_changes = 0, passes_by_player = 0, passes_of_player = 0;
    int32_t max_lat_excess = 0, min_speed_racing = INT32_MAX;
    struct timespec t0, t1;
    double host_ns = 0;
    for (int t = 0; t < ticks; ++t) {
        if (start_tick < 0 && player[t].race_time > 0) start_tick = t;
        if (start_tick < 0) continue;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < nriv; ++i) {
            expect[i] += pace_v[i];
            const int32_t want = sh2z_pace_target(pace_v[i], expect[i], sh2z_track_total(&L, &rv[i]), 655, 0, pace_v[i] * 2);
            sh2z_line_sample_t at;
            sh2z_line_sample(&L, rv[i].node, rv[i].dist, &at);
            const int32_t before_lap = rv[i].lap;
            sh2z_follow_step(&L, &fp, &rv[i], want, at.lateral + lane[i], 1);
            if (rv[i].lap != before_lap && rv[i].lap >= 1 && rv[i].lap <= 4) lap_done_tick[i][rv[i].lap - 1] = t - start_tick;
        }
        uint8_t prev_order[MAX_RIVALS];
        memcpy(prev_order, order, (size_t)nriv);
        sh2z_rank_sort(order, &L, rv, (uint32_t)nriv);
        rank_changes += memcmp(prev_order, order, (size_t)nriv) != 0;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        host_ns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
        /* Checks against the replayed player (line space). */
        const int32_t pt = (int32_t)lround(player[t].dist * 256.0);
        const int32_t ppt = t ? (int32_t)lround(player[t - 1].dist * 256.0) : pt;
        for (int i = 0; i < nriv; ++i) {
            sh2z_line_sample_t at;
            sh2z_line_sample(&L, rv[i].node, rv[i].dist, &at);
            const int32_t ex = abs(rv[i].lateral) - at.half_width;
            if (ex > max_lat_excess) max_lat_excess = ex;
            const int32_t tot = sh2z_track_total(&L, &rv[i]);
            if (t - start_tick > 600 && rv[i].lap < 3 && rv[i].speed < min_speed_racing) min_speed_racing = rv[i].speed;
            static int32_t prev_tot[MAX_RIVALS];
            if (t > start_tick) {
                if (prev_tot[i] > ppt && tot <= pt) passes_by_player++;
                if (prev_tot[i] <= ppt && tot > pt) passes_of_player++;
            }
            prev_tot[i] = tot;
        }
    }
    FILE* o = fopen(report, "w");
    if (!o) die("cannot write report");
    fprintf(o, "{\n \"tier\": \"HOST\", \"rivals\": %d, \"ticks\": %d, \"race_start_tick\": %d,\n", nriv, ticks, start_tick);
    fprintf(o, " \"course_length\": %.3f, \"nodes\": %d, \"speed_units_to_distance_per_tick\": %.6f,\n", length, node_count, speed_k);
    fprintf(o, " \"host_ns_per_tick\": %.1f, \"rank_changes_ticks\": %d, \"player_passes_rivals\": %d, \"rivals_pass_player\": %d,\n",
            host_ns / (ticks - start_tick), rank_changes, passes_by_player, passes_of_player);
    fprintf(o, " \"max_lateral_beyond_half_width_q8\": %ld, \"min_speed_while_racing_q8\": %ld,\n", (long)max_lat_excess,
            (long)(min_speed_racing == INT32_MAX ? -1 : min_speed_racing));
    fprintf(o, " \"rivals_detail\": [\n");
    for (int i = 0; i < nriv; ++i) {
        fprintf(o, "  {\"id\": %d, \"target_lap_ticks\": %ld, \"lap_end_ticks\": [%d,%d,%d], \"final_lap\": %ld}%s\n", i,
                (long)lap_ticks[i], lap_done_tick[i][0], lap_done_tick[i][1], lap_done_tick[i][2], (long)rv[i].lap,
                i + 1 < nriv ? "," : "");
    }
    fprintf(o, " ]\n}\n");
    fclose(o);
    write_pack(header, len_q8, lap_ticks, MAX_RIVALS, pace);
    printf("nodes %d length %.1f k %.6f host %.1f ns/tick\n", node_count, length, speed_k, host_ns / (ticks - start_tick));
    return 0;
}
