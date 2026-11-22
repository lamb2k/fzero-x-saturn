/* Reference Grand Prix run on HOST (reference role only): the original race code with 30 racers.
 * MIT; see ../LICENSE. Host headers only; the game code is behind ref.h and rivals_bake.c.
 *
 *   fzx-gp-ref --course RECORD --rom BOOT --cpu-line LINE --difficulty 0..3 --seed N
 *              --ticks MAX --report OUT.json [--trace OUT.bin] [--player-cpu 0|1] [--tuning-course N]
 *              [--zones 0|1] [--trace-phys OUT.bin]
 *
 * Records what the native rival model is judged against: lap and race times per racer, finishing
 * order against grid order, field spread, rank changes, retirements. --trace writes racer 0's
 * line-space state per tick (float32 dist, lateral, speed; little-endian) for the native HOST runs.
 * --trace-phys (C-118) writes racer 0's physical pose per tick, from the same tick as --trace and on past the
 * race's end by PHYS_EXTRA ticks (the native run may outlast it): one 48-byte little-endian record, int32 tick,
 * segment index, stateFlags, lap; float32 lapDistance, lateral, up, speed, raceDistance, world x, y, z.
 * The extra ticks only step the game on after everything the report reads is final.
 * Nothing here runs on the Saturn, and no output is committed. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref.h"

int rb_gp_init(int difficulty, unsigned seed, const short* cpu_line);
int rb_gp_tuning_course(int n);
void rb_gp_target(int out[3]);
void rb_racer_pace(int i, int out[6]);
extern int rb_gp_fresh_save;
void rb_racer(int i, float f[5], int n[8]);
float rb_racer_up(int i);
void rb_racer_disp(int i, float d[3]);
void rb_racer_phys(int i, float f[6], int n[3]);
#define PHYS_EXTRA 3100
static void phys_write(FILE* pf, int t) {
    float f[5], g[6]; int v[8], n[3];
    rb_racer(0, f, v); rb_racer_phys(0, g, n);
    const int32_t ri[4] = { t, n[0], n[1], n[2] };
    const float rf[8] = { g[0], g[1], g[2], f[2], f[0], g[3], g[4], g[5] };
    fwrite(ri, sizeof ri, 1, pf); fwrite(rf, sizeof rf, 1, pf);
}
float rb_course_length(void);
void rb_gp_frame(void);
void rb_gp_zones(void);
void rb_cpu_diag(int i, float f[5], int n[8]);
void rb_cpu_diag2(int i, float f[6], int n[2]);
void rb_set_ko_age(int i, int age, int by);
int rb_ko_count(void);
float rb_regain(int i);

#define MAXR 30
#define ST_RETIRED (1 << 18)
#define ST_FINISHED (1 << 25)

static void die(const char* m) { fprintf(stderr, "fzx-gp-ref: %s\n", m); exit(2); }
static unsigned char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) die("cannot open input");
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = malloc(*n ? *n : 1);
    if (!b || fread(b, 1, *n, f) != *n) die("short read");
    fclose(f);
    return b;
}

/* Tube angles (claude/jack-gp): RIVALS.PAK's nodes (16-byte records, then 32 bytes of pose each) give
 * each pipe (shape 0x80) and cylinder (0xC0) span; a CPU racer's tick on one is binned by its angle round
 * the tube from the drive line (cylinder: from the top, the displacement's up; pipe: from the bottom). */
#define TUBE_BINS 4
static int32_t tube_start[4096]; static uint16_t tube_flags[4096], tube_hw[4096]; static int16_t tube_lat[4096]; static int tube_n, tube_last;
static long tube_ticks, tube_bin[TUBE_BINS];
static void tube_load(const char* path) {
    size_t n; unsigned char* b = read_file(path, &n);
    if (!n || n % 48 || n / 48 > 4096) die("tube pack is not nodes x 48");
    tube_n = (int)(n / 48);
    for (int i = 0; i < tube_n; ++i) {
        const unsigned char* p = b + 16 * i;
        tube_start[i] = (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
        tube_hw[i] = (uint16_t)((p[8] << 8) | p[9]); tube_lat[i] = (int16_t)((p[4] << 8) | p[5]); tube_flags[i] = (uint16_t)((p[10] << 8) | p[11]);
    }
    free(b);
}
static int tube_shape(double dist_units, double length) {
    double d = fmod(dist_units, length); if (d < 0) d += length;
    const int32_t q8 = (int32_t)(d * 256.0);
    int lo = 0, hi = tube_n - 1;
    while (lo < hi) { const int mid = (lo + hi + 1) / 2; if (tube_start[mid] <= q8) lo = mid; else hi = mid - 1; }
    tube_last = lo;
    const int shape = tube_flags[lo] & 0x01c0;
    return shape == 0x0080 ? 1 : shape == 0x00c0 ? 2 : 0;
}
static void tube_add(double angle_deg) {
    const int k = angle_deg < 30 ? 0 : angle_deg < 60 ? 1 : angle_deg < 90 ? 2 : 3;
    ++tube_bin[k]; ++tube_ticks;
}

int main(int argc, char** argv) {
    const char *course = 0, *rom = 0, *line = 0, *report = 0, *trace = 0, *tube_pack = 0, *diag = 0, *ko_probe = 0, *pace_log = 0, *phys = 0;
    int difficulty = 1, max_ticks = 14000, player_cpu = 1, tuning_course = 0, zones = 0;
    unsigned seed = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--course")) course = argv[i + 1];
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--cpu-line")) line = argv[i + 1];
        else if (!strcmp(argv[i], "--report")) report = argv[i + 1];
        else if (!strcmp(argv[i], "--trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--trace-phys")) phys = argv[i + 1];
        else if (!strcmp(argv[i], "--difficulty")) difficulty = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) max_ticks = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--player-cpu")) player_cpu = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--tuning-course")) tuning_course = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--zones")) zones = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--tube-pack")) tube_pack = argv[i + 1];   /* RIVALS.PAK: tube_angles over its pipe and cylinder nodes */   /* 1: the surface zones, jumps and mines (rivals_bake.c) */
        else if (!strcmp(argv[i], "--diag")) diag = argv[i + 1];
        else if (!strcmp(argv[i], "--pace-log")) pace_log = argv[i + 1];   /* claude/rival-class: PACE LOG below */
        else if (!strcmp(argv[i], "--fresh-save")) rb_gp_fresh_save = atoi(argv[i + 1]);   /* 0: the old records-at-0 state (rivals_bake.c) */
        else if (!strcmp(argv[i], "--ko-probe")) ko_probe = argv[i + 1];   /* RACER:TICK:AGE:BY, see KO PROBE below */   /* line-field-5: per racer and tick, see DIAG below */
        else die("unknown argument");
    }
    if (!course || !rom || !line || !report) die("usage: see gp_ref_main.c");
    size_t n;
    unsigned char* rec = read_file(course, &n);
    if (n != 0x7E0) die("course record must be 0x7E0 bytes");
    unsigned char* r = read_file(rom, &n);
    if (n < REF_ROM_BOOT_SIZE) die("short ROM boot");
    ref_set_rom_boot(r);
    unsigned char* lb = read_file(line, &n);
    if (n != 0x400) die("CPU line must be 0x400 bytes");
    static short cpu_line[0x200];
    for (int i = 0; i < 0x200; ++i) cpu_line[i] = (short)((lb[2 * i] << 8) | lb[2 * i + 1]);
    if (tube_pack) tube_load(tube_pack);
    if (ref_load_course(rec, 1) != 0 || ref_race_init() != 0) die("race set-up failed");
    if (rb_gp_tuning_course(tuning_course) != 0) die("--tuning-course is the original course index, 0..23");
    const int total = rb_gp_init(difficulty, seed, cpu_line);
    if (total != MAXR) die("the reference did not set up 30 racers");
    if (zones) rb_gp_zones();
    { int tg[3]; rb_gp_target(tg); printf("gp-ref: target %d record0 %d table %d\n", tg[0], tg[1], tg[2]); }
    if (player_cpu) ref_set_cpu_driver(1);
    const double length = rb_course_length();

    static int lap_end[MAXR][4], finish_tick[MAXR], retire_tick[MAXR], grid_pos[MAXR], final_pos[MAXR], target_ms[MAXR], character[MAXR];
    static int prev_lap[MAXR], prev_pos[MAXR], chase[MAXR], pace_rank[MAXR];
    static float grid_dist[MAXR], grid_lat[MAXR], max_energy[MAXR], last_dist[MAXR], last_energy[MAXR], top_speed[MAXR];
    static int last_flags[MAXR];
    int start_tick = -1, rank_change_ticks = 0, rank_swaps = 0, ticks = 0, finished = 0, retired = 0;
    double spread_sum = 0, spread_max = 0; int spread_n = 0;
    static double spread_at[32]; int spread_marks = 0;
    double speed_sum[MAXR] = {0}; int speed_n[MAXR] = {0};
    float min_lat_room = 1e9f;
    FILE* tf = trace ? fopen(trace, "wb") : NULL;
    /* DIAG (claude/line-field-5): one 88-byte little-endian record per racer and tick from the race start:
     * int32 tick, id, stateFlags, shape, unk_288, attacker, segment, lap, boostTimer, machineLod; float32 dist,
     * lateral, up, energy, unk_370, lastStickX, unk_33C, pos x y z, radius left, right. HOST diagnostics only; output never committed. */
    FILE* df = diag ? fopen(diag, "wb") : NULL;
    if (diag && !df) die("cannot write diag");
    if (trace && !tf) die("cannot write trace");
    /* PACE LOG (claude/rival-class): text, race ticks from the start. "S id rank chase setting a360 a364" per racer at
     * the start; "T tick target" when the race target (D_i3_80141990) changes; "L tick id lap target398 rank" when a
     * racer's lap counter changes; "F tick id target398 rank" at its finish. HOST diagnostics only. */
    FILE* pf = pace_log ? fopen(pace_log, "w") : NULL;
    if (pace_log && !pf) die("cannot write pace log");
    int pace_prev_target = 0, pace_prev_lap[MAXR] = {0}, pace_started = 0;
    FILE* physf = phys ? fopen(phys, "wb") : NULL;
    if (phys && !physf) die("cannot write trace-phys");
    for (int i = 0; i < MAXR; ++i) { finish_tick[i] = retire_tick[i] = -1; for (int k = 0; k < 4; ++k) lap_end[i][k] = -1; }
    for (int t = 0; t < max_ticks; ++t) {
        rb_gp_frame();
        ref_tick(0, 0, 0);
        /* KO PROBE (claude/line-field-5): after race tick TICK (from the start), racer RACER's KO-credit age is set
         * to AGE with racer BY (-1 none) as its attacker; every tick from then until 3 ticks after its fall
         * starts prints the age, flags, KO count and BY's pending regain (stderr). HOST diagnostics only. */
        if (ko_probe && start_tick >= 0) {
            int kr, kt, ka, kb; static int fall_seen = -1;
            if (sscanf(ko_probe, "%d:%d:%d:%d", &kr, &kt, &ka, &kb) == 4) {
                if (t - start_tick == kt) rb_set_ko_age(kr, ka, kb);
                if (t - start_tick >= kt && (fall_seen < 0 || t - start_tick <= fall_seen + 3)) {
                    float g[5]; int m[8]; rb_cpu_diag(kr, g, m);
                    if ((m[0] & (1 << 19)) && fall_seen < 0) fall_seen = t - start_tick;
                    fprintf(stderr, "ko-probe t %d racer %d age %d attacker %d flags %08x falling %d kos %d regain_by %.3f energy_victim %.2f\n",
                            t - start_tick, kr, m[2], m[3], (unsigned)m[0], (m[0] >> 19) & 1, rb_ko_count(), kb >= 0 ? rb_regain(kb) : 0.0f, g[2]);
                }
            }
        }
        float f[5]; int v[8];
        double lead = -1e18, last = 1e18; int changed = 0;
        for (int i = 0; i < MAXR; ++i) {
            rb_racer(i, f, v);
            if (t == 0) { grid_pos[i] = v[1]; grid_dist[i] = f[0]; grid_lat[i] = f[1]; max_energy[i] = f[4]; target_ms[i] = v[5]; character[i] = v[4]; chase[i] = v[6]; pace_rank[i] = v[7]; prev_lap[i] = v[0]; prev_pos[i] = v[1]; }
            if (i == 0 && start_tick < 0 && v[3] > 0) start_tick = t;
            if (i == 0 && tf) { float rec3[3] = { f[0], f[1], f[2] }; fwrite(rec3, sizeof rec3, 1, tf); }
            if (i == 0 && physf) phys_write(physf, t);
            if (start_tick >= 0) {
                if (v[0] != prev_lap[i] && v[0] >= 2 && v[0] <= 4 && lap_end[i][v[0] - 2] < 0) lap_end[i][v[0] - 2] = t - start_tick;
                if ((v[2] & ST_FINISHED) && finish_tick[i] < 0) { finish_tick[i] = t - start_tick; final_pos[i] = v[1]; ++finished; }
                if ((v[2] & ST_RETIRED) && retire_tick[i] < 0) { retire_tick[i] = t - start_tick; ++retired; }
                if (v[1] != prev_pos[i]) { changed = 1; ++rank_swaps; }
                if (!(v[2] & (ST_FINISHED | ST_RETIRED))) {
                    if (tube_pack && i > 0) {
                        const int shape = tube_shape(f[0], length);
                        if (shape) {
                            const double up = rb_racer_up(i), a = atan2(fabs((double)f[1]), shape == 2 ? up : -up) * 57.29578;
                            tube_add(a);
                            if (getenv("FZX_TUBE_DEBUG") && t % 50 == 0) {
                                float dw[3]; rb_racer_disp(i, dw);
                                fprintf(stderr, "tube t %d i %d shape %d dist %.0f lat %.1f up %.1f angle %.0f node_lat %d hw %d world_up %.1f\n", t, i, shape, f[0], f[1], up, a, tube_lat[tube_last], tube_hw[tube_last], dw[1]);
                            }
                        }
                    }
                    if (f[0] > lead) lead = f[0];
                    if (f[0] < last) last = f[0];
                    speed_sum[i] += f[2]; speed_n[i]++;
                }
                (void)min_lat_room;
            }
            if (df && start_tick >= 0) {
                float g[5], h[6]; int m[8], k[2]; rb_cpu_diag(i, g, m); rb_cpu_diag2(i, h, k);
                int32_t ri[10] = { t - start_tick, i, m[0], m[1], m[2], m[3], m[4], m[7], k[0], k[1] };
                float rf[12] = { f[0], f[1], rb_racer_up(i), f[3], g[0], g[1], h[0], h[1], h[2], h[3], h[4], h[5] };
                fwrite(ri, sizeof ri, 1, df); fwrite(rf, sizeof rf, 1, df);
            }
            prev_lap[i] = v[0]; prev_pos[i] = v[1];
            last_dist[i] = f[0]; last_energy[i] = f[3]; last_flags[i] = v[2]; if (f[2] > top_speed[i]) top_speed[i] = f[2];
        }
        if (pf && start_tick >= 0) {
            int tg[3], pc[6], fl[8]; float ff[5];
            rb_gp_target(tg);
            for (int i = 0; i < MAXR; ++i) {
                rb_racer(i, ff, fl); rb_racer_pace(i, pc);
                if (!pace_started) fprintf(pf, "S %d %d %d %d %d %d\n", i, pc[1], pc[2], pc[3], pc[4], pc[5]);
                if (pace_started && fl[0] != pace_prev_lap[i]) fprintf(pf, "L %d %d %d %d %d\n", t - start_tick, i, fl[0], pc[0], pc[1]);
                if (finish_tick[i] == t - start_tick) fprintf(pf, "F %d %d %d %d\n", t - start_tick, i, pc[0], pc[1]);
                pace_prev_lap[i] = fl[0];
            }
            if (!pace_started || tg[0] != pace_prev_target) fprintf(pf, "T %d %d\n", t - start_tick, tg[0]);
            pace_prev_target = tg[0]; pace_started = 1;
        }
        ticks = t + 1;
        if (start_tick >= 0) {
            rank_change_ticks += changed;
            if (lead > last) { const double s = lead - last; spread_sum += s; ++spread_n; if (s > spread_max) spread_max = s; }
            if ((t - start_tick) % 600 == 599 && spread_marks < 32) spread_at[spread_marks++] = lead > last ? lead - last : 0;
            if (finished + retired >= MAXR) break;
        }
    }
    if (tf) fclose(tf);
    if (df) fclose(df);
    if (pf) fclose(pf);
    if (physf) {   /* C-118: racer 0's pose on past the race's end; nothing the report reads changes */
        for (int t = ticks; t < ticks + PHYS_EXTRA; ++t) { rb_gp_frame(); ref_tick(0, 0, 0); phys_write(physf, t); }
        fclose(physf);
    }
    FILE* o = fopen(report, "w");
    if (!o) die("cannot write report");
    fprintf(o, "{\n \"tier\": \"HOST\", \"role\": \"reference (original code)\", \"difficulty\": %d, \"seed\": %u, \"player_cpu\": %d,\n", difficulty, seed, player_cpu);
    fprintf(o, " \"ticks\": %d, \"race_start_tick\": %d, \"course_length\": %.3f, \"finished\": %d, \"retired\": %d,\n", ticks, start_tick, length, finished, retired);
    fprintf(o, " \"rank_change_ticks\": %d, \"rank_swaps\": %d, \"spread_mean\": %.1f, \"spread_max\": %.1f,\n \"spread_every_600_ticks\": [", rank_change_ticks, rank_swaps / 2, spread_n ? spread_sum / spread_n : 0.0, spread_max);
    for (int i = 0; i < spread_marks; ++i) fprintf(o, "%s%.0f", i ? "," : "", spread_at[i]);
    fprintf(o, "],\n");
    if (tube_pack) fprintf(o, " \"tube_ticks\": %ld, \"tube_angle_bins\": [%ld,%ld,%ld,%ld],\n", tube_ticks, tube_bin[0], tube_bin[1], tube_bin[2], tube_bin[3]);
    fprintf(o, " \"racers\": [\n");
    for (int i = 0; i < MAXR; ++i) {
        fprintf(o, "  {\"id\": %d, \"character\": %d, \"grid\": %d, \"grid_dist\": %.2f, \"grid_lateral\": %.2f, \"target_lap_ms\": %d, \"chase\": %d, \"pace_rank\": %d, \"max_energy\": %.2f,"
                   " \"lap_end_ticks\": [%d,%d,%d], \"finish_tick\": %d, \"final_pos\": %d, \"retire_tick\": %d, \"mean_speed\": %.3f,"
                   " \"last_dist\": %.1f, \"last_energy\": %.1f, \"last_flags\": \"%08x\", \"top_speed\": %.2f}%s\n",
                i, character[i], grid_pos[i], grid_dist[i], grid_lat[i], target_ms[i], chase[i], pace_rank[i], max_energy[i], lap_end[i][0], lap_end[i][1], lap_end[i][2],
                finish_tick[i], finish_tick[i] >= 0 ? final_pos[i] : -1, retire_tick[i], speed_n[i] ? speed_sum[i] / speed_n[i] : 0.0,
                last_dist[i], last_energy[i], (unsigned)last_flags[i], top_speed[i], i + 1 < MAXR ? "," : "");
    }
    fprintf(o, " ]\n}\n");
    fclose(o);
    printf("gp-ref: difficulty %d seed %u ticks %d start %d finished %d retired %d\n", difficulty, seed, ticks, start_tick, finished, retired);
    return 0;
}
