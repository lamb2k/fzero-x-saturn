/* E1 native physics on HOST: the same CLI and trace as ref/handling_main.c, with the
 * SH2ZAM vehicle-dynamics-1 model in place of the original race code. MIT; see ../../LICENSE.
 *
 *   fzx-phys --pack PACK --inputs SCRIPT [--out TRACE.tsv] [--zones 0|1]
 *            [--driver kl,kh[,boost_period[,start_tick[,target[,end_tick]]]]] [--steps N] [--dump INPUTS]
 *
 * PACK comes from tools/physics_pack.py (local only). Inputs are N64 button words as
 * the reference reads them, mapped by fzx_phys_buttons_n64. --steps N runs the step N
 * times per tick on a copy (timing only; the trace is unchanged). --dump writes each
 * tick's model input and the running state hash, for the SH-2 bit-exact check
 * (tools/physics_sh2).
 * --from T starts at tick T + 1 from a pack baked with fzx-ref --bake-at T (state
 * resync); script ticks up to T are skipped but set the button edge state.
 * --save-at T --save PACK writes the native state after tick T as a pack (state restore check).
 * --checkpoints K prints "chk N HASH" after every K-th step: FNV-1a 64 over the state alone, in
 * big-endian words, as the engine image (saturn/engine/engine_drive.c) records it.
 * --boost-reserve R: the C-059 energy-aware driver, as in the reference driver.
 * --driver-ahead LOOK: test driver R03 (saturn/engine/drive_ahead.h, the original CPU steering law with
 * a look-ahead of LOOK * speed ticks) in place of the kl,kh law; --driver still gives the start and end ticks.
 * --mutate NAME plants one model mutation for the controls: grip+5%, turn+2%, damp0.9968,
 * wall1.6, boost99, zones-off, stick+1 (a one-unit stick offset on non-zero input), force1.0005 (every engine force constant x 1.0005).
 * Output: the fzx-handling v1 trace, and "ticks N hash H" with H = FNV-1a 64 over the
 * raw fixed-point body state of every tick (the bit-exact check between builds). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics_native.h"
#include "engine/drive_ahead.h"

static uint64_t hash = 0xCBF29CE484222325ull;

static void mix(const void* data, size_t size) {
    const unsigned char* p = data;
    while (size--) hash = (hash ^ *p++) * 0x100000001B3ull;
}

static void die(const char* message) {
    fprintf(stderr, "fzx-phys: %s\n", message);
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

static double q16(int32_t v) { return v / 65536.0; }
/* accel_force carries the build's force scale (SH2Z_VD_FORCE_FRAC); the trace prints it as the same real number at any scale */
static double qa(int32_t v) { return v / (65536.0 * (double) (1u << (FZX_PHYS_FORCE_FRAC - 16))); }
static double q12(int32_t v) { return v / 4096.0; }
static double q30(int32_t v) { return v / 1073741824.0; }
static int32_t dotu16(const int32_t u[3], const int32_t v[3]) {
    return (int32_t) (((int64_t) u[0] * v[0] + (int64_t) u[1] * v[1] + (int64_t) u[2] * v[2]) / 1073741824);
}
static double dot30(const int32_t a[3], const int32_t b[3]) {
    return q30(a[0]) * q30(b[0]) + q30(a[1]) * q30(b[1]) + q30(a[2]) * q30(b[2]);
}

int main(int argc, char** argv) {
    const char *pack = NULL, *inputs = NULL, *out_path = NULL, *driver = NULL, *dump_path = NULL;
    FILE* dump = NULL;
    static Step steps[32768];
    static FzxPhysCourse course;
    sh2z_vd_rules_t rules = fzx_phys_rules;
    const char* mutate = NULL;
    int stick_offset = 0, from = -1, save_at = -1, checkpoints = 0;
    int reserve_tenths = 0;
    const char* save_path = NULL;
    static uint8_t image[1 << 20];
    sh2z_vd_body_t b;
    int nsteps, i, tick = 0, zones = 1, driver_boost = 0, driver_start = 0, repeat = 0, vd1 = 0;
    float driver_kl = 0.0f, driver_kh = 0.0f, driver_target = 0.0f, driver_ahead = -1.0f;
    int driver_end = 0x7FFFFFFF;
    unsigned short prev = 0;
    size_t size;
    FILE* f;
    FILE* out = NULL;

    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--pack") == 0) pack = argv[i + 1];
        else if (strcmp(argv[i], "--inputs") == 0) inputs = argv[i + 1];
        else if (strcmp(argv[i], "--out") == 0) out_path = argv[i + 1];
        else if (strcmp(argv[i], "--zones") == 0) zones = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--driver") == 0) driver = argv[i + 1];
        else if (strcmp(argv[i], "--driver-ahead") == 0) driver_ahead = (float) atof(argv[i + 1]);
        else if (strcmp(argv[i], "--steps") == 0) repeat = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--dump") == 0) dump_path = argv[i + 1];
        else if (strcmp(argv[i], "--mutate") == 0) mutate = argv[i + 1];
        else if (strcmp(argv[i], "--from") == 0) from = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--boost-reserve") == 0) reserve_tenths = (int) (atof(argv[i + 1]) * 10.0 + 0.5);
        else if (strcmp(argv[i], "--save-at") == 0) save_at = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--save") == 0) save_path = argv[i + 1];
        else if (strcmp(argv[i], "--checkpoints") == 0) checkpoints = atoi(argv[i + 1]);
        else die("unknown option");
    }
    if (pack == NULL || inputs == NULL) die("usage: fzx-phys --pack PACK --inputs SCRIPT [--out TRACE]");
    if (driver != NULL && sscanf(driver, "%f,%f,%d,%d,%f,%d", &driver_kl, &driver_kh, &driver_boost, &driver_start,
                                 &driver_target, &driver_end) < 2)
        die("--driver is kl,kh[,boost_period[,start_tick[,target[,end_tick]]]]");
    f = fopen(pack, "rb");
    if (f == NULL) die("cannot open the pack");
    size = fread(image, 1, sizeof(image), f);
    fclose(f);
    if (fzx_phys_load(&course, image, size, zones) != 0) die("bad physics pack");
    if (mutate != NULL) {
        /* Planted model mutations (C-056): each must be rejected by some fixture. */
        if (strcmp(mutate, "grip+5%") == 0) course.vehicle.grip_limit += course.vehicle.grip_limit / 20;
        else if (strcmp(mutate, "turn+2%") == 0) {
            course.vehicle.turn_grip += course.vehicle.turn_grip / 50;
            course.vehicle.turn_free += course.vehicle.turn_free / 50;
        } else if (strcmp(mutate, "damp0.9968") == 0) rules.damp_fwd = (int32_t) (0.9968 * 1073741824.0 + 0.5);
        else if (strcmp(mutate, "wall1.6") == 0)
            course.vehicle.wall_bounce = course.start.wall_bounce = (int32_t) (1.6 * 65536.0 + 0.5);
        else if (strcmp(mutate, "boost99") == 0) rules.boost_ticks = 99;
        else if (strcmp(mutate, "zones-off") == 0) zones = 0;
        else if (strcmp(mutate, "stick+1") == 0) stick_offset = 1;
        else if (strcmp(mutate, "force1.0005") == 0) {   /* force card control: every engine force constant x 1.0005 */
            course.vehicle.force_low += course.vehicle.force_low / 2000;
            course.vehicle.force_slide += course.vehicle.force_slide / 2000;
            course.vehicle.force_high += course.vehicle.force_high / 2000;
            course.vehicle.top_force += course.vehicle.top_force / 2000;
        }
#if FZX_PHYS_TUBES
        /* Tube classes (claude-tub): the lane's starting point (tubes as road) and one change
         * per tube rule. */
        else if (strcmp(mutate, "tubes-off") == 0) vd1 = 1;
        else if (strcmp(mutate, "pipe-rim+5") == 0) course.track2.tube.pipe_rim += 5 * 65536;
        else if (strcmp(mutate, "cyl-reach-50") == 0) course.track2.tube.cyl_reach -= 50 * 65536;
        else if (strcmp(mutate, "join-lever-half") == 0) course.track2.tube.join_lever /= 2;
        else if (strcmp(mutate, "pipe-rim-30") == 0) course.track2.tube.pipe_rim -= 30 * 65536;
        else if (strcmp(mutate, "cyl-reach-150") == 0) course.track2.tube.cyl_reach -= 150 * 65536;
        else if (strcmp(mutate, "join-scale+10%") == 0) {
            uint32_t n;
            for (n = 0; n < course.track.count; n++) course.joins[n].scale += course.joins[n].scale / 10;
        }
#endif
        else die("unknown mutation");
        if (strcmp(mutate, "zones-off") == 0 && fzx_phys_load(&course, image, size, 0) != 0) die("bad physics pack");
    }
    b = course.start;
    nsteps = load_script(inputs, steps, 32768);
    if (out_path != NULL && (out = fopen(out_path, "w")) == NULL) die("cannot write the trace");
    if (dump_path != NULL && (dump = fopen(dump_path, "w")) == NULL) die("cannot write the dump");
    if (out != NULL) {
        fprintf(out, "# fzx-handling v1 (native E1, SH2ZAM vehicle-dynamics-1)\n# tick\tbutton\tsx\tsy\tintro\tlateral"
                     "\tradius_left\tradius_right\theight\tlap_distance\trace_distance\tv_side\tv_up\tv_forward\tspeed"
                     "\theading_side\theading_forward\tfx\tfy\tfz\tx\ty\tz\taccel_force\tdrift_force\tslide_boost"
                     "\tenergy\tmax_energy\tflags\tboost\tspin\tattack\tgrip_a\tgrip_b\tslide_count\ta_tap\tsegment"
                     "\tlap\tlaps_completed\trace_time\tlap1\tlap2\tlap3\tv_lateral\n");
    }
    for (i = 0; i < nsteps; i++) {
        int t;
        for (t = 0; t < steps[i].ticks; t++) {
            sh2z_vd_input_t in;
            unsigned short button = steps[i].button;
            signed char sx = steps[i].sx;
            int k;
            if (tick <= from) {
                prev = course.prev_buttons; /* the buttons actually applied on tick `from` */
                tick++;
                continue;
            }
            if (driver != NULL && tick >= driver_start && tick < driver_end && b.intro == 0) {
                float lateral = (float) q16(sh2z_vd_body_lateral(&b));
                float heading = (float) dot30(b.body[0], b.road[2]);
                float steer = driver_kl * (lateral - driver_target) + driver_kh * heading;
                if (steer > 1.0f) steer = 1.0f;
                if (steer < -1.0f) steer = -1.0f;
                sx = (signed char) (steer >= 0.0f ? (int) (steer * 63.0f + 0.5f) : -(int) (-steer * 63.0f + 0.5f));
                if (driver_ahead >= 0.0f) sx = fzx_drive_ahead(&course.track, &b, driver_ahead, &button);
                if (driver_boost > 0 && tick % driver_boost == 0 &&
                    (reserve_tenths <= 0 || 10 * (int64_t) b.energy >= (int64_t) reserve_tenths * course.vehicle.max_energy))
                    button |= 0x4000;
            }
            in.held = fzx_phys_buttons_n64(button);
            in.pressed = fzx_phys_buttons_n64((unsigned short) ((prev ^ button) & button));
            in.released = fzx_phys_buttons_n64((unsigned short) ((prev ^ button) & prev));
            in.stick_x = (signed char) (sx + (sx != 0 ? stick_offset : 0));
            in.stick_y = steps[i].sy;
            prev = button;
            for (k = 0; k < repeat; k++) {
                sh2z_vd_body_t scratch = b;
                fzx_phys_step(&course, &scratch, &rules, &in);
            }
            if (vd1) sh2z_vd_step(&b, &course.track, &course.vehicle, &rules, &in);
            else fzx_phys_step(&course, &b, &rules, &in);
            {
                /* Words in big-endian order, so the SH-2 build hashes the same bytes. */
                const uint32_t* w = (const uint32_t*) (const void*) &b;
                size_t n;
                for (n = 0; n < sizeof(b) / 4; n++) {
                    unsigned char be[4] = { (unsigned char) (w[n] >> 24), (unsigned char) (w[n] >> 16),
                                            (unsigned char) (w[n] >> 8), (unsigned char) w[n] };
                    mix(be, 4);
                }
            }
            if (checkpoints > 0 && (tick + 1) % checkpoints == 0) {
                /* The state alone after tick+1 steps, as the engine image records it (E1 drive). */
                uint64_t keep = hash;
                const uint32_t* w = (const uint32_t*) (const void*) &b;
                size_t n;
                hash = 0xCBF29CE484222325ull;
                for (n = 0; n < sizeof(b) / 4; n++) {
                    unsigned char be[4] = { (unsigned char) (w[n] >> 24), (unsigned char) (w[n] >> 16),
                                            (unsigned char) (w[n] >> 8), (unsigned char) w[n] };
                    mix(be, 4);
                }
                printf("chk %d %016llx\n", tick + 1, (unsigned long long) hash);
                hash = keep;
            }
            if (dump != NULL)
                fprintf(dump, "%u %u %u %d %d %016llx\n", in.held, in.pressed, in.released, in.stick_x, in.stick_y,
                        (unsigned long long) hash);
            if (out != NULL) {
                fprintf(out,
                        "%d\t%04x\t%d\t%d\t%d\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g"
                        "\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%.9g\t%08x\t%d\t%d\t%d\t%d\t%d"
                        "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.9g\n",
                        tick, button, sx, steps[i].sy, b.intro, q16(sh2z_vd_body_lateral(&b)), q16(b.half_left),
                        q16(b.half_right), q16(b.height), q12(b.lap_dist), q12(b.race_dist), q16(b.bvel[0]),
                        q16(b.bvel[1]), q16(b.bvel[2]), q16(b.speed), dot30(b.body[0], b.road[2]),
                        dot30(b.body[0], b.road[0]), q30(b.body[0][0]), q30(b.body[0][1]), q30(b.body[0][2]),
                        q16(b.pos[0]), q16(b.pos[1]), q16(b.pos[2]), qa(b.accel_force), q16(b.lean_force),
                        q16(b.slide_boost), q16(b.energy), q16(course.vehicle.max_energy), (unsigned) b.flags,
                        b.boost_timer, b.spin_timer, b.attack, b.free_timer, b.hit_timer, b.slide_count, b.tap_timer,
                        (int) b.node, b.lap, b.laps_done, b.race_time, b.lap_times[0], b.lap_times[1], b.lap_times[2],
                        q16(dotu16(b.road[2], b.vel)));
            }
            if (save_path != NULL && tick == save_at) {
                static uint8_t saved[1 << 20];
                size_t n = fzx_phys_save(&course, image, &b, button, saved, sizeof(saved));
                FILE* s = fopen(save_path, "wb");
                if (n == 0 || s == NULL || fwrite(saved, 1, n, s) != n) die("cannot save the state");
                fclose(s);
            }
            tick++;
        }
    }
    if (out != NULL) fclose(out);
    if (dump != NULL) fclose(dump);
    {
        /* The final state alone, so a restored run can be compared with an uninterrupted one. */
        uint64_t keep = hash;
        const uint32_t* w = (const uint32_t*) (const void*) &b;
        size_t n;
        hash = 0xCBF29CE484222325ull;
        for (n = 0; n < sizeof(b) / 4; n++) {
            unsigned char be[4] = { (unsigned char) (w[n] >> 24), (unsigned char) (w[n] >> 16),
                                    (unsigned char) (w[n] >> 8), (unsigned char) w[n] };
            mix(be, 4);
        }
        printf("ticks %d final %016llx hash %016llx\n", tick, (unsigned long long) hash, (unsigned long long) keep);
    }
    return 0;
}
