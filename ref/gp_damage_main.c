/* Damage sources in the reference Grand Prix (attacks lane): the original code with 30 racers (as
 * ref/gp_ref_main.c), each racer's energy losses attributed to their source every tick. HOST reference
 * role only. MIT; see ../LICENSE.
 *
 *   fzx-gp-damage --course RECORD --rom BOOT --cpu-line LINE --difficulty D --seed N [--ticks MAX]
 *
 * A loss is attributed from the state at the start of the tick (the contact pass runs before anyone
 * moves): to an attack when another active racer was within 46.5 and either of the two was attacking,
 * to a contact when one was within 46.5 and neither attacked, to a wall otherwise when the racer was
 * marked as damaged (walls, mines and edges; the first course has no mines), and to the boost when
 * the racer was boosting and the loss is its boost cost. For every spin-out (energy empty) the source
 * of that tick's loss is kept. Prints one JSON object. Derived from the owner's ROM: local only. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ref.h"

int rb_gp_init(int difficulty, unsigned seed, const short* cpu_line);
void rb_gp_frame(void);
void dp_racer(int i, float f[5], int n[4]);

#define MAXR 30
#define ST_RECEIVED_DAMAGE (1 << 17)
#define ST_RETIRED (1 << 18)
#define ST_ACTIVE (1 << 30)

static void die(const char* m) { fprintf(stderr, "fzx-gp-damage: %s\n", m); exit(2); }
static unsigned char* read_file(const char* path, size_t* n) {
    FILE* f = fopen(path, "rb");
    if (!f) die("cannot open input");
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char* b = malloc(*n ? *n : 1);
    if (!b || fread(b, 1, *n, f) != *n) die("short read");
    fclose(f);
    return b;
}

enum { SRC_ATTACK, SRC_CONTACT, SRC_WALL, SRC_OTHER, SRC_N };
static const char* const NAMES[SRC_N] = { "attack", "contact", "wall", "other" };

int main(int argc, char** argv) {
    const char *course = 0, *rom = 0, *line = 0;
    int difficulty = 1, max_ticks = 14000;
    unsigned seed = 1;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--course")) course = argv[i + 1];
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--cpu-line")) line = argv[i + 1];
        else if (!strcmp(argv[i], "--difficulty")) difficulty = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--seed")) seed = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--ticks")) max_ticks = atoi(argv[i + 1]);
        else die("unknown argument");
    }
    if (!course || !rom || !line) die("usage: see gp_damage_main.c");
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
    if (rb_gp_init(difficulty, seed, cpu_line) != MAXR) die("the reference did not set up 30 racers");
    ref_set_cpu_driver(1);

    static float pf[MAXR][5], cf[MAXR][5];
    static int pn[MAXR][4], cn[MAXR][4];
    double amount[SRC_N] = { 0 }, amount_player[SRC_N] = { 0 };
    int incidents[SRC_N] = { 0 }, spin_by[SRC_N] = { 0 }, spin_far[SRC_N] = { 0 }, retired = 0, spins = 0;
    for (int i = 0; i < MAXR; ++i) dp_racer(i, pf[i], pn[i]);
    for (int t = 0; t < max_ticks; ++t) {
        rb_gp_frame();
        ref_tick(0, 0, 0);
        int done = 0;
        for (int i = 0; i < MAXR; ++i) dp_racer(i, cf[i], cn[i]);
        for (int i = 0; i < MAXR; ++i) {
            const float loss = pf[i][3] - cf[i][3];
            const float boost_cost = pf[i][4] * 0.0015f;
            int src = -1;
            if (loss > 1e-4f && !(pn[i][1] & ST_RETIRED)) {
                int near = 0, atk = 0;
                for (int j = 0; j < MAXR; ++j) {
                    if (j == i || !(pn[j][1] & ST_ACTIVE)) continue;
                    const float dx = pf[j][0] - pf[i][0], dy = pf[j][1] - pf[i][1], dz = pf[j][2] - pf[i][2];
                    if (dx * dx + dy * dy + dz * dz <= 46.5f * 46.5f) { near = 1; if (pn[i][0] || pn[j][0]) atk = 1; }
                }
                if (cn[i][2] > 0 && fabsf(loss - boost_cost) < 1e-3f) src = -2;   /* the boost's own cost */
                else if (near) src = atk ? SRC_ATTACK : SRC_CONTACT;
                else if (cn[i][1] & ST_RECEIVED_DAMAGE) src = SRC_WALL;
                else src = SRC_OTHER;
                if (src >= 0) {
                    amount[src] += loss; incidents[src]++;
                    if (i == 0) amount_player[src] += loss;
                }
            }
            if (cn[i][3] && !pn[i][3]) {
                ++spins;
                const int s = src >= 0 ? src : SRC_OTHER;
                spin_by[s]++;
                (void)spin_far;
            }
            if ((cn[i][1] & ST_RETIRED) && !(pn[i][1] & ST_RETIRED)) ++retired;
            done += (cn[i][1] & ((1 << 25) | ST_RETIRED)) != 0;
        }
        memcpy(pf, cf, sizeof(pf)); memcpy(pn, cn, sizeof(pn));
        if (done >= MAXR) break;
    }
    printf("{\"difficulty\": %d, \"seed\": %u, \"retired\": %d, \"spin_outs\": %d, \"spin_outs_by\": {", difficulty, seed, retired, spins);
    for (int s = 0; s < SRC_N; ++s) printf("%s\"%s\": %d", s ? ", " : "", NAMES[s], spin_by[s]);
    printf("}, \"damage\": {");
    for (int s = 0; s < SRC_N; ++s) printf("%s\"%s\": [%d, %.1f, %.1f]", s ? ", " : "", NAMES[s], incidents[s], amount[s], amount_player[s]);
    printf("}}\n");
    return 0;
}
