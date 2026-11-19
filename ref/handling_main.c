/* E1 plays-right fixtures: driver for the handling probe. HOST only.
 * Built by ref/handling_build.sh in place of main.c, so ref/main.c, ref/build.sh
 * and everything bound to them stay untouched. MIT; see ../LICENSE.
 *
 *   fzx-ref --course RECORD --inputs SCRIPT [--rom ROM] [--out TRACE.tsv] [--zones 0|1]
 *           [--driver kl,kh[,boost_period[,start_tick[,target[,end_tick]]]]] [--selection skin,engine] [--bake SOURCE]
 *           [--line STATIONS] [--machine CHARACTER,ENGINE]
 *
 * SCRIPT lines are "<ticks> <buttons> [stick_x stick_y]" as for main.c. RECORD is a
 * big-endian 0x7E0 course record. --zones 1 (default) sets up surface effects, jumps
 * and landmines as Race_Init does; --zones 0 is the legacy bridge, which leaves them
 * out. --selection needs the machine-menu build (FZX_SOURCE_MACHINE_MENU=1).
 *
 * --driver is a closed-loop test driver: each tick the stick is
 * 63 * clamp(kl * (lateral - target) + kh * heading_side), from the state after the
 * previous tick. The same rule can drive the reference and a native model, which an
 * open-loop recording cannot: one tick of shift sends a recorded race off the road.
 * With boost_period > 0, B is pressed every that many ticks. Before start_tick and from
 * end_tick on, the stick comes from the script.
 *
 * --bake writes the physics pack source (ref_handling_bake) after set-up and before the
 * first tick, or with --bake-at T after tick T (state resync for a native run that starts
 * there): local only, since it is derived from the course record and the ROM.
 * --boost-reserve R (C-059): the driver's scheduled B only when the previous tick's energy
 * is at least R of the maximum (10 x energy >= 10R x max; R = 0.3 is the candidate).
 * --machine re-initialises racer 0 as stock machine CHARACTER (0-29) at ENGINE (0-1).
 * --line writes the shared course-line stations (ref/course_line.h): a header line
 * "course-line-1 length_q12 count hash" and one "dist_q12 segment t_q16 t" per station.
 *
 * Output: one tab-separated line per tick, and "ticks N hash H" on stdout, H being
 * FNV-1a 64 over every tick's inputs and probe state. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref.h"
#include "handling.h"
#include "course_line.h"

int ref_race_set_selection(unsigned int, unsigned int, float) __attribute__((weak));

static uint64_t hash = 0xCBF29CE484222325ull;

static void mix(const void* data, size_t size) {
    const unsigned char* p = data;
    while (size--) hash = (hash ^ *p++) * 0x100000001B3ull;
}

static FILE* bake_out;

static void bake_emit(const char* key, int a, int b, const float* v, int n) {
    int k;
    fprintf(bake_out, "%s %d %d", key, a, b);
    for (k = 0; k < n; k++) fprintf(bake_out, " %a", (double) v[k]);
    fputc('\n', bake_out);
}

static void die(const char* message) {
    fprintf(stderr, "fzx-ref handling: %s\n", message);
    exit(2);
}

typedef struct {
    int ticks;
    unsigned short button;
    signed char sx, sy;
} Step;

static unsigned short parse_buttons(const char* text) {
    static const struct { const char* name; unsigned short mask; } names[] = {
        { "A", 0x8000 }, { "B", 0x4000 }, { "Z", 0x2000 }, { "S", 0x1000 }, { "L", 0x0020 },
        { "R", 0x0010 }, { "CU", 0x0008 }, { "CD", 0x0004 }, { "CL", 0x0002 }, { "CR", 0x0001 } };
    char copy[64];
    char* token;
    unsigned short mask = 0;
    if (strcmp(text, "-") == 0) return 0;
    snprintf(copy, sizeof(copy), "%s", text);
    for (token = strtok(copy, "+"); token != NULL; token = strtok(NULL, "+")) {
        size_t i, n = sizeof(names) / sizeof(names[0]);
        for (i = 0; i < n && strcmp(token, names[i].name) != 0; i++) {}
        if (i == n) die("unknown button in input script");
        mask |= names[i].mask;
    }
    return mask;
}

static int load_script(const char* path, Step* steps, int capacity) {
    FILE* f = fopen(path, "r");
    char line[256];
    int count = 0;
    if (f == NULL) die("cannot open input script");
    while (fgets(line, sizeof(line), f) != NULL) {
        char buttons[64];
        int ticks, sx = 0, sy = 0, n;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        n = sscanf(line, "%d %63s %d %d", &ticks, buttons, &sx, &sy);
        if (n < 2 || ticks <= 0 || sx < -128 || sx > 127 || sy < -128 || sy > 127 || count == capacity)
            die("bad input script line");
        steps[count].ticks = ticks;
        steps[count].button = parse_buttons(buttons);
        steps[count].sx = (signed char) sx;
        steps[count].sy = (signed char) sy;
        count++;
    }
    fclose(f);
    return count;
}

int main(int argc, char** argv) {
    const char *course = NULL, *inputs = NULL, *rom = NULL, *out_path = NULL, *driver = NULL, *selection = NULL, *bake = NULL, *line = NULL, *machine = NULL;
    int bake_at = -1;
    int reserve_tenths = 0;
    static Step steps[32768];
    static unsigned char record[REF_COURSE_RECORD_SIZE];
    int nsteps, i, tick = 0, zones = 1, driver_boost = 0, driver_start = 0;
    float driver_kl = 0.0f, driver_kh = 0.0f, driver_target = 0.0f;
    int driver_end = 0x7FFFFFFF;
    FILE* f;
    FILE* out = NULL;

    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--course") == 0) course = argv[i + 1];
        else if (strcmp(argv[i], "--inputs") == 0) inputs = argv[i + 1];
        else if (strcmp(argv[i], "--rom") == 0) rom = argv[i + 1];
        else if (strcmp(argv[i], "--out") == 0) out_path = argv[i + 1];
        else if (strcmp(argv[i], "--zones") == 0) zones = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--driver") == 0) driver = argv[i + 1];
        else if (strcmp(argv[i], "--selection") == 0) selection = argv[i + 1];
        else if (strcmp(argv[i], "--bake") == 0) bake = argv[i + 1];
        else if (strcmp(argv[i], "--line") == 0) line = argv[i + 1];
        else if (strcmp(argv[i], "--machine") == 0) machine = argv[i + 1];
        else if (strcmp(argv[i], "--bake-at") == 0) bake_at = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--boost-reserve") == 0) reserve_tenths = (int) (atof(argv[i + 1]) * 10.0 + 0.5);
        else die("unknown option");
    }
    if (course == NULL || inputs == NULL) die("usage: fzx-ref --course RECORD --inputs SCRIPT [--rom ROM] [--out TRACE]");
    if (driver != NULL && sscanf(driver, "%f,%f,%d,%d,%f,%d", &driver_kl, &driver_kh, &driver_boost, &driver_start,
                                 &driver_target, &driver_end) < 2)
        die("--driver is kl,kh[,boost_period[,start_tick[,target[,end_tick]]]]");
    f = fopen(course, "rb");
    if (f == NULL || fread(record, 1, sizeof(record), f) != sizeof(record) || fgetc(f) != EOF)
        die("course record must be exactly 0x7E0 bytes");
    fclose(f);
    nsteps = load_script(inputs, steps, 32768);
    if (rom != NULL) {
        static unsigned char boot[REF_ROM_BOOT_SIZE];
        int k;
        f = fopen(rom, "rb");
        if (f == NULL || fread(boot, 1, sizeof(boot), f) != sizeof(boot)) die("cannot read the ROM");
        fclose(f);
        if (boot[0] == 0x37 && boot[1] == 0x80) {
            for (k = 0; k < REF_ROM_BOOT_SIZE; k += 2) { unsigned char c = boot[k]; boot[k] = boot[k + 1]; boot[k + 1] = c; }
        } else if (boot[0] == 0x40 && boot[1] == 0x12) {
            for (k = 0; k < REF_ROM_BOOT_SIZE; k += 4) {
                unsigned char a = boot[k], b = boot[k + 1];
                boot[k] = boot[k + 3]; boot[k + 1] = boot[k + 2]; boot[k + 2] = b; boot[k + 3] = a;
            }
        } else if (!(boot[0] == 0x80 && boot[1] == 0x37)) {
            die("the ROM header is not an N64 ROM");
        }
        ref_set_rom_boot(boot);
    } else {
        ref_set_rom_boot(NULL);
    }
    if (selection != NULL) {
        unsigned int skin;
        float engine;
        if (sscanf(selection, "%u,%f", &skin, &engine) != 2 || !ref_race_set_selection ||
            ref_race_set_selection(0, skin, engine))
            die("--selection needs skin,engine and a machine-menu build");
    }
    if (ref_load_course(record, 1) != 0 || ref_race_init() != 0) die("race set-up failed");
    if (machine != NULL) {
        int character;
        float engine;
        if (sscanf(machine, "%d,%f", &character, &engine) != 2 || ref_handling_select_machine(character, engine))
            die("--machine is CHARACTER,ENGINE with 0 <= CHARACTER < 30 and 0 <= ENGINE <= 1");
    }
    if (zones) ref_handling_race_features();
    if (bake != NULL && bake_at < 0) {
        if ((bake_out = fopen(bake, "w")) == NULL) die("cannot write the bake source");
        fprintf(bake_out, "# fzx-physics-source v1\n");
        ref_handling_bake(bake_emit);
        fclose(bake_out);
    }
    if (line != NULL) {
        static CourseLineStation st[8192];
        int32_t length;
        int n = course_line_stations(st, 8192, &length, COURSE_LINE_SPACING), k;
        FILE* l;
        if (n < 0) die("course line: too many stations or a gap outside the Q8 bounds");
        if ((l = fopen(line, "w")) == NULL) die("cannot write the course line");
        fprintf(l, "course-line-1 %d %d %016llx\n", (int) length, n, (unsigned long long) course_line_course_hash());
        for (k = 0; k < n; k++)
            fprintf(l, "%d %d %d %a\n", (int) st[k].dist_q12, (int) st[k].segment, (int) st[k].t_q16, (double) st[k].t);
        fclose(l);
    }
    if (out_path != NULL && (out = fopen(out_path, "w")) == NULL) die("cannot write the trace");
    if (out != NULL) {
        fprintf(out, "# fzx-handling v1\n# tick\tbutton\tsx\tsy\tintro\tlateral\tradius_left\tradius_right"
                     "\theight\tlap_distance\trace_distance\tv_side\tv_up\tv_forward\tspeed\theading_side\theading_forward"
                     "\tfx\tfy\tfz\tx\ty\tz\taccel_force\tdrift_force\tslide_boost\tenergy\tmax_energy\tflags\tboost\tspin"
                     "\tattack\tgrip_a\tgrip_b\tslide_count\ta_tap\tsegment\tlap\tlaps_completed\trace_time\tlap1"
                     "\tlap2\tlap3\tv_lateral\n");
    }
    for (i = 0; i < nsteps; i++) {
        int t;
        for (t = 0; t < steps[i].ticks; t++) {
            RefHandling h;
            unsigned short button = steps[i].button;
            signed char sx = steps[i].sx;
            if (driver != NULL && tick >= driver_start && tick < driver_end) {
                ref_handling(&h);
                if (h.intro == 0) {
                    float steer = driver_kl * (h.lateral - driver_target) + driver_kh * h.heading_side;
                    if (steer > 1.0f) steer = 1.0f;
                    if (steer < -1.0f) steer = -1.0f;
                    sx = (signed char) (steer >= 0.0f ? (int) (steer * 63.0f + 0.5f) : -(int) (-steer * 63.0f + 0.5f));
                    /* C-059 energy-aware driver: a scheduled B only when the previous tick's
                     * energy is at least boost_reserve of the maximum. */
                    if (driver_boost > 0 && tick % driver_boost == 0 &&
                        (reserve_tenths <= 0 || 10.0f * h.energy >= (float) reserve_tenths * h.max_energy))
                        button |= 0x4000;
                }
            }
            ref_tick(button, sx, steps[i].sy);
            memset(&h, 0, sizeof(h));
            ref_handling(&h);
            mix(&tick, sizeof(tick));
            mix(&button, sizeof(button));
            mix(&sx, 1);
            mix(&h, sizeof(h));
            if (out != NULL) {
                fprintf(out,
                        "%d\t%04x\t%d\t%d\t%d\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g"
                        "\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%08x\t%d\t%d\t%d\t%d\t%d"
                        "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.9g\n",
                        tick, button, sx, steps[i].sy, h.intro, h.lateral, h.radius_left, h.radius_right, h.height,
                        h.lap_distance, h.race_distance, h.v_side, h.v_up, h.v_forward, h.speed, h.heading_side,
                        h.heading_forward, h.forward[0], h.forward[1], h.forward[2], h.pos[0], h.pos[1], h.pos[2],
                        h.accel_force, h.drift_force, h.slide_boost, h.energy, h.max_energy, (unsigned) h.flags,
                        h.boost_timer, h.spin_timer, h.attack_state, h.grip_a, h.grip_b, h.slide_count, h.a_tap,
                        h.segment, h.lap, h.laps_completed, h.race_time, h.lap_times[0], h.lap_times[1],
                        h.lap_times[2], h.v_lateral);
            }
            if (bake != NULL && tick == bake_at) {
                if ((bake_out = fopen(bake, "w")) == NULL) die("cannot write the bake source");
                fprintf(bake_out, "# fzx-physics-source v1 after tick %d\n", tick);
                ref_handling_bake(bake_emit);
                bake_emit("input", button, 0, NULL, 0);
                fclose(bake_out);
            }
            tick++;
        }
    }
    if (out != NULL) fclose(out);
    printf("ticks %d hash %016llx\n", tick, (unsigned long long) hash);
    return 0;
}
