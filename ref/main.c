/* G1.2 reference harness driver: the original race code, built natively in float.
 * MIT; see ../LICENSE. Host headers only; the game code is behind ref.h.
 *
 *   fzx-ref --course RECORD --byte-order big|little --inputs SCRIPT [--rom ROM] [--ticks N] [--trace OUT]
 *
 * ROM is the owner's F-Zero X ROM in any byte order; only its first 4 KB are
 * read (the machine constants). Without it the CI fixture machine is used.
 * RECORD is one 0x7E0-byte CourseDataDisk record (synthetic in CI, from the
 * owner's ROM locally). SCRIPT holds lines "<ticks> <buttons> [stick_x stick_y]",
 * buttons being "-" or names joined by "+": A B Z S(tart) L R CU CD CL CR.
 * The bridge runs the race start as Race_Init does (course, then racer); each
 * tick it reads the pad through the original Controller_UpdateInputs and steps
 * the original Racer_Update. The harness is the judge for the fixed-point
 * port, never a component of the Saturn or web builds.
 *
 * Trace format v1: a header, one tab-separated line per tick (floats as exact
 * binary32 hex, then a few in decimal for reading), and a final "hash" line:
 * FNV-1a 64 over every traced value and input. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref.h"

static uint64_t hash = 0xCBF29CE484222325ull;

/* Generated in stubs.c (gen_stubs.py): reports how much of each data stand-in was used. */
void ref_stub_report(const char* path) __attribute__((weak));

static void mix(const void* data, size_t size) {
    const unsigned char* p = data;
    while (size--) {
        hash = (hash ^ *p++) * 0x100000001B3ull;
    }
}

static void die(const char* message) {
    fprintf(stderr, "fzx-ref: %s\n", message);
    exit(2);
}

typedef struct {
    int ticks;
    unsigned short button;
    signed char sx, sy;
} Step;

static unsigned short parse_buttons(const char* text) {
    static const struct {
        const char* name;
        unsigned short mask;
    } names[] = { { "A", 0x8000 }, { "B", 0x4000 }, { "Z", 0x2000 }, { "S", 0x1000 }, { "L", 0x0020 },
                  { "R", 0x0010 }, { "CU", 0x0008 }, { "CD", 0x0004 }, { "CL", 0x0002 }, { "CR", 0x0001 } };
    char copy[64];
    char* token;
    unsigned short mask = 0;

    if (strcmp(text, "-") == 0) {
        return 0;
    }
    snprintf(copy, sizeof(copy), "%s", text);
    for (token = strtok(copy, "+"); token != NULL; token = strtok(NULL, "+")) {
        size_t i, n = sizeof(names) / sizeof(names[0]);
        for (i = 0; i < n && strcmp(token, names[i].name) != 0; i++) {
        }
        if (i == n) {
            die("unknown button in input script");
        }
        mask |= names[i].mask;
    }
    return mask;
}

static int load_script(const char* path, Step* steps, int capacity) {
    FILE* f = fopen(path, "r");
    char line[256];
    int count = 0;

    if (f == NULL) {
        die("cannot open input script");
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char buttons[64];
        int ticks, sx = 0, sy = 0, n;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        n = sscanf(line, "%d %63s %d %d", &ticks, buttons, &sx, &sy);
        if (n < 2 || ticks <= 0 || sx < -128 || sx > 127 || sy < -128 || sy > 127 || count == capacity) {
            die("bad input script line");
        }
        steps[count].ticks = ticks;
        steps[count].button = parse_buttons(buttons);
        steps[count].sx = (signed char) sx;
        steps[count].sy = (signed char) sy;
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

static void trace_tick(FILE* out, int tick, const Step* in) {
    RefState s;

    ref_state(&s);
    mix(&tick, sizeof(tick));
    mix(&in->button, sizeof(in->button));
    mix(&in->sx, 1);
    mix(&in->sy, 1);
    mix(&s, sizeof(s));
    if (out != NULL) {
        fprintf(out,
                "%d\t%d\t%04x\t%d\t%d\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x"
                "\t%d\t%d\t%d\t%d\t%08x\t%d\t%.3f\t%.3f\t%.3f\t%.3f\n",
                tick, s.intro, in->button, in->sx, in->sy, bits(s.pos[0]), bits(s.pos[1]), bits(s.pos[2]),
                bits(s.vel[0]), bits(s.vel[1]), bits(s.vel[2]), bits(s.speed), bits(s.energy),
                bits(s.lap_distance), bits(s.seg_t), bits(s.height), s.segment, s.lap, s.laps_completed,
                s.race_time, (uint32_t) s.state_flags, s.boost_timer, s.pos[0], s.pos[1], s.pos[2], s.speed);
    }
}

int main(int argc, char** argv) {
    const char *course = NULL, *order = NULL, *inputs = NULL, *trace = NULL, *rom = NULL;
    static Step steps[4096];
    static unsigned char record[REF_COURSE_RECORD_SIZE];
    int nsteps, i, tick = 0, limit = -1;
    FILE* f;
    FILE* out = NULL;

    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--course") == 0) course = argv[i + 1];
        else if (strcmp(argv[i], "--byte-order") == 0) order = argv[i + 1];
        else if (strcmp(argv[i], "--inputs") == 0) inputs = argv[i + 1];
        else if (strcmp(argv[i], "--trace") == 0) trace = argv[i + 1];
        else if (strcmp(argv[i], "--rom") == 0) rom = argv[i + 1];
        else if (strcmp(argv[i], "--ticks") == 0) limit = atoi(argv[i + 1]);
        else die("unknown option");
    }
    if (course == NULL || order == NULL || inputs == NULL ||
        (strcmp(order, "big") != 0 && strcmp(order, "little") != 0)) {
        die("usage: fzx-ref --course RECORD --byte-order big|little --inputs SCRIPT [--rom ROM] [--ticks N] [--trace OUT]");
    }
    f = fopen(course, "rb");
    if (f == NULL || fread(record, 1, sizeof(record), f) != sizeof(record) || fgetc(f) != EOF) {
        die("course record must be exactly 0x7E0 bytes");
    }
    fclose(f);
    nsteps = load_script(inputs, steps, 4096);
    if (rom != NULL) {
        static unsigned char boot[REF_ROM_BOOT_SIZE];
        int k;
        f = fopen(rom, "rb");
        if (f == NULL || fread(boot, 1, sizeof(boot), f) != sizeof(boot)) {
            die("cannot read the ROM");
        }
        fclose(f);
        /* Normalise to big-endian (z64) from the header magic. */
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
    if (ref_load_course(record, strcmp(order, "big") == 0) != 0 || ref_race_init() != 0) {
        die("race set-up failed: the original loader did not request the course record");
    }
    if (trace != NULL && (out = fopen(trace, "w")) == NULL) {
        die("cannot write trace");
    }
    if (out != NULL) {
        fprintf(out, "# fzx-trace v1\n# tick\tintro\tbutton\tsx\tsy\tposx\tposy\tposz\tvelx\tvely\tvelz\tspeed\tenergy"
                     "\tlapdist\tsegt\theight\tsegment\tlap\tlapsdone\tracetime\tflags\tboost\tx\ty\tz\tspeed_f\n");
    }
    for (i = 0; i < nsteps && (limit < 0 || tick < limit); i++) {
        int t;
        for (t = 0; t < steps[i].ticks && (limit < 0 || tick < limit); t++) {
            ref_tick(steps[i].button, steps[i].sx, steps[i].sy);
            trace_tick(out, tick, &steps[i]);
            tick++;
        }
    }
    if (out != NULL) {
        fprintf(out, "hash\t%016llx\n", (unsigned long long) hash);
        fclose(out);
    }
    if (ref_stub_report != NULL && getenv("REF_STUB_REPORT") != NULL) {
        ref_stub_report(getenv("REF_STUB_REPORT"));
    }
    printf("ticks %d hash %016llx\n", tick, (unsigned long long) hash);
    return 0;
}
