/* Rival line bake and HOST rival model check (diagnostic, claude/rivals-native).
 * MIT; see ../LICENSE. Host headers only; the game code is behind ref.h and rivals_bake.c.
 *
 *   fzx-rivals --course RECORD --rom ROM --cpu-line LINE --inputs-hex WORDS --pace BASE,STEP
 *              --header OUT.h --report OUT.json [--rivals N] [--zones OUT.txt]
 *
 * --zones writes the course's surface zones in line space for the native field (SH2ZAM line-field-4),
 * after everything else, so the pack, header and report are as without it: one row per zone,
 * "start end lat_lo lat_hi kind" (Q8 distance and lateral, positive right; kind 1 plate, 2 low grip,
 * 3 rough, 4 recharge), sorted by start, a zone over the line's start split in two.
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
#include "course_line.h"

int rb_segment_count(void);
void rb_segment(int index, float out[5]);
void rb_sample(int index, float t, float out[13]);
void rb_player(float out[3]);
void rb_gp_zones(void);
int rb_zones(float out[][11], int max);

#define MAX_NODES 4096
#define MAX_TICKS 16384
#define MAX_RIVALS 29

static void die(const char* m) { fprintf(stderr, "fzx-rivals: %s\n", m); exit(2); }

typedef struct { int32_t pos[3]; int16_t fwd[3], up[3], left[3]; uint16_t pad; } Pose;   /* 32 bytes */
static sh2z_line_node_t nodes[MAX_NODES];
static Pose poses[MAX_NODES];
static double node_dist[MAX_NODES];
static int node_seg[MAX_NODES];
static double node_t[MAX_NODES];
static int node_count;
static CourseLineStation stations[MAX_NODES];
static int line_len_q12;           /* course-line-1 lap length, Q12 */
static unsigned long long line_hash;  /* course-line-1 agreement hash, from this bake */
static int16_t cpu_line[0x200];
static double handed_sum;   /* sum over nodes of (forward x up) . left */

typedef struct { float dist, lateral, speed; int race_time; } PlayerTick;
static PlayerTick player[MAX_TICKS];

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

/* Bake one node per course-line-1 station (ref/course_line.c, shared with E1): the stations,
 * their Q12 distances and the one Q12-to-Q8 rule come from there; this adds the rival payload. */
static void bake(double speed_k) {
    float s[5], p[13];
    static int seg_start_q12[MAX_NODES];
    const int segs = rb_segment_count();
    node_count = course_line_stations(stations, MAX_NODES, &line_len_q12, COURSE_LINE_SPACING);
    if (node_count < 2) die("course-line-1 baker failed (too many stations or a gap out of bounds)");
    if (segs > MAX_NODES) die("too many segments");
    for (int i = 0; i < segs; ++i) seg_start_q12[i] = -1;
    for (int j = 0; j < node_count; ++j) {
        const CourseLineStation* st = &stations[j];
        const int i = st->segment;
        const double t = st->t;
        if (st->t_q16 == 0) seg_start_q12[i] = st->dist_q12;
        rb_segment(i, s);
        rb_sample(i, st->t, p);
        node_dist[j] = st->dist_q12 / 4096.0; node_seg[j] = i; node_t[j] = t;
        sh2z_line_node_t* nd = &nodes[j];
        Pose* ps = &poses[j];
        const int half = t >= 0.5 ? 2 : 0;
        const int16_t cap = cpu_line[(i * 4 + half) & 0x1ff], lat = cpu_line[(i * 4 + half + 1) & 0x1ff];
        nd->start = (int32_t)course_line_q8(st->dist_q12);
        nd->lateral = lat;
        const double capq4 = cap > 0 ? cap * speed_k * 256.0 / 16.0 : 65535.0;   /* Q8 per tick -> Q4 */
        nd->speed_cap = (uint16_t)(capq4 > 65535.0 ? 65535.0 : capq4);
        /* The road's half width at this station as the source works it out for a racer (racer.c currentRadiusLeft and
         * Right): the segment's radius plus t of the way to the next segment's. The smaller side. */
        float sn[5];
        rb_segment(i + 1 < segs ? i + 1 : 0, sn);
        const double hw = fmin(s[2] + t * (sn[2] - s[2]), s[3] + t * (sn[3] - s[3]));
        nd->half_width = (uint16_t)(hw > 0 ? (hw > 65535 ? 65535 : hw) : 0);
        nd->flags = (uint16_t)s[4];
        /* Basis rows as the source's: x forward, y up, z side (positive left), unit length. */
        const double fl = sqrt(p[3]*p[3]+p[4]*p[4]+p[5]*p[5]), ul = sqrt(p[6]*p[6]+p[7]*p[7]+p[8]*p[8]);
        const double ll = sqrt(p[9]*p[9]+p[10]*p[10]+p[11]*p[11]);
        if (fl <= 0 || ul <= 0 || ll <= 0) die("degenerate course basis");
        for (int a = 0; a < 3; ++a) {
            ps->pos[a] = (int32_t)lround(p[a] * 16.0);
            ps->fwd[a] = q14(p[3 + a] / fl);
            ps->up[a] = q14(p[6 + a] / ul);
            ps->left[a] = q14(p[9 + a] / ll);
        }
        /* Handedness: (forward x up) . left, summed (recorded in the report). */
        const double cx = (p[4]*p[8]-p[5]*p[7])/(fl*ul), cy = (p[5]*p[6]-p[3]*p[8])/(fl*ul), cz = (p[3]*p[7]-p[4]*p[6])/(fl*ul);
        handed_sum += (cx*p[9]+cy*p[10]+cz*p[11])/ll;
        ps->pad = 0;
    }
    if (nodes[0].start != 0) die("bake must start at distance 0");
    /* Agreement hash from this bake's own stations (each segment start must be a station), which
     * must equal the course's: then E1's physics pack and RIVALS.PAK index the same line. */
    for (int i = 0; i < segs; ++i) if (seg_start_q12[i] < 0) die("a segment start is not a station");
    line_hash = course_line_hash(line_len_q12, seg_start_q12, segs);
    if (line_hash != course_line_course_hash()) die("course-line-1 agreement hash mismatch");
    const int32_t len_q8 = (int32_t)course_line_q8(line_len_q12);
    for (int i = 0; i < node_count; ++i) {
        const int32_t next = i + 1 < node_count ? nodes[i + 1].start : len_q8;
        const int32_t span = next - nodes[i].start;
        if (span < 2 || span > 65536) die("node span out of range");
        nodes[i].inv_span = sh2z_line_node_inv_span(span);
    }
}

/* Surface zones in line space (--zones). The source tests a zone by t range on its segment, then by
 * side offset (left of the road middle, shifted by half the radius difference) or by two edge
 * vectors (the displacement between them about the road forward). The lateral range here is the
 * band of on-road laterals that pass, taken at the zone's middle t. */
#define MAX_ZONES 256
typedef struct { int32_t start, end, lo, hi; int kind; } LineZone;
static int zone_cmp(const void* a, const void* b) {
    const LineZone *x = a, *y = b;
    return x->start < y->start ? -1 : x->start > y->start ? 1 : 0;
}
static void write_zones(const char* path, int32_t len_q8) {
    static float z[MAX_ZONES][11];
    static LineZone out[2 * MAX_ZONES];
    static const int kind_map[5] = { 0, 4, 3, 1, 2 };   /* source pit, dirt, dash, ice -> field recharge, rough, plate, low grip */
    rb_gp_zones();
    const int n = rb_zones(z, MAX_ZONES);
    if (n > MAX_ZONES) die("too many zones");
    int m = 0;
    for (int k = 0; k < n; ++k) {
        const int seg = (int)z[k][0], type = (int)z[k][1];
        if (type < 1 || type > 4) continue;   /* lava and magnets: not in the field */
        float s5[5], p0[13], p1[13], pm[13];
        rb_segment(seg, s5);
        rb_sample(seg, z[k][2], p0); rb_sample(seg, z[k][3], p1); rb_sample(seg, 0.5f * (z[k][2] + z[k][3]), pm);
        double lo = 1e9, hi = -1e9;
        if (z[k][10] != 0.0f) {
            const double shift = 0.5 * (s5[3] - s5[2]);   /* (right - left) / 2 */
            lo = shift - z[k][7]; hi = shift - z[k][4];
        } else {
            const double fl = sqrt(pm[3]*pm[3]+pm[4]*pm[4]+pm[5]*pm[5]), ll = sqrt(pm[9]*pm[9]+pm[10]*pm[10]+pm[11]*pm[11]);
            const double f[3] = { pm[3] / fl, pm[4] / fl, pm[5] / fl }, l[3] = { pm[9] / ll, pm[10] / ll, pm[11] / ll };
            const double lv[3] = { z[k][4], z[k][5], z[k][6] }, hv[3] = { z[k][7], z[k][8], z[k][9] };
            const double reach = fmax(s5[2], s5[3]);
            for (double lat = -reach; lat <= reach; lat += 0.25) {
                const double d[3] = { -lat * l[0], -lat * l[1], -lat * l[2] };
                const double c1[3] = { d[1]*lv[2]-d[2]*lv[1], d[2]*lv[0]-d[0]*lv[2], d[0]*lv[1]-d[1]*lv[0] };
                const double c2[3] = { hv[1]*d[2]-hv[2]*d[1], hv[2]*d[0]-hv[0]*d[2], hv[0]*d[1]-hv[1]*d[0] };
                if (c1[0]*f[0]+c1[1]*f[1]+c1[2]*f[2] < 0 || c2[0]*f[0]+c2[1]*f[1]+c2[2]*f[2] < 0) continue;
                if (lat < lo) lo = lat;
                if (lat > hi) hi = lat;
            }
            if (lo > hi) continue;   /* no on-road lateral inside */
        }
        int32_t a0 = (int32_t)lround(p0[12] * 256.0), a1 = (int32_t)lround(p1[12] * 256.0);
        while (a0 >= len_q8) a0 -= len_q8;
        while (a0 < 0) a0 += len_q8;
        while (a1 >= len_q8) a1 -= len_q8;
        while (a1 < 0) a1 += len_q8;
        const LineZone base = { 0, 0, (int32_t)lround(lo * 256.0), (int32_t)lround(hi * 256.0), kind_map[type] };
        if (a1 > a0) { out[m] = base; out[m].start = a0; out[m].end = a1; ++m; }
        else if (a1 < a0) {
            out[m] = base; out[m].start = a0; out[m].end = len_q8; ++m;
            if (a1 > 0) { out[m] = base; out[m].start = 0; out[m].end = a1; ++m; }
        }
    }
    qsort(out, (size_t)m, sizeof out[0], zone_cmp);
    FILE* o = fopen(path, "w");
    if (!o) die("cannot write zones");
    for (int k = 0; k < m; ++k) fprintf(o, "%ld %ld %ld %ld %d\n", (long)out[k].start, (long)out[k].end, (long)out[k].lo, (long)out[k].hi, out[k].kind);
    fclose(o);
    printf("zones %d (of %d source effects)\n", m, n);
    /* The final dash (cpu.c: on the last lap, past segment index (2 x count) / 3, the CPU racers stop
     * pacing): the Q8 line distance where that stretch starts, written beside the zones as PATH.dash. */
    {
        float q[13];
        const int first = (2 * rb_segment_count()) / 3 + 1;
        char dp[4096];
        snprintf(dp, sizeof dp, "%s.dash", path);
        FILE* d = fopen(dp, "w");
        if (!d) die("cannot write the dash distance");
        if (first < rb_segment_count()) { rb_sample(first, 0.0f, q); fprintf(d, "%ld\n", (long)lround(q[12] * 256.0)); }
        else fprintf(d, "0\n");
        fclose(d);
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
        for (int a = 0; a < 3; ++a) put16(f, (uint16_t)p->fwd[a]);
        for (int a = 0; a < 3; ++a) put16(f, (uint16_t)p->up[a]);
        for (int a = 0; a < 3; ++a) put16(f, (uint16_t)p->left[a]);
        put16(f, 0);
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
               " regenerate, never edit, never commit. */\n#ifndef FZX_RIVALS_COURSE_H\n#define FZX_RIVALS_COURSE_H\n");
    fprintf(o, "#define FZX_RIVALS_LINE_LENGTH %ld\n#define FZX_RIVALS_NODE_COUNT %du\n#define FZX_RIVALS_PACK_BYTES %luu\n"
               "#define FZX_RIVALS_PACK_HASH 0x%08lxu\n#define FZX_RIVALS_PACE \"%s\"\n",
            (long)len_q8, node_count, (unsigned long)n, (unsigned long)h, pace);
    fprintf(o, "#define FZX_RIVALS_COURSE_LINE \"course-line-1\"\n#define FZX_RIVALS_COURSE_LINE_HASH 0x%016llxull"
               " /* = E1 physics pack receipt course_line_hash */\n", line_hash);
    const double hand = handed_sum / node_count;
    if (fabs(fabs(hand) - 1.0) > 0.01) die("course basis is not orthonormal enough to fix the side sign");
    fprintf(o, "#define FZX_RIVALS_LEFT_SIGN (%d) /* left = sign * (forward x up), from the bake */\n", hand > 0 ? 1 : -1);
    fprintf(o, "static const int32_t fzx_rivals_lap_ticks[%d] = {", nrivals);
    for (int i = 0; i < nrivals; ++i) fprintf(o, "%ld,", (long)lap_ticks[i]);
    fprintf(o, "};\n#endif\n");
    fclose(o);
}

int main(int argc, char** argv) {
    const char *course = 0, *rom = 0, *line = 0, *hex = 0, *header = 0, *report = 0, *pace = "33000,300", *zones = 0;
    int nriv = MAX_RIVALS;
    double speed_k_arg = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--course")) course = argv[i + 1];
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--cpu-line")) line = argv[i + 1];
        else if (!strcmp(argv[i], "--inputs-hex")) hex = argv[i + 1];
        else if (!strcmp(argv[i], "--header")) header = argv[i + 1];
        else if (!strcmp(argv[i], "--report")) report = argv[i + 1];
        else if (!strcmp(argv[i], "--pace")) pace = argv[i + 1];
        else if (!strcmp(argv[i], "--rivals")) nriv = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--zones")) zones = argv[i + 1];
        else if (!strcmp(argv[i], "--speed-k")) speed_k_arg = atof(argv[i + 1]);   /* bake with this speed unit, no replay (--inputs-hex then not needed) */
        else die("unknown argument");
    }
    if (!course || !rom || !line || (!hex && speed_k_arg <= 0) || !header || !report || nriv < 1 || nriv > MAX_RIVALS) die("usage: see rivals_main.c");
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
    FILE* f = hex ? fopen(hex, "r") : NULL;
    if (hex && !f) die("cannot open inputs");
    unsigned int w;
    int ticks = 0;
    double sum_d = 0, sum_v = 0;
    float prev = 0;
    while (f && ticks < MAX_TICKS && fscanf(f, "%x", &w) == 1) {
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
    if (f) fclose(f);
    if (speed_k_arg <= 0 && sum_v <= 0) die("player never moved: cannot calibrate speed units");
    const double speed_k = speed_k_arg > 0 ? speed_k_arg : sum_d / sum_v;   /* distance per tick per speed unit */
    bake(speed_k);
    const double length = line_len_q12 / 4096.0;
    const int32_t len_q8 = (int32_t)course_line_q8(line_len_q12);
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
    fprintf(o, " \"course_line\": \"course-line-1\", \"course_line_hash\": \"0x%016llx\", \"line_length_q8\": %ld,\n \"course_length\": %.3f, \"nodes\": %d, \"speed_units_to_distance_per_tick\": %.6f, \"basis_handedness_mean\": %.6f,\n", line_hash, (long)course_line_q8(line_len_q12), length, node_count, speed_k, handed_sum / node_count);
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
    if (zones) write_zones(zones, len_q8);
    printf("nodes %d length %.1f k %.6f host %.1f ns/tick\n", node_count, length, speed_k, host_ns / (ticks - start_tick));
    return 0;
}
