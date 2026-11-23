/* HOST fixture (tests/test_modes.py): the Time Attack ghost's backup storage (saturn/engine/engine_records.c, ENG_GHOST=1). A save is a
 * queue of backup-RAM files (chunk files, the head file last); the checks: every write is at most one chunk, the head is written last
 * and once, a ghost saved by pumping reads back equal, the two slots alternate, a power cut after any number of writes leaves the
 * previous ghost or the whole new one and never a mixture, the fault controls (wrong CRC, a save stopped half way) keep the previous
 * ghost, and a ghost in the first format (one file) still reads. The backup library is a stub: files live in memory.
 * usage: ghost_store_driver   (prints one PASS/FAIL line per check) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <bup.h>
#include "engine_ghost.h"
#include "engine_records.h"

#define NFILES 80
static struct { char name[16]; uint8_t data[FZX_GHOST_HEAD_BYTES + FZX_GHOST_STREAM_MAX + 64]; uint32_t size; int used; } files[NFILES];
static uint32_t write_log[64][2]; static int n_writes;   /* [0] data size, [1] 1 if the file is a slot's head (no digit suffix) */
void bup_init(bup_t *b, bup_devices_t *d) { (void)b; (void)d; }
bup_status_t bup_read(int dev, const char *name, void *buf) {
    (void)dev;
    for (int i = 0; i < NFILES; ++i) if (files[i].used && !strcmp(files[i].name, name)) { memcpy(buf, files[i].data, files[i].size); return BUP_OK; }
    return BUP_FAILED;
}
int bup_dir(int dev, const char *name, int n, bup_directory_t *d) {
    (void)dev; (void)n;
    for (int i = 0; i < NFILES; ++i) if (files[i].used && !strcmp(files[i].name, name)) { d->data_size = files[i].size; return 1; }
    return 0;
}
bup_status_t bup_write(int dev, const bup_directory_t *dir, const void *buf, int mode) {
    (void)dev; (void)mode;
    if (dir->data_size > sizeof(files[0].data)) return BUP_FAILED;
    for (int i = 0; i < NFILES; ++i)
        if (!files[i].used || !strcmp(files[i].name, dir->filename)) {
            files[i].used = 1; strcpy(files[i].name, dir->filename); files[i].size = dir->data_size; memcpy(files[i].data, buf, dir->data_size);
            if (n_writes < 64) { write_log[n_writes][0] = dir->data_size; write_log[n_writes][1] = strlen(dir->filename) == 8; ++n_writes; }
            return BUP_OK;
        }
    return BUP_NOT_ENOUGH_MEMORY;
}
bup_status_t bup_status(int dev, uint32_t size, bup_stat_t *st) { (void)dev; (void)size; st->free_blocks = 100; st->block_size = 64; return BUP_OK; }
void engine_smpc_quiet(bool on) { (void)on; }
uint16_t cpu_frt_count_get(void) { return 0; }
uint32_t video_fields_get(void) { return 0; }

static int bad;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) bad = 1; }

/* A recorded ghost of about `ticks` ticks, race time `ms` (the course 3). */
static void make_ghost(fzx_ghost_t *g, unsigned ticks, uint32_t ms, uint32_t seed) {
    fzx_ghost_rec_begin(g, 3u);
    uint32_t s = seed; double x = 0, z = 0, vx = 1, vz = 1;
    for (unsigned t = 0; t < ticks; ++t) {
        s = s * 1664525u + 1013904223u;
        vx += ((int)(s >> 24) - 128) / 300.0; vz += ((int)((s >> 16) & 255) - 128) / 300.0;
        x += vx / 2; z += vz / 2;
        int32_t q[3] = { (int32_t)(x * 65536.0), 0, (int32_t)(z * 65536.0) };
        fzx_ghost_rec_tick(g, q);
    }
    if (!fzx_ghost_rec_end(g, ms)) { printf("FAIL recording refused\n"); exit(1); }
}
static fzx_ghost_t g1, g2, got;

static void save_all(const fzx_ghost_t *g, int *pumps) {
    (void)fzx_records_ghost_save_begin(g);
    int n = 0;
    while (fzx_records_ghost_pending()) { (void)fzx_records_ghost_pump(); ++n; }
    *pumps = n;
}

int main(void) {
    make_ghost(&g1, 6200u, 104040u, 1u);
    make_ghost(&g2, 6000u, 103000u, 99u);
    const uint32_t size1 = FZX_GHOST_FILE_BYTES(&g1);
    int pumps;
    check(!fzx_records_ghost_load(&got), "no ghost on a fresh card");
    n_writes = 0;
    save_all(&g1, &pumps);
    const int chunks = (int)((size1 + 1023u) / 1024u);
    check(pumps == chunks && n_writes == chunks, "a save is one write per chunk file and the head (every write counted)");
    int over = 0, heads = 0;
    for (int i = 0; i < n_writes; ++i) { over |= write_log[i][0] > 1024u; heads += (int)write_log[i][1]; }
    check(!over, "every backup write is at most one chunk (1024 bytes)");
    check(heads == 1 && write_log[n_writes - 1][1] == 1u, "the head file is written once, last (the commit)");
    check(fzx_ghost_store[11] == 1u && fzx_ghost_store[13] == 1u && fzx_ghost_store[5] == FZX_REC_OK, "one save begun, one completed, result OK");
    check(fzx_records_ghost_load(&got) && got.race_ms == 104040u && got.generation == 1u && !memcmp(&got, &g1, 8) && fzx_ghost_valid(&got, size1) &&
          !memcmp(got.stream, g1.stream, g1.bytes), "a pumped save reads back equal (generation 1)");

    /* The second save goes to the other slot; a third back to the first. */
    save_all(&g2, &pumps);
    check(fzx_records_ghost_load(&got) && got.race_ms == 103000u && got.generation == 2u && fzx_ghost_store[2] == 2u, "second save: slot B, generation 2");
    save_all(&g1, &pumps);
    check(fzx_records_ghost_load(&got) && got.generation == 3u && fzx_ghost_store[2] == 1u, "third save: slot A again, generation 3");

    /* Power cut after k writes of a save of g2 over [A=g1 gen 3, B=g2 gen 2]: the load must give gen 3 (g1) until the head lands. */
    static struct { char name[16]; uint8_t data[FZX_GHOST_HEAD_BYTES + FZX_GHOST_STREAM_MAX + 64]; uint32_t size; int used; } snap[NFILES];
    memcpy(snap, files, sizeof(files));
    const uint32_t size2 = FZX_GHOST_FILE_BYTES(&g2);
    const int chunks2 = (int)((size2 + 1023u) / 1024u);
    int cuts_ok = 0, cuts = 0, saw_old = 0, saw_new = 0;
    for (int k = 0; k <= chunks2; ++k) {
        const pid_t pid = fork();
        if (pid == 0) {
            memcpy(files, snap, sizeof(files));
            fzx_records_ghost_load(&got);                 /* the boot: the slots' generations */
            (void)fzx_records_ghost_save_begin(&g2);
            for (int i = 0; i < k && fzx_records_ghost_pending(); ++i) (void)fzx_records_ghost_pump();
            const int ok = fzx_records_ghost_load(&got) && fzx_ghost_valid(&got, FZX_GHOST_FILE_BYTES(&got)) &&
                           ((got.generation == 3u && got.race_ms == 104040u) || (got.generation == 4u && got.race_ms == 103000u && !memcmp(got.stream, g2.stream, g2.bytes)));
            _exit(ok ? (got.generation == 4u ? 11 : 10) : 1);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        const int rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        ++cuts; cuts_ok += rc == 10 || rc == 11; saw_old += rc == 10; saw_new += rc == 11;
    }
    check(cuts_ok == cuts && saw_new == 1 && saw_old == cuts - 1, "a power cut after any number of writes: the previous ghost, whole, until the head lands, then the new one");

    /* Each scenario below runs in a child (the queue is static state a power cut would discard): restore the card, run, report 0 / 1. */
#define SCENARIO(what, body) do { const pid_t p_ = fork(); if (p_ == 0) { memcpy(files, snap, sizeof(files)); (void)fzx_records_ghost_load(&got); int ok_ = 0; body; _exit(ok_ ? 0 : 1); }         int st_ = 0; waitpid(p_, &st_, 0); check(WIFEXITED(st_) && WEXITSTATUS(st_) == 0, what); } while (0)

    SCENARIO("control: the target slot, half rewritten (three new chunk files, the old head), is not a valid ghost; the other slot is", {
        (void)fzx_records_ghost_save_begin(&g1);   /* a different image from the one slot B holds (g2) */
        for (int i = 0; i < 3 && fzx_records_ghost_pending(); ++i) (void)fzx_records_ghost_pump();
        (void)fzx_records_ghost_load(&got);
        ok_ = fzx_ghost_store[3] == 1u && fzx_ghost_store[4] == 0u;   /* A (generation 3) valid, B (the target, generation 2) not */
    });
    SCENARIO("control: a wrong-CRC save is refused, the previous ghost stands", {
        fzx_ghost_fault = 1;
        int n; save_all(&g2, &n);
        ok_ = fzx_records_ghost_load(&got) && got.generation == 3u && got.race_ms == 104040u;
    });
    SCENARIO("control: a save stopped half way, the head never written: the previous ghost stands, the result is failed", {
        fzx_ghost_fault = 2;
        n_writes = 0;
        int n; save_all(&g2, &n);
        ok_ = fzx_records_ghost_load(&got) && got.generation == 3u && got.race_ms == 104040u && fzx_ghost_store[5] == FZX_REC_FAILED && n_writes < chunks2;
    });
    SCENARIO("control: every chunk file written but not the head: still the previous ghost", {
        (void)fzx_records_ghost_save_begin(&g2);
        int w = 0;
        while (fzx_records_ghost_pending() && w < chunks2 - 1) { (void)fzx_records_ghost_pump(); ++w; }
        ok_ = fzx_records_ghost_load(&got) && got.generation == 3u;
    });
    SCENARIO("control: a chunk file of the newest slot one byte short: that slot is refused, the other (older) ghost is loaded", {
        for (int i = 0; i < NFILES; ++i) if (files[i].used && !strcmp(files[i].name, "FZXGHO_A02")) files[i].size -= 1u;
        ok_ = fzx_records_ghost_load(&got) && got.generation == 2u && fzx_ghost_store[3] == 0u;
    });
    SCENARIO("a second save begun while one is queued finishes the first (one buffer)", {
        (void)fzx_records_ghost_save_begin(&g2);
        (void)fzx_records_ghost_pump();
        (void)fzx_records_ghost_save_begin(&g1);
        int n = 0; while (fzx_records_ghost_pending()) { (void)fzx_records_ghost_pump(); ++n; }
        ok_ = fzx_records_ghost_load(&got) && got.race_ms == 104040u && got.generation == 5u;
    });

    /* The first format: one file with the whole image reads as before. */
    SCENARIO("a ghost in the first format (one file) still reads", {
        memset(files, 0, sizeof(files));
        fzx_ghost_t old = g1;
        fzx_ghost_stamp(&old, 5u);
        files[0].used = 1; strcpy(files[0].name, "FZXGHO_B"); files[0].size = FZX_GHOST_FILE_BYTES(&old); memcpy(files[0].data, &old, files[0].size);
        ok_ = fzx_records_ghost_load(&got) && got.generation == 5u && fzx_ghost_store[2] == 2u;
    });
    return bad;
}
