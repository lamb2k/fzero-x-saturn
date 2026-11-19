/* G1.2 reference harness driver: the original race code, built natively in float.
 * MIT; see ../LICENSE. Host headers only; the game code is behind ref.h.
 *
 *   fzx-ref --course RECORD --byte-order big|little --inputs SCRIPT [--rom ROM] [--ticks N] [--trace OUT]
 *           [--prefix SCRIPT] [--cpu 1]
 *
 * --cpu 1 hands racer 0 to the original CPU driver (as the title demo does): SCRIPT's buttons are
 * still read but the CPU steers.
 *
 * --prefix runs that script first, untraced, then restarts the race (ref_race_restart) and
 * traces SCRIPT from tick 0: the hash must equal a fresh run of SCRIPT.
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref.h"
#if FZX_BACKDROP_STATE
#include "backdrop_state.h"
#include "skyline_state.h"
#include "scene_probe.h"
int ref_backdrop_check(RefBackdrop*,unsigned int,int);
int ref_backdrop_branch_check(void);
int ref_skyline_check(RefSkyline*,unsigned int);
int ref_skyline_branch_check(void);
static FILE *backdrop_trace;
static FILE *skyline_trace;
static void backdrop_gate(int tick) {
    RefBackdrop packet;
    RefSkyline skyline;
    RefState before,after;
    RefCamera camera_before,camera_after;
    unsigned int i;
    ref_state(&before);ref_camera(&camera_before);
    if(tick==0 && (ref_backdrop_branch_check() || ref_skyline_branch_check())) {
        fprintf(stderr,"backdrop branch control failed\n");exit(3);
    }
    /* Repeat pool selections and paused presentation frames independently of
     * the running simulation, to cover retained source vertex-pool contents. */
    const int result=ref_backdrop_check(&packet,((unsigned int)tick/3)&1,tick%31==7);
    const int skyline_result=ref_skyline_check(&skyline,((unsigned int)tick/3)&1);
    ref_state(&after);ref_camera(&camera_after);
    if(result || skyline_result || memcmp(&before,&after,sizeof(before)) ||
       memcmp(&camera_before,&camera_after,sizeof(camera_before))) {
        fprintf(stderr,"backdrop failed at tick %d: %d skyline %d\n",tick,result,skyline_result);exit(3);
    }
    if(backdrop_trace) {
        fprintf(backdrop_trace,"%d",tick);
        for(i=0;i<sizeof(packet)/4;++i) {
            uint32_t word;memcpy(&word,(const unsigned char*)&packet+4*i,4);
            fprintf(backdrop_trace," %08x",word);
        }
        fputc('\n',backdrop_trace);
    }
    if(skyline_trace) {
        fprintf(skyline_trace,"%d",tick);
        for(i=0;i<sizeof(skyline)/4;++i) {
            uint32_t word;memcpy(&word,(const unsigned char*)&skyline+4*i,4);
            fprintf(skyline_trace," %08x",word);
        }
        fputc('\n',skyline_trace);
    }
}
#endif
#if FZX_ROAD_PLAN
#include "road_plan.h"
#ifndef FZX_ENGINE_DUMP
#define FZX_ENGINE_DUMP 0
#endif
#if FZX_ENGINE_DUMP
#include "engine_dump.h"
#endif
int ref_road_plan_check(RefRoadPlan *out,unsigned int draw_frame,const unsigned char fog[3]);
int ref_road_plan_branch_check(void);
static FILE *road_plan_trace;
static unsigned char road_plan_fog[3]={203,137,61};
static void road_plan_gate(int tick) {
    RefRoadPlan plan;
    RefState before,after;
    RefCamera camera_before,camera_after;
    unsigned int i;
    ref_state(&before); ref_camera(&camera_before);
    if(tick==0 && ref_road_plan_branch_check()) {
        fprintf(stderr,"road plan branch control failed\n");exit(3);
    }
    /* Deliberately unequal synthetic channels exercise the source transition
     * math. These are a test input, never a substitute retail environment. */
    const int result=ref_road_plan_check(&plan,(unsigned int)tick,road_plan_fog);
    ref_state(&after); ref_camera(&camera_after);
    if(result || memcmp(&before,&after,sizeof(before)) ||
       memcmp(&camera_before,&camera_after,sizeof(camera_before))) {
        fprintf(stderr,"road plan failed at tick %d: %d\n",tick,result);exit(3);
    }
    if(road_plan_trace) {
        fprintf(road_plan_trace,"%d %u %u %u %d",tick,plan.count,plan.visible_chunks,plan.outer,plan.fog_start);
        for(i=0;i<4;++i)fprintf(road_plan_trace," %u",plan.fog[i]);
        for(i=0;i<4;++i)fprintf(road_plan_trace," %u",plan.primitive[i]);
        for(i=0;i<plan.count;++i) {
            unsigned int depth;memcpy(&depth,&plan.groups[i].average_depth,4);
            fprintf(road_plan_trace," %u,%u,%d,%08x",plan.groups[i].start,plan.groups[i].end,plan.groups[i].direction,depth);
        }
        fputc('\n',road_plan_trace);
    }
}
#endif

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
#if FZX_BACKDROP_STATE
    if(argc==2 && strcmp(argv[1],"--scene-probe")==0) {
        unsigned int frame,hashes[2];
        if(ref_scene_probe_init())return 3;
        for(frame=0;frame<REF_SCENE_PROBE_CASES;++frame) {
            if(ref_scene_probe_step(frame,hashes))return 3;
            printf("%u %08x %08x\n",frame,hashes[0],hashes[1]);
        }
        return 0;
    }
    if(argc==2 && strcmp(argv[1],"--backdrop-catchup")==0) {
        /* FZX_BACKDROP_CATCHUP: catch-up spans 1..8 against the original source,
         * every probe camera (including the cloud-at-eye-height case), paused
         * frames, both pools, and a pass that crosses the scroll wrap points. */
        unsigned int pass,frame,checked=0,ticks=0;
        if(ref_scene_probe_init())return 3;
        for(pass=0;pass<2;++pass)for(frame=0;frame<REF_SCENE_PROBE_CASES;++frame) {
            const unsigned int span=frame%8u+1u;
            const int r=ref_scene_probe_catchup(pass,frame,span,frame%13u==5u);
            if(r){printf("catch-up mismatch pass %u frame %u span %u: %d\n",pass,frame,span,r);return 4;}
            ++checked;ticks+=span;
        }
        printf("catch-up identical: %u catch-ups, %u ticks\n",checked,ticks);
        return 0;
    }
#endif
    const char *course = NULL, *order = NULL, *inputs = NULL, *trace = NULL, *rom = NULL, *prefix = NULL;
#if FZX_SOURCE_CAMERA
    const char* camera_path = NULL;
    FILE* camera_out = NULL;
#endif
    static Step steps[16384];
    static unsigned char record[REF_COURSE_RECORD_SIZE];
    int nsteps, i, tick = 0, limit = -1, cpu = 0;
    FILE* f;
    FILE* out = NULL;

    for (i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--course") == 0) course = argv[i + 1];
        else if (strcmp(argv[i], "--byte-order") == 0) order = argv[i + 1];
        else if (strcmp(argv[i], "--inputs") == 0) inputs = argv[i + 1];
        else if (strcmp(argv[i], "--trace") == 0) trace = argv[i + 1];
        else if (strcmp(argv[i], "--rom") == 0) rom = argv[i + 1];
        else if (strcmp(argv[i], "--ticks") == 0) limit = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--prefix") == 0) prefix = argv[i + 1];
        else if (strcmp(argv[i], "--cpu") == 0) cpu = atoi(argv[i + 1]);
#if FZX_SOURCE_CAMERA
        else if (strcmp(argv[i], "--camera-trace") == 0) camera_path = argv[i + 1];
#endif
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
    nsteps = load_script(inputs, steps, 16384);
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
    if (prefix != NULL) {
        static Step pre[16384];
        const int npre = load_script(prefix, pre, 16384);
        int t;
        for (i = 0; i < npre; i++) {
            for (t = 0; t < pre[i].ticks; t++) ref_tick(pre[i].button, pre[i].sx, pre[i].sy);
        }
        ref_race_restart();
    }
    if (cpu) {
        ref_set_cpu_driver(1);
    }
    if (trace != NULL && (out = fopen(trace, "w")) == NULL) {
        die("cannot write trace");
    }
    if (out != NULL) {
        fprintf(out, "# fzx-trace v1\n# tick\tintro\tbutton\tsx\tsy\tposx\tposy\tposz\tvelx\tvely\tvelz\tspeed\tenergy"
                     "\tlapdist\tsegt\theight\tsegment\tlap\tlapsdone\tracetime\tflags\tboost\tx\ty\tz\tspeed_f\n");
    }
#if FZX_SOURCE_CAMERA
    if (camera_path && !(camera_out = fopen(camera_path, "w"))) die("cannot write camera trace");
#endif
#if FZX_ROAD_PLAN
    if(getenv("REF_ROAD_PLAN_FOG")) {
        unsigned int r,g,b;char tail;
        if(sscanf(getenv("REF_ROAD_PLAN_FOG"),"%u,%u,%u%c",&r,&g,&b,&tail)!=3 || r>255 || g>255 || b>255)
            die("invalid road environment color");
        road_plan_fog[0]=(unsigned char)r;road_plan_fog[1]=(unsigned char)g;road_plan_fog[2]=(unsigned char)b;
    }
    if(getenv("REF_ROAD_PLAN_TRACE") && !(road_plan_trace=fopen(getenv("REF_ROAD_PLAN_TRACE"),"w")))
        die("cannot write road plan trace");
#endif
#if FZX_BACKDROP_STATE
    if(getenv("REF_BACKDROP_TRACE") && !(backdrop_trace=fopen(getenv("REF_BACKDROP_TRACE"),"w")))
        die("cannot write backdrop trace");
    if(getenv("REF_SKYLINE_TRACE") && !(skyline_trace=fopen(getenv("REF_SKYLINE_TRACE"),"w")))
        die("cannot write skyline trace");
#endif
    for (i = 0; i < nsteps && (limit < 0 || tick < limit); i++) {
        int t;
        for (t = 0; t < steps[i].ticks && (limit < 0 || tick < limit); t++) {
            ref_tick(steps[i].button, steps[i].sx, steps[i].sy);
            trace_tick(out, tick, &steps[i]);
#if FZX_BACKDROP_STATE
            backdrop_gate(tick);
#endif
#if FZX_ROAD_PLAN
            road_plan_gate(tick);
#endif
#if FZX_SOURCE_CAMERA
            RefCamera camera;
            unsigned camera_index;
            if (ref_camera_count() != 2) die("Time Attack camera count changed");
            for (camera_index = 0; camera_index < ref_camera_count(); ++camera_index) {
                unsigned j;
                if (!ref_camera_read(camera_index, &camera) || camera.faults)
                    die("unmapped source camera viewport");
                /* RefCamera has four leading integers, then 58 binary32 values. */
                for (j = 4; j < 62; ++j) {
                    float value;
                    memcpy(&value, (const unsigned char*)&camera + 4*j, 4);
                    if (!isfinite(value)) die("nonfinite source camera value");
                }
                if (camera_out) {
                    fprintf(camera_out, "%d %u", tick, camera_index);
                    for (j = 0; j < sizeof(camera) / 4; ++j) {
                        uint32_t word;
                        memcpy(&word, (const unsigned char*)&camera + 4*j, 4);
                        fprintf(camera_out, " %08x", word);
                    }
                    fputc('\n', camera_out);
                }
            }
#endif
            tick++;
        }
    }
    if (out != NULL) {
        fprintf(out, "hash\t%016llx\n", (unsigned long long) hash);
        fclose(out);
    }
#if FZX_SOURCE_CAMERA
    if (camera_out) fclose(camera_out);
    if (getenv("REF_STUB_CALL_REPORT")) {
        extern void ref_stub_call_report(const char* path);
        ref_stub_call_report(getenv("REF_STUB_CALL_REPORT"));
    }
#endif
    if (ref_stub_report != NULL && getenv("REF_STUB_REPORT") != NULL) {
        ref_stub_report(getenv("REF_STUB_REPORT"));
    }
    printf("ticks %d hash %016llx\n", tick, (unsigned long long) hash);
#if FZX_BACKDROP_STATE
    if(backdrop_trace)fclose(backdrop_trace);
    if(skyline_trace)fclose(skyline_trace);
#endif
#if FZX_ROAD_PLAN
    if(road_plan_trace)fclose(road_plan_trace);
#endif
#if FZX_ENGINE_DUMP
    if(getenv("REF_ENGINE_COURSE")) { /* the course ring and constants, as fzx-sim --engine-course */
        FILE *e=fopen(getenv("REF_ENGINE_COURSE"),"w");
        float c[REF_ENGINE_CONSTANTS];
        unsigned n=ref_engine_chunk_count(),k,j;
        uint32_t word;
        if(!e || !n) die("cannot write the engine course dump");
        ref_engine_constants(c);
        fprintf(e,"constants");
        for(j=0;j<REF_ENGINE_CONSTANTS;++j) { memcpy(&word,&c[j],4); fprintf(e," %08x",word); }
        fprintf(e,"%cchunks %u%c",10,n,10);
        for(k=0;k<n;++k) {
            float p[3];
            ref_engine_chunk(k,p);
            for(j=0;j<3;++j) { memcpy(&word,&p[j],4); fprintf(e,j?" %08x":"%08x",word); }
            fputc(10,e);
        }
        fclose(e);
    }
#endif
    return 0;
}
