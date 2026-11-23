/* Host test of saturn/iso_root.h: the scenes image's root-directory reader. Synthetic volume only.
 * The fixture has a record that ends exactly on a sector boundary; the old cdfs reader lost that file
 * (Sector Beta, RLOD14.PAK). A model of that behaviour proves the fixture can fail. */
#include "../saturn/iso_root.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static void require(int c, const char *e, int line) {
    ++checks;
    if (!c) { fprintf(stderr, "FAIL %d: %s\n", line, e); exit(EXIT_FAILURE); }
}
#define CHECK(C) require(!!(C), #C, __LINE__)

enum { SECTORS = 8, ROOT_LBA = 18 };
static uint8_t vol[(ROOT_LBA + SECTORS) * FZX_ISO_SECTOR];
static unsigned cur[2];        /* write cursor: sector, offset */
static unsigned boundary_ends; /* records that end exactly at a sector end */

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void add(const char *name, uint32_t lba, uint32_t size, int dir, int raw) {
    const unsigned nl = raw ? 1u : (unsigned)strlen(name);
    unsigned len = 33u + nl; len += len & 1u;
    if (cur[1] + len > FZX_ISO_SECTOR) { ++cur[0]; cur[1] = 0; }
    uint8_t *r = vol + (ROOT_LBA + cur[0]) * FZX_ISO_SECTOR + cur[1];
    r[0] = (uint8_t)len; put32(r + 2, lba); put32(r + 10, size); r[25] = dir ? 2u : 0u; r[32] = (uint8_t)nl;
    if (raw) r[33] = (uint8_t)name[0]; else memcpy(r + 33, name, nl);
    cur[1] += len;
    if (cur[1] == FZX_ISO_SECTOR) ++boundary_ends;
}
static void build(void) {
    memset(vol, 0, sizeof vol); cur[0] = cur[1] = 0; boundary_ends = 0;
    uint8_t *pvd = vol + 16 * FZX_ISO_SECTOR;
    pvd[0] = 1; memcpy(pvd + 1, "CD001", 5);
    put32(pvd + 158, ROOT_LBA); put32(pvd + 166, SECTORS * FZX_ISO_SECTOR);
    add("\0", ROOT_LBA, SECTORS * FZX_ISO_SECTOR, 1, 1);
    add("\1", ROOT_LBA, SECTORS * FZX_ISO_SECTOR, 1, 1);
    char n[32];
    for (unsigned i = 0; i < 42; ++i) { snprintf(n, sizeof n, "F%06u.PAK;1", i); add(n, 100 + i, 2048u * (i + 1u), 0, 0); }
    add("MACH3D05.PAK;1", 400, 4096, 0, 0);   /* 14 characters: record length 48, ends sector 0 exactly */
    add("RLOD14.PAK;1", 500, 8192, 0, 0);     /* first record of sector 1 */
    for (unsigned i = 0; i < 20; ++i) { snprintf(n, sizeof n, "G%d.PAK;1", (int)i); add(n, 600 + i, 2048, 0, 0); }
    add("SUB;1", 900, 2048, 1, 0);
}
static void rd(uint32_t lba, uint8_t *s, void *ctx) { (void)ctx; memcpy(s, vol + lba * FZX_ISO_SECTOR, FZX_ISO_SECTOR); }
typedef struct { fzx_iso_rec_t rec[128]; unsigned n; } list_t;
static int emit(const fzx_iso_rec_t *r, void *ctx) {
    list_t *l = ctx; l->rec[l->n++] = *r; return 1;
}
static int emit_stop(const fzx_iso_rec_t *r, void *ctx) { (void)r; ++*(unsigned *)ctx; return *(unsigned *)ctx < 3; }

/* The old reader, as observed: a record that ends exactly on the sector's end is not listed. */
static unsigned old_reader_count(void) {
    unsigned count = 0;
    for (unsigned s = 0; s < SECTORS; ++s) {
        const uint8_t *d = vol + (ROOT_LBA + s) * FZX_ISO_SECTOR; unsigned o = 0;
        while (o < FZX_ISO_SECTOR && d[o]) {
            const unsigned len = d[o], nl = d[o + 32];
            const int dot = nl == 1 && d[o + 33] <= 1;
            if (!dot && o + len != FZX_ISO_SECTOR) ++count;
            o += len;
        }
    }
    return count;
}

int main(void) {
    static list_t L;
    fzx_iso_rec_t *const all = L.rec;
    build();
    CHECK(boundary_ends == 1);                       /* the fixture has the hazard */
    uint8_t sec[FZX_ISO_SECTOR];
    L.n = 0;
    const int n = fzx_iso_root_walk(rd, NULL, sec, emit, &L);
    CHECK(n == 42 + 1 + 1 + 20 + 1);                 /* every record, '.' and '..' excluded */
    CHECK(old_reader_count() == (unsigned)n - 1u);   /* control: the old behaviour loses exactly one */
    unsigned found = 0;
    for (int i = 0; i < n; ++i) {
        if (!strcmp(all[i].name, "MACH3D05.PAK")) { ++found; CHECK(all[i].lba == 400 && all[i].size == 4096 && !all[i].dir); }
        if (!strcmp(all[i].name, "RLOD14.PAK")) { ++found; CHECK(all[i].lba == 500 && all[i].size == 8192 && all[i].name_len == 10); }
        if (!strcmp(all[i].name, "SUB")) { ++found; CHECK(all[i].dir && all[i].lba == 900); }
    }
    CHECK(found == 3);
    CHECK(!strcmp(all[0].name, "F000000.PAK") && all[0].lba == 100);
    unsigned stop = 0;
    CHECK(fzx_iso_root_walk(rd, NULL, sec, emit_stop, &stop) == 3);   /* emit returning 0 stops the walk */
    vol[16 * FZX_ISO_SECTOR + 3] = 'X';                                /* not a volume descriptor */
    CHECK(fzx_iso_root_walk(rd, NULL, sec, emit, &L) == -1);
    build(); L.n = 0; vol[(ROOT_LBA) * FZX_ISO_SECTOR + 68 + 32] = 200;          /* name longer than the record */
    CHECK(fzx_iso_root_walk(rd, NULL, sec, emit, &L) == -1);
    printf("iso_root: %u checks ok\n", checks);
    return 0;
}
