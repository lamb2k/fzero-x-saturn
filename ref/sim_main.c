/* Simulation-lane HOST harness for quick win 1 (FZX_DEFER_PROJECTION). MIT; see ../LICENSE.
 * Host headers only; the game code is behind ref.h and sim.h.
 *
 *   fzx-sim --course RECORD --byte-order big|little (--inputs SCRIPT | --inputs-hex HEX)
 *           [--rom ROM] [--select SKIN,ENGINE_BITS] [--ticks N] [--trace OUT]
 *           [--camera-trace OUT] [--read-every K] [--restart-at T]
 *           [--read-after-restart 0|1] [--records-probe T]
 *
 * The STRICT trace hash is ref/main.c's (FNV-1a 64 over tick, inputs and
 * RefState), so it compares directly with ref/main.c and the fixture hashes.
 * Quick win 1 is exact work elimination: knob on and knob off must give the
 * same hash and the same camera trace.
 *
 * --read-every K reads the cameras (ref_camera_read, which builds deferred
 * matrices) only every K ticks, as the Saturn image does once per presented
 * frame; the camera trace then holds only those ticks. K = 1 reads every tick.
 *
 * --restart-at T restarts the race (ref_race_restart, as the image's
 * orig_restart does) before tick T and keeps ticking. With the knob on the
 * matrices of the ticks since the last read are still pending at that point;
 * ref_camera_flush_all runs first, as in the image. --read-after-restart 1
 * then reads both cameras before the first post-restart tick.
 *
 * --records-probe T moves camera 1 (far plane +64 units; camera 1 is
 * otherwise constant on every captured route) before tick T, and after that tick
 * reads camera 1 the way the records scene does (flush, then read). Knob on and
 * off must log the same matrices; a build without that flush must not.
 * Both probes are HOST controls (ref_sim_test_move_camera is never in an image). */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>

#include "ref.h"
#include "sim.h"

typedef struct {
    int ticks;
    unsigned short button;
    signed char sx, sy;
} Step;

typedef struct { /* all mutable driver state lives here, never in the closure image */
    uint64_t hash;
    FILE *trace, *camera;
    int read_every;
} Driver;

static void die(const char* message) {
    fprintf(stderr, "fzx-sim: %s\n", message);
    exit(2);
}

static void mix(Driver* d, const void* data, size_t size) {
    const unsigned char* p = data;
    while (size--) d->hash = (d->hash ^ *p++) * 0x100000001B3ull;
}

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
        steps[count].sx = (signed char)sx;
        steps[count].sy = (signed char)sy;
        count++;
    }
    fclose(f);
    return count;
}

/* One word per tick: button << 16 | (u8)sx << 8 | (u8)sy (the image's input log). */
static int load_hex(const char* path, Step* steps, int capacity) {
    FILE* f = fopen(path, "r");
    unsigned int word;
    int count = 0;
    if (f == NULL) die("cannot open input words");
    while (fscanf(f, "%x", &word) == 1) {
        if (count == capacity) die("too many input words");
        steps[count].ticks = 1;
        steps[count].button = (unsigned short)(word >> 16);
        steps[count].sx = (signed char)(unsigned char)(word >> 8);
        steps[count].sy = (signed char)(unsigned char)word;
        count++;
    }
    fclose(f);
    return count;
}

static uint32_t bits(float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof(u));
    return u;
}

static void camera_read(Driver* d, int tick) {
    RefCamera camera;
    unsigned index, j;
    if (ref_camera_count() != 2) die("Time Attack camera count changed");
    for (index = 0; index < ref_camera_count(); ++index) {
#if FZX_DEFER_PROJECTION
        ref_camera_flush(index); /* as the image's publication does before reading */
#endif
        if (!ref_camera_read(index, &camera) || camera.faults) die("unmapped source camera viewport");
        for (j = 4; j < 62; ++j) {
            float value;
            memcpy(&value, (const unsigned char*)&camera + 4 * j, 4);
            if (!isfinite(value)) die("nonfinite source camera value");
        }
        if (d->camera) {
            fprintf(d->camera, "%d %u", tick, index);
            for (j = 0; j < sizeof(camera) / 4; ++j) {
                uint32_t word;
                memcpy(&word, (const unsigned char*)&camera + 4 * j, 4);
                fprintf(d->camera, " %08x", word);
            }
            fputc('\n', d->camera);
        }
    }
}

/* --records-probe T: the records scene's camera-1 read (fzx_records_model_camera:
 * flush camera 1, then read its matrices), logged as "records T 1 ...". */
static void records_read(Driver* d, int tick) {
    RefCamera camera;
    unsigned j;
#if FZX_DEFER_PROJECTION
    ref_camera_flush(1); /* as saturn/records_native.c does before the matrix bridge */
#endif
    if (!ref_camera_read(1, &camera) || camera.faults) die("unmapped camera 1 viewport");
    if (d->camera) {
        fprintf(d->camera, "records %d 1", tick);
        for (j = 0; j < sizeof(camera) / 4; ++j) {
            uint32_t word;
            memcpy(&word, (const unsigned char*)&camera + 4 * j, 4);
            fprintf(d->camera, " %08x", word);
        }
        fputc('\n', d->camera);
    }
}

static void after_tick(Driver* d, int tick, const Step* in) {
    RefState s;
    ref_state(&s);
    mix(d, &tick, sizeof(tick));
    mix(d, &in->button, sizeof(in->button));
    mix(d, &in->sx, 1);
    mix(d, &in->sy, 1);
    mix(d, &s, sizeof(s));
    if (d->trace) {
        fprintf(d->trace,
                "%d\t%d\t%04x\t%d\t%d\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x"
                "\t%d\t%d\t%d\t%d\t%08x\t%d\t%.3f\t%.3f\t%.3f\t%.3f\n",
                tick, s.intro, in->button, in->sx, in->sy, bits(s.pos[0]), bits(s.pos[1]), bits(s.pos[2]),
                bits(s.vel[0]), bits(s.vel[1]), bits(s.vel[2]), bits(s.speed), bits(s.energy),
                bits(s.lap_distance), bits(s.seg_t), bits(s.height), s.segment, s.lap, s.laps_completed,
                s.race_time, (uint32_t)s.state_flags, s.boost_timer, s.pos[0], s.pos[1], s.pos[2], s.speed);
    }
    if (d->read_every > 0 && (tick + 1) % d->read_every == 0) camera_read(d, tick);
}


/* Pin the host floating-point state the oracle assumes (CODEX-TO-OPUS C-001 item 5):
 * SSE round-to-nearest-even, gradual underflow (FTZ and DAZ off), exceptions masked.
 * Asserted again after the run. */
#define SIM_MXCSR_DEFAULT 0x1F80u
#define SIM_MXCSR_MODE 0xE040u /* RC bits 13-14, FZ bit 15, DAZ bit 6: all must stay 0 */
static void pin_fp_state(void) {
    _mm_setcsr(SIM_MXCSR_DEFAULT);
    if (_mm_getcsr() & SIM_MXCSR_MODE) die("cannot pin MXCSR");
}

int main(int argc, char** argv) {
    const char *course = NULL, *order = NULL, *inputs = NULL, *hex = NULL, *trace = NULL, *rom = NULL;
    const char *camera_path = NULL, *select = NULL;
    static Step steps[16384];
    static unsigned char record[REF_COURSE_RECORD_SIZE];
    int nsteps, i, tick = 0, limit = -1, restart_at = -1, records_probe = -1, read_after_restart = 0;
    FILE* f;
    Driver* d = calloc(1, sizeof(Driver));
    if (!d) die("out of memory");
    pin_fp_state();
    d->hash = 0xCBF29CE484222325ull;
    d->read_every = 1;

    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--course") == 0) course = argv[i + 1];
        else if (strcmp(argv[i], "--byte-order") == 0) order = argv[i + 1];
        else if (strcmp(argv[i], "--inputs") == 0) inputs = argv[i + 1];
        else if (strcmp(argv[i], "--inputs-hex") == 0) hex = argv[i + 1];
        else if (strcmp(argv[i], "--trace") == 0) trace = argv[i + 1];
        else if (strcmp(argv[i], "--rom") == 0) rom = argv[i + 1];
        else if (strcmp(argv[i], "--ticks") == 0) limit = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--camera-trace") == 0) camera_path = argv[i + 1];
        else if (strcmp(argv[i], "--read-every") == 0) d->read_every = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--select") == 0) select = argv[i + 1];
        else if (strcmp(argv[i], "--restart-at") == 0) restart_at = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--read-after-restart") == 0) read_after_restart = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--records-probe") == 0) records_probe = atoi(argv[i + 1]);
        else die("unknown option");
    }
    if (i != argc) die("options take one value each");
    if (course == NULL || order == NULL || (inputs == NULL) == (hex == NULL) ||
        (strcmp(order, "big") != 0 && strcmp(order, "little") != 0) || d->read_every < 0)
        die("usage: see ref/sim_main.c");
    f = fopen(course, "rb");
    if (f == NULL || fread(record, 1, sizeof(record), f) != sizeof(record) || fgetc(f) != EOF)
        die("course record must be exactly 0x7E0 bytes");
    fclose(f);
    nsteps = inputs ? load_script(inputs, steps, 16384) : load_hex(hex, steps, 16384);
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
    if (select != NULL) {
#if FZX_SOURCE_MACHINE_MENU
        unsigned int skin, engine_bits;
        float engine;
        if (sscanf(select, "%u,%x", &skin, &engine_bits) != 2) die("--select SKIN,ENGINE_BITS");
        memcpy(&engine, &engine_bits, 4);
        if (ref_race_set_selection(0, skin, engine)) die("unsupported selection");
#else
        die("--select needs FZX_SOURCE_MACHINE_MENU=1");
#endif
    }
    if (ref_load_course(record, strcmp(order, "big") == 0) != 0 || ref_race_init() != 0)
        die("race set-up failed: the original loader did not request the course record");
    if (trace != NULL && (d->trace = fopen(trace, "w")) == NULL) die("cannot write trace");
    if (camera_path && !(d->camera = fopen(camera_path, "w"))) die("cannot write camera trace");
    if (d->trace) {
        fprintf(d->trace, "# fzx-trace v1\n# tick\tintro\tbutton\tsx\tsy\tposx\tposy\tposz\tvelx\tvely\tvelz\tspeed\tenergy"
                          "\tlapdist\tsegt\theight\tsegment\tlap\tlapsdone\tracetime\tflags\tboost\tx\ty\tz\tspeed_f\n");
    }
    for (i = 0; i < nsteps && (limit < 0 || tick < limit); i++) {
        int t;
        for (t = 0; t < steps[i].ticks && (limit < 0 || tick < limit); t++) {
            if (tick == restart_at) {
#if FZX_DEFER_PROJECTION
                ref_camera_flush_all(); /* as orig_restart does before ref_race_restart */
#endif
                ref_race_restart();
                mix(d, "restart", 7);
                /* Read before the first post-restart tick: the matrices must be the ones
                 * the cameras had at the restart, as without the knob. */
                if (read_after_restart) camera_read(d, -1 - tick);
            }
            /* Give camera 1 a state its last matrix build has not seen (HOST control). */
            if (tick == records_probe) ref_sim_test_move_camera(1, 64.0f);
            ref_tick(steps[i].button, steps[i].sx, steps[i].sy);
            if (tick == records_probe) records_read(d, tick);
            after_tick(d, tick, &steps[i]);
            tick++;
        }
    }
    if (_mm_getcsr() & SIM_MXCSR_MODE) die("MXCSR changed during the run");
    if (d->trace) {
        fprintf(d->trace, "hash\t%016llx\n", (unsigned long long)d->hash);
        fclose(d->trace);
    }
    if (d->camera) fclose(d->camera);
    printf("ticks %d hash %016llx", tick, (unsigned long long)d->hash);
#if FZX_DEFER_PROJECTION
    printf(" deferrals %u builds %u", ref_camera_deferrals, ref_camera_deferred_builds);
#endif
    printf(" restart %d\n", restart_at);
    return 0;
}
