/* Float reference harness (card G1.2). MIT; see ../LICENSE.
 *
 * Steps the decomp's own one-racer race code, compiled natively in binary32 float
 * (-m32 -ffp-contract=off, no fast-math), and writes a per-tick trace. It is the judge
 * for the fixed-point core, never a component of the Saturn or web builds.
 *
 * Usage: ref --course RECORD.bin --script INPUTS.txt --trace OUT.tsv [--ticks N] [--machine N]
 *            [--rom ROM | --rom-word HEX]
 *   --rom       retail machine tuning from the ROM's first 4 KB (ROM lane only)
 *   --rom-word  the fixture's authored tuning word (default REF_FIXTURE_ROM_WORD)
 *   RECORD.bin  one 0x7E0-byte CourseDataDisk record, big-endian as stored in the ROM
 *   INPUTS.txt  lines "<ticks> <buttons-hex> <stickX> <stickY>"; '#' starts a comment
 * A script stick_x of 99 means "auto": a centre-line bot steers (see bot_steer) and
 * --record OUT.txt writes the inputs actually used, as a plain script with no 99s.
 * Prints "ref: hash <16 hex>" (the hash of every tick's state hash) and a stub report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ref.h"
#include "ref_stubs.h"


typedef struct {
    uint32_t ticks;
    uint16_t buttons;
    int8_t stick_x, stick_y;
    float target; /* lateral target for auto steering (optional 5th column, default 0) */
} ScriptStep;

static uint64_t fnv(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = data;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }
    return h;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static uint16_t be16(const uint8_t* p) { return (uint16_t) ((p[0] << 8) | p[1]); }
static void put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
static void put16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }

/* Big-endian CourseDataDisk -> host order, field by field (fzx_course.h layout). */
static int load_course(const char* path) {
    uint8_t be[REF_COURSE_RECORD_SIZE];
    FILE* f = fopen(path, "rb");
    if (f == NULL || fread(be, 1, sizeof(be), f) != sizeof(be) || fgetc(f) != EOF) {
        fprintf(stderr, "ref: %s is not one 0x7E0-byte course record\n", path);
        if (f) fclose(f);
        return -1;
    }
    fclose(f);
    memcpy(ref_course_record, be, sizeof(be));
    put32(ref_course_record + 0x004, be32(be + 0x004));
    for (int i = 0; i < 64; ++i) {
        uint8_t* cp = ref_course_record + 0x20 + i * 0x14;
        const uint8_t* src = be + 0x20 + i * 0x14;
        put32(cp + 0x0, be32(src + 0x0));
        put32(cp + 0x4, be32(src + 0x4));
        put32(cp + 0x8, be32(src + 0x8));
        put16(cp + 0xC, be16(src + 0xC));
        put16(cp + 0xE, be16(src + 0xE));
        put32(cp + 0x10, be32(src + 0x10));
        put16(ref_course_record + 0x520 + i * 2, be16(be + 0x520 + i * 2));
    }
    return 0;
}

/* The first 4 KB of a ROM, normalised to big-endian (z64) order from z64, v64 or n64.
 * Only Racer_InitMachineStats reads it. The ROM's path and bytes never leave this process. */
static int load_rom_head(const char* path) {
    unsigned char head[REF_ROM_HEAD_BYTES];
    FILE* f = fopen(path, "rb");
    if (f == NULL || fread(head, 1, sizeof(head), f) != sizeof(head)) {
        fprintf(stderr, "ref: cannot read the ROM header\n");
        if (f) fclose(f);
        return -1;
    }
    fclose(f);
    for (unsigned i = 0; i < sizeof(head); i += 4) {
        unsigned char a = head[i], b = head[i + 1], c = head[i + 2], d = head[i + 3];
        if (head[0] == 0x80) { /* z64 */
        } else if (head[0] == 0x37) { /* v64: 16-bit byte swap */
            head[i] = b; head[i + 1] = a; head[i + 2] = d; head[i + 3] = c;
        } else if (head[0] == 0x40) { /* n64: 32-bit little-endian */
            head[i] = d; head[i + 1] = c; head[i + 2] = b; head[i + 3] = a;
        } else {
            fprintf(stderr, "ref: not an N64 ROM image\n");
            return -1;
        }
    }
    memcpy(ref_rom_head, head, sizeof(head));
    ref_rom_loaded = 1;
    return 0;
}

static int load_script(const char* path, ScriptStep* steps, int max) {
    FILE* f = fopen(path, "r");
    char line[256];
    int n = 0;
    if (f == NULL) {
        fprintf(stderr, "ref: cannot read %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        unsigned ticks, buttons;
        int sx, sy;
        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';
        float target = 0.0f;
        if (sscanf(line, "%u %x %d %d %f", &ticks, &buttons, &sx, &sy, &target) < 4) continue;
        if (n == max || ((sx < -80 || sx > 80) && sx != REF_AUTO_STEER) || sy < -80 || sy > 80 ||
            buttons > 0xFFFF) {
            fprintf(stderr, "ref: bad script line: %s", line);
            fclose(f);
            return -1;
        }
        steps[n++] = (ScriptStep) { ticks, (uint16_t) buttons, (int8_t) sx, (int8_t) sy, target };
    }
    fclose(f);
    return n;
}

static uint32_t bits(float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    return u;
}

/* Centre-line bot: steer against the offset from the spline and the velocity's angle to it,
 * both measured along the track's side axis (racer up x spline tangent), so climbs, banks
 * and loops are handled. Used only to author input scripts; the gate never runs it. */
static float bot_sign = 1.0f;
static float bot_gain_offset = 0.5f, bot_gain_heading = 60.0f;
static signed char bot_steer(const RefSnapshot* s, float target) {
    const float* f = s->seg_forward;
    const float* u = s->up;
    float side[3] = { u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0] };
    float sl = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
    float vl = sqrtf(s->vel[0] * s->vel[0] + s->vel[1] * s->vel[1] + s->vel[2] * s->vel[2]);
    if (sl == 0.0f) return 0;
    float lateral = (side[0] * s->seg_disp[0] + side[1] * s->seg_disp[1] + side[2] * s->seg_disp[2]) / sl;
    float heading = vl > 0.0f ? (side[0] * s->vel[0] + side[1] * s->vel[1] + side[2] * s->vel[2]) / (sl * vl) : 0.0f;
    float c = bot_sign * (bot_gain_offset * (lateral - target) + bot_gain_heading * heading);
    if (c > 80.0f) c = 80.0f;
    if (c < -80.0f) c = -80.0f;
    return (signed char) lrintf(c);
}

int main(int argc, char** argv) {
    const char *course = NULL, *script = NULL, *trace = NULL, *rom = NULL, *record = NULL;
    FILE* rec = NULL;
    int stop_at_finish = 0; /* --stop-at-finish 1: end after the tick the racer finishes */
    signed char auto_x = 0;
    int run_buttons = -1, run_x = 0, run_y = 0;
    long run_len = 0;
    long max_ticks = -1;
    int machine = 0;
    uint64_t course_hash = 0;
    int course_segments = 0;
    static ScriptStep steps[4096];
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--course")) course = argv[i + 1];
        else if (!strcmp(argv[i], "--script")) script = argv[i + 1];
        else if (!strcmp(argv[i], "--trace")) trace = argv[i + 1];
        else if (!strcmp(argv[i], "--ticks")) max_ticks = strtol(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--machine")) machine = (int) strtol(argv[i + 1], NULL, 10);
        else if (!strcmp(argv[i], "--rom-word")) ref_fixture_rom_word = (unsigned) strtoul(argv[i + 1], NULL, 16);
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--record")) record = argv[i + 1];
        else if (!strcmp(argv[i], "--stop-at-finish")) stop_at_finish = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--bot")) sscanf(argv[i + 1], "%f,%f,%f", &bot_sign, &bot_gain_offset, &bot_gain_heading);
    }
    if (!course || !script || !trace) {
        fprintf(stderr, "usage: ref --course RECORD.bin --script INPUTS.txt --trace OUT.tsv [--ticks N]\n");
        return 2;
    }
    int nsteps = load_script(script, steps, 4096);
    if (load_course(course) != 0 || nsteps < 0) return 2;
    if (rom != NULL && load_rom_head(rom) != 0) return 2;

    ref_game_init(machine);
    {
        /* The OFFLINE course values go into the trace header and the run hash. */
        float tension[64], length[64];
        int n = ref_game_course_values(tension, length, 64);
        uint64_t ch = fnv(0xCBF29CE484222325ull, tension, sizeof(float) * (size_t) (n < 64 ? n : 64));
        ch = fnv(ch, length, sizeof(float) * (size_t) (n < 64 ? n : 64));
        course_hash = ch;
        course_segments = n;
        for (int i = 0; i < n && i < 64; ++i) {
            fprintf(stderr, "ref: segment %d tension %08x length %08x\n", i, bits(tension[i]), bits(length[i]));
        }
    }

    FILE* out = fopen(trace, "w");
    if (out == NULL) {
        fprintf(stderr, "ref: cannot write %s\n", trace);
        return 2;
    }
    fprintf(out, "# fzx reference trace v%d\n", REF_TRACE_FORMAT);
    fprintf(out, "# course segments %d hash %016llx\n", course_segments, (unsigned long long) course_hash);
    fprintf(out, "tick\tbuttons\tstick_x\tstick_y\tstate_flags\tsegment\tt\tpos_x\tpos_y\tpos_z"
                 "\tvel_x\tvel_y\tvel_z\tspeed\tenergy\tlap\tlaps_completed\trace_distance\thash\n");

    uint64_t run = fnv(0xCBF29CE484222325ull, &course_hash, sizeof(course_hash));
    long tick = 0;
    if (record != NULL && (rec = fopen(record, "w")) == NULL) {
        fprintf(stderr, "ref: cannot write %s\n", record);
        return 2;
    }
    if (rec) fprintf(rec, "# Recorded by ref from %s (auto steering resolved).\n", script);
    for (int s = 0; s < nsteps; ++s) {
        for (uint32_t k = 0; k < steps[s].ticks; ++k) {
            if (max_ticks >= 0 && tick >= max_ticks) goto done;
            RefSnapshot snap;
            signed char sx = steps[s].stick_x == REF_AUTO_STEER ? auto_x : steps[s].stick_x;
            if (rec) {
                if (steps[s].buttons == run_buttons && sx == run_x && steps[s].stick_y == run_y) {
                    ++run_len;
                } else {
                    if (run_len) fprintf(rec, "%ld %04x %d %d\n", run_len, run_buttons, run_x, run_y);
                    run_buttons = steps[s].buttons, run_x = sx, run_y = steps[s].stick_y, run_len = 1;
                }
            }
            ref_game_tick(steps[s].buttons, sx, steps[s].stick_y);
            ref_game_snapshot(&snap);
            auto_x = bot_steer(&snap, steps[s].target);
            uint64_t h = fnv(0xCBF29CE484222325ull, snap.racer, snap.racer_bytes);
            h = fnv(h, &snap.segment, sizeof(snap.segment));
            run = fnv(run, &h, sizeof(h));
            fprintf(out,
                    "%ld\t%04x\t%d\t%d\t%08x\t%d\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x\t%08x"
                    "\t%08x\t%08x\t%d\t%d\t%08x\t%016llx\n",
                    tick, steps[s].buttons, sx, steps[s].stick_y,
                    (unsigned) snap.state_flags, snap.segment, bits(snap.t), bits(snap.pos[0]),
                    bits(snap.pos[1]), bits(snap.pos[2]), bits(snap.vel[0]), bits(snap.vel[1]),
                    bits(snap.vel[2]), bits(snap.speed), bits(snap.energy), snap.lap,
                    snap.laps_completed, bits(snap.race_distance), (unsigned long long) h);
            ++tick;
            if (stop_at_finish && (snap.state_flags & (1u << 25))) goto done; /* RACER_STATE_FINISHED */
        }
    }
done:
    fclose(out);
    if (rec) {
        if (run_len) fprintf(rec, "%ld %04x %d %d\n", run_len, run_buttons, run_x, run_y);
        fclose(rec);
    }
    printf("ref: ticks %ld hash %016llx\n", tick, (unsigned long long) run);
    fprintf(stderr, "ref: course loads %u, other DMA %u\n", ref_dma_course_loads, ref_dma_other_loads);
    int cpu_drove = 0;
    for (uint32_t i = 0; i < ref_stub_count; ++i) {
        if (ref_stub_calls[i]) fprintf(stderr, "ref: stub %s called %u times\n", ref_stub_names[i], ref_stub_calls[i]);
        if (ref_stub_calls[i] && !strcmp(ref_stub_names[i], "Cpu_GenerateInputs")) cpu_drove = 1;
    }
    /* After the finish (or for a CPU racer) the game steers through Cpu_GenerateInputs, which is
     * a stub here: whatever it leaves is not the original behaviour. Such a run is not valid. */
    if (cpu_drove) {
        fprintf(stderr, "ref: INVALID: the CPU-input stub drove the racer; end the script at the finish\n");
        return 3;
    }
    return 0;
}
