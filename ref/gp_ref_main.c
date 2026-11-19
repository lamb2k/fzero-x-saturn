/* Reference Grand Prix run on HOST (reference role only): the original race code with 30 racers.
 * MIT; see ../LICENSE. Host headers only; the game code is behind ref.h and rivals_bake.c.
 *
 *   fzx-gp-ref --course RECORD --rom BOOT --cpu-line LINE --difficulty 0..3 --seed N
 *              --ticks MAX --report OUT.json [--trace OUT.bin] [--player-cpu 0|1]
 *
 * Records what the native rival model is judged against: lap and race times per racer, finishing
 * order against grid order, field spread, rank changes, retirements. --trace writes racer 0's
 * line-space state per tick (float32 dist, lateral, speed; little-endian) for the native HOST runs.
 * Nothing here runs on the Saturn, and no output is committed. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref.h"

int rb_gp_init(int difficulty, unsigned seed, const short* cpu_line);
void rb_racer(int i, float f[5], int n[6]);
float rb_course_length(void);
void rb_gp_frame(void);

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

int main(int argc, char** argv) {
    const char *course = 0, *rom = 0, *line = 0, *report = 0, *trace = 0;
    int difficulty = 1, max_ticks = 14000, player_cpu = 1;
    unsigned seed = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--course")) course = argv[i + 1];
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--cpu-line")) line = argv[i + 1];
        else if (!strcmp(argv[i], "--report")) report = argv[i + 1];
        else if (!strcmp(argv[i], "--trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--difficulty")) difficulty = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) max_ticks = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--player-cpu")) player_cpu = atoi(argv[i + 1]);
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
    if (ref_load_course(rec, 1) != 0 || ref_race_init() != 0) die("race set-up failed");
    const int total = rb_gp_init(difficulty, seed, cpu_line);
    if (total != MAXR) die("the reference did not set up 30 racers");
    if (player_cpu) ref_set_cpu_driver(1);
    const double length = rb_course_length();

    static int lap_end[MAXR][4], finish_tick[MAXR], retire_tick[MAXR], grid_pos[MAXR], final_pos[MAXR], target_ms[MAXR], character[MAXR];
    static int prev_lap[MAXR], prev_pos[MAXR];
    static float grid_dist[MAXR], grid_lat[MAXR], max_energy[MAXR], last_dist[MAXR], last_energy[MAXR], top_speed[MAXR];
    static int last_flags[MAXR];
    int start_tick = -1, rank_change_ticks = 0, rank_swaps = 0, ticks = 0, finished = 0, retired = 0;
    double spread_sum = 0, spread_max = 0; int spread_n = 0;
    static double spread_at[32]; int spread_marks = 0;
    double speed_sum[MAXR] = {0}; int speed_n[MAXR] = {0};
    float min_lat_room = 1e9f;
    FILE* tf = trace ? fopen(trace, "wb") : NULL;
    if (trace && !tf) die("cannot write trace");
    for (int i = 0; i < MAXR; ++i) { finish_tick[i] = retire_tick[i] = -1; for (int k = 0; k < 4; ++k) lap_end[i][k] = -1; }
    for (int t = 0; t < max_ticks; ++t) {
        rb_gp_frame();
        ref_tick(0, 0, 0);
        float f[5]; int v[6];
        double lead = -1e18, last = 1e18; int changed = 0;
        for (int i = 0; i < MAXR; ++i) {
            rb_racer(i, f, v);
            if (t == 0) { grid_pos[i] = v[1]; grid_dist[i] = f[0]; grid_lat[i] = f[1]; max_energy[i] = f[4]; target_ms[i] = v[5]; character[i] = v[4]; prev_lap[i] = v[0]; prev_pos[i] = v[1]; }
            if (i == 0 && start_tick < 0 && v[3] > 0) start_tick = t;
            if (i == 0 && tf) { float rec3[3] = { f[0], f[1], f[2] }; fwrite(rec3, sizeof rec3, 1, tf); }
            if (start_tick >= 0) {
                if (v[0] != prev_lap[i] && v[0] >= 2 && v[0] <= 4 && lap_end[i][v[0] - 2] < 0) lap_end[i][v[0] - 2] = t - start_tick;
                if ((v[2] & ST_FINISHED) && finish_tick[i] < 0) { finish_tick[i] = t - start_tick; final_pos[i] = v[1]; ++finished; }
                if ((v[2] & ST_RETIRED) && retire_tick[i] < 0) { retire_tick[i] = t - start_tick; ++retired; }
                if (v[1] != prev_pos[i]) { changed = 1; ++rank_swaps; }
                if (!(v[2] & (ST_FINISHED | ST_RETIRED))) {
                    if (f[0] > lead) lead = f[0];
                    if (f[0] < last) last = f[0];
                    speed_sum[i] += f[2]; speed_n[i]++;
                }
                (void)min_lat_room;
            }
            prev_lap[i] = v[0]; prev_pos[i] = v[1];
            last_dist[i] = f[0]; last_energy[i] = f[3]; last_flags[i] = v[2]; if (f[2] > top_speed[i]) top_speed[i] = f[2];
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
    FILE* o = fopen(report, "w");
    if (!o) die("cannot write report");
    fprintf(o, "{\n \"tier\": \"HOST\", \"role\": \"reference (original code)\", \"difficulty\": %d, \"seed\": %u, \"player_cpu\": %d,\n", difficulty, seed, player_cpu);
    fprintf(o, " \"ticks\": %d, \"race_start_tick\": %d, \"course_length\": %.3f, \"finished\": %d, \"retired\": %d,\n", ticks, start_tick, length, finished, retired);
    fprintf(o, " \"rank_change_ticks\": %d, \"rank_swaps\": %d, \"spread_mean\": %.1f, \"spread_max\": %.1f,\n \"spread_every_600_ticks\": [", rank_change_ticks, rank_swaps / 2, spread_n ? spread_sum / spread_n : 0.0, spread_max);
    for (int i = 0; i < spread_marks; ++i) fprintf(o, "%s%.0f", i ? "," : "", spread_at[i]);
    fprintf(o, "],\n \"racers\": [\n");
    for (int i = 0; i < MAXR; ++i) {
        fprintf(o, "  {\"id\": %d, \"character\": %d, \"grid\": %d, \"grid_dist\": %.2f, \"grid_lateral\": %.2f, \"target_lap_ms\": %d, \"max_energy\": %.2f,"
                   " \"lap_end_ticks\": [%d,%d,%d], \"finish_tick\": %d, \"final_pos\": %d, \"retire_tick\": %d, \"mean_speed\": %.3f,"
                   " \"last_dist\": %.1f, \"last_energy\": %.1f, \"last_flags\": \"%08x\", \"top_speed\": %.2f}%s\n",
                i, character[i], grid_pos[i], grid_dist[i], grid_lat[i], target_ms[i], max_energy[i], lap_end[i][0], lap_end[i][1], lap_end[i][2],
                finish_tick[i], finish_tick[i] >= 0 ? final_pos[i] : -1, retire_tick[i], speed_n[i] ? speed_sum[i] / speed_n[i] : 0.0,
                last_dist[i], last_energy[i], (unsigned)last_flags[i], top_speed[i], i + 1 < MAXR ? "," : "");
    }
    fprintf(o, " ]\n}\n");
    fclose(o);
    printf("gp-ref: difficulty %d seed %u ticks %d start %d finished %d retired %d\n", difficulty, seed, ticks, start_tick, finished, retired);
    return 0;
}
