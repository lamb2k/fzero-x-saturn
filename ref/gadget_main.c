/* Course gadget dump, host side: loads a course record through the original code and writes every gadget
 * the original draws as text lines (the world-space data from gadget_bake.c). HOST only, ROM-derived output:
 * local, never committed.
 *   fzx-gadgets --record RECORD --rom ROM --out FILE
 * Lines: S segs chunks | C i seg t x y z info | G i seg radL radR shape len start | F i type seg t lat dx dy dz chunk
 *        L i 5x(x y z s t) | J i 6x(...) | E i seg type t0 t1 a b nvtx | V i k x y z s t chunk | D d pos basis scale */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ref.h"

void gb_view_init(void);
int gb_segment_count(void), gb_chunk_count(void), gb_feature_count(void), gb_landmine_count(void), gb_jump_count(void);
void gb_chunk(int, float[6]);
void gb_segment(int, float[5]);
void gb_feature(int, float[8]);
void gb_decoration(int, float[13]);
void gb_landmine_vtx(int, float[25]);
void gb_jump_vtx(int, float[30]);
int gb_effect_count(void), gb_effect_vtx_count(int);
void gb_effect(int, float[6]);
void gb_effect_vtx(int, int, float[6]);

static unsigned char* read_file(const char* p, size_t* n) {
    FILE* f = fopen(p, "rb");
    unsigned char* b;
    if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(2); }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*n); if (fread(b, 1, *n, f) != *n) exit(2); fclose(f); return b;
}

int main(int argc, char** argv) {
    const char *rec = 0, *rom = 0, *out = 0;
    int i, k, d = 0;
    size_t n;
    FILE* o;
    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--record")) rec = argv[i + 1];
        else if (!strcmp(argv[i], "--rom")) rom = argv[i + 1];
        else if (!strcmp(argv[i], "--out")) out = argv[i + 1];
    }
    if (!rec || !rom || !out) { fprintf(stderr, "usage: fzx-gadgets --record R --rom ROM --out FILE\n"); return 2; }
    unsigned char* r = read_file(rec, &n);
    if (n != REF_COURSE_RECORD_SIZE) { fprintf(stderr, "record must be 0x7E0 bytes\n"); return 2; }
    unsigned char* rm = read_file(rom, &n);
    if (n < REF_ROM_BOOT_SIZE) return 2;
    ref_set_rom_boot(rm);
    if (ref_load_course(r, 1) != 0 || ref_race_init() != 0) { fprintf(stderr, "race set-up failed\n"); return 3; }
    gb_view_init();
    o = fopen(out, "w");
    fprintf(o, "S %d %d\n", gb_segment_count(), gb_chunk_count());
    for (i = 0; i < gb_chunk_count(); i++) { float c[6]; gb_chunk(i, c); fprintf(o, "C %d %.0f %.6f %.3f %.3f %.3f %.0f\n", i, c[0], c[1], c[2], c[3], c[4], c[5]); }
    for (i = 0; i < gb_segment_count(); i++) { float c[5]; gb_segment(i, c); fprintf(o, "G %d %.3f %.3f %.0f %.3f %.3f\n", i, c[0], c[1], c[2], c[3], c[4]); }
    for (i = 0; i < gb_feature_count(); i++) {
        float f[8]; gb_feature(i, f);
        fprintf(o, "F %d %.0f %.0f %.6f %.3f %.3f %.3f %.3f %.0f\n", i, f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7]);
        if (f[7] >= 0) { float c[13]; gb_decoration(d++, c); fprintf(o, "D %d", i); for (k = 0; k < 13; k++) fprintf(o, " %.4f", c[k]); fprintf(o, "\n"); }
    }
    for (i = 0; i < gb_landmine_count(); i++) { float v[25]; gb_landmine_vtx(i, v); fprintf(o, "L %d", i); for (k = 0; k < 25; k++) fprintf(o, " %.0f", v[k]); fprintf(o, "\n"); }
    for (i = 0; i < gb_jump_count(); i++) { float v[30]; gb_jump_vtx(i, v); fprintf(o, "J %d", i); for (k = 0; k < 30; k++) fprintf(o, " %.0f", v[k]); fprintf(o, "\n"); }
    for (i = 0; i < gb_effect_count(); i++) {
        float e[6]; int nv = gb_effect_vtx_count(i); gb_effect(i, e);
        fprintf(o, "E %d %.0f %.0f %.6f %.6f %.3f %.3f %d\n", i, e[0], e[1], e[2], e[3], e[4], e[5], nv);
        for (k = 0; k < nv; k++) { float v[6]; gb_effect_vtx(i, k, v); fprintf(o, "V %d %d %.0f %.0f %.0f %.0f %.0f %.0f\n", i, k, v[0], v[1], v[2], v[3], v[4], v[5]); }
    }
    fclose(o);
    return 0;
}
