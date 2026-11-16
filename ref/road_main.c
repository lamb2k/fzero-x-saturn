/* HOST driver for the bounded offline source course exporter. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ref.h"

extern int ref_road_export(void);
extern void ref_road_boot(void);
extern void ref_road_outer(int enabled);

void road_out_counts(int chunks, int vertices, int commands, int spans) {
    printf("FZXROAD2 %d %d %d %d\n", chunks, vertices, commands, spans);
}
void road_out_span(int id, int chunk, int first, int end, int vfirst, int vend) {
    printf("R %d %d %d %d %d %d\n", id, chunk, first, end, vfirst, vend);
}
void road_out_chunk(int i, unsigned int flags, int segment, const short* p) {
    int k;
    printf("C %d %08x %d", i, flags, segment);
    for (k = 0; k < 24; ++k) printf(" %d", p[k]);
    puts("");
}
void road_out_vertex(int i, const short* p, const short* uv, const unsigned char* c) {
    printf("V %d %d %d %d %d %d %u %u %u %u\n", i, p[0], p[1], p[2],
           uv[0], uv[1], c[0], c[1], c[2], c[3]);
}
void road_out_command(unsigned int a, unsigned int b, const char* name) {
    printf("G %08x %08x %s\n", a, b, name ? name : "-");
}
int main(int argc, char** argv) {
    unsigned char record[REF_COURSE_RECORD_SIZE];
    FILE* f;
    int status;
    if (argc != 2 && (argc != 3 || strcmp(argv[2], "--interior"))) return 2;
    if (!(f = fopen(argv[1], "rb"))) return 2;
    if (fread(record, 1, sizeof(record), f) != sizeof(record) || fgetc(f) != EOF) return 2;
    fclose(f);
    ref_road_boot();
    if (ref_load_course(record, 1) || ref_race_init()) return 3;
    ref_road_outer(argc == 2);
    status = ref_road_export();
    if (status) fprintf(stderr, "source road export rejected: %d\n", status);
    return status;
}
