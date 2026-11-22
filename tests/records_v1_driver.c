/* HOST driver (tests/test_modes.py): the records service reads a version 1 save (before cup progress) with ENG_MODES=1, keeps its
 * times, writes version 2 afterwards, and refuses a version 1 file whose checksum is wrong. The backup library is a stub: files
 * live in memory. Prints one PASS/FAIL line per check. */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <bup.h>
#include "engine_records.h"

#define V1_BYTES ((uint32_t)offsetof(fzx_records_t, progress))
static struct { char name[16]; uint8_t data[sizeof(fzx_records_t)]; uint32_t size; int used; } files[4];
void bup_init(bup_t *b, bup_devices_t *d) { (void)b; (void)d; }
bup_status_t bup_read(int dev, const char *name, void *buf) {
    (void)dev;
    for (int i = 0; i < 4; ++i) if (files[i].used && !strcmp(files[i].name, name)) { memcpy(buf, files[i].data, files[i].size); return BUP_OK; }
    return BUP_FAILED;
}
int bup_dir(int dev, const char *name, int n, bup_directory_t *d) {
    (void)dev; (void)n; (void)d;
    for (int i = 0; i < 4; ++i) if (files[i].used && !strcmp(files[i].name, name)) return 1;
    return 0;
}
bup_status_t bup_write(int dev, const bup_directory_t *dir, const void *buf, int mode) {
    (void)dev; (void)mode;
    for (int i = 0; i < 4; ++i)
        if (!files[i].used || !strcmp(files[i].name, dir->filename)) {
            files[i].used = 1; strcpy(files[i].name, dir->filename); files[i].size = dir->data_size; memcpy(files[i].data, buf, dir->data_size);
            return BUP_OK;
        }
    return BUP_NOT_ENOUGH_MEMORY;
}
bup_status_t bup_status(int dev, uint32_t size, bup_stat_t *st) { (void)dev; (void)size; st->free_blocks = 100; st->block_size = 64; return BUP_OK; }
void engine_smpc_quiet(bool on) { (void)on; }

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (unsigned k = 0; k < 8u; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static int bad;
static void check(int ok, const char *what) { printf("%s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) bad = 1; }

/* A version 1 image: a defaults table with course 0's best time set, magic FZXR, version 1, V1_BYTES long. */
static void put_v1(const char *name, uint32_t gen, int corrupt) {
    fzx_records_t r;
    memset(&r, 0, sizeof(r));
    for (unsigned c = 0; c < FZX_REC_COURSES; ++c) {
        for (unsigned i = 0; i < FZX_REC_TOP; ++i) { r.ta[c].top[i].ms = FZX_REC_MAX_TIMER; r.ta[c].top[i].slider_bits = FZX_REC_SLIDER_DEFAULT; }
        r.ta[c].best_lap.ms = FZX_REC_MAX_TIMER; r.death_race[c].ms = FZX_REC_MAX_TIMER;
    }
    r.ta[0].top[0].ms = 104040u; r.ta[0].top[0].name[0] = 'A'; r.ta[0].max_speed = 123456u; r.death_race[3].ms = 99999u;
    r.magic = 0x465A5852u; r.version = 1u; r.generation = gen; r.boot_id = 7u; r.bytes = V1_BYTES;
    r.crc = crc32((const uint8_t *)&r + offsetof(fzx_records_t, crc) + 4u, V1_BYTES - (offsetof(fzx_records_t, crc) + 4u));
    if (corrupt) r.crc ^= 1u;
    int i = 0; while (files[i].used) ++i;
    files[i].used = 1; strcpy(files[i].name, name); files[i].size = V1_BYTES; memcpy(files[i].data, &r, V1_BYTES);
}

int main(void) {
    put_v1("FZXREC_A", 5u, 0);
    check(fzx_records_load() == FZX_REC_OK, "version 1 save loads");
    const fzx_records_t *t = fzx_records_get();
    check(t->ta[0].top[0].ms == 104040u && t->ta[0].top[0].name[0] == 'A' && t->ta[0].max_speed == 123456u && t->death_race[3].ms == 99999u,
          "version 1 records kept (time, name, top speed, death race)");
    check(fzx_records_unlock_level() == 0u && !fzx_records_class_open(3u) && !fzx_records_credits_due(), "progress zero after a version 1 load");
    check(fzx_rec_report[1] == 1u && fzx_rec_report[2] == 5u, "file A generation 5 loaded");
    /* The load saved at once: version 2, the other file, generation 6. */
    int v2 = 0;
    for (int i = 0; i < 4; ++i) if (files[i].used && !strcmp(files[i].name, "FZXREC_B")) v2 = files[i].size == sizeof(fzx_records_t) && ((uint32_t *)files[i].data)[1] == 2u;
    check(v2, "the next save is version 2");
    check(fzx_records_cup_clear(1u, 0u, 3u) == FZX_REC_OK && fzx_records_cup_clear(1u, 1u, 3u) == FZX_REC_OK && fzx_records_cup_clear(1u, 2u, 3u) == FZX_REC_OK
              && fzx_records_unlock_level() == 1u, "three cups cleared on Standard: level 1");
    check(fzx_records_load() == FZX_REC_OK && fzx_records_get()->ta[0].top[0].ms == 104040u && fzx_records_unlock_level() == 1u, "reload: records and progress both survive");
    /* Control: a version 1 file with a wrong checksum is not valid; the table is the defaults. */
    memset(files, 0, sizeof(files));
    put_v1("FZXREC_A", 5u, 1);
    check(fzx_records_load() != FZX_REC_OK && fzx_records_get()->ta[0].top[0].ms == FZX_REC_MAX_TIMER, "control: a corrupt version 1 file is refused");
    return bad;
}
