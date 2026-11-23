/* Engine records service (ENG_SCENES=1): see engine_records.h and SCENES.md section 5. */
#include <stddef.h>
#include <string.h>
#include <yaul.h>
#include <bup.h>
#include "engine_records.h"
#ifndef ENG_MODES
#define ENG_MODES 0
#endif
#if ENG_MODES
#include "engine_modes.h"
#endif
#ifndef ENG_GHOST
#define ENG_GHOST 0
#endif
#if ENG_GHOST
#include "engine_ghost.h"
#endif

#if ENG_GHOST
extern uint32_t video_fields_get(void);
/* Captures (32-bit: the FRT counters wrap past 4.6 fields): per backup write, in order (the first 12): video fields at entry and at exit,
 * the bytes, 1 for a records file or 2 for a ghost file, and the FRT at entry and exit. [0] writes so far. */
volatile uint32_t fzx_bup_log[1 + 5 * 12];
static void bup_log(uint32_t f0, uint16_t t0, uint32_t bytes, uint32_t kind) {
    const uint32_t n = fzx_bup_log[0];
    if (n < 12u) {
        fzx_bup_log[1u + 5u * n] = f0; fzx_bup_log[2u + 5u * n] = video_fields_get(); fzx_bup_log[3u + 5u * n] = bytes; fzx_bup_log[4u + 5u * n] = kind;
        fzx_bup_log[5u + 5u * n] = ((uint32_t)t0 << 16) | cpu_frt_count_get();   /* FRT at entry, at exit (16 bits each) */
    }
    fzx_bup_log[0] = n + 1u;
}
#endif
#define REC_MAGIC 0x465A5852u /* "FZXR" */
#if ENG_MODES
#define REC_VERSION 2u
#else
#define REC_VERSION 1u
#endif
#define RECENT 8u

volatile uint32_t fzx_rec_report[16];
volatile uint32_t fzx_rec_fault;
#define POKED(w) (*(volatile uint32_t *)((uintptr_t)&(w) | CPU_CACHE_THROUGH))

static bup_t bup __attribute__((aligned(4)));
static bup_devices_t devices;
static bool bup_ready;
#if ENG_LOW_COLD
/* Read and written only by the scenes (select, results, records), never during a race frame: low work RAM
 * (engine_low.x). `io` and `bup` stay in high work RAM: the backup library reads and writes them and has not run
 * with its buffers in low work RAM on hardware. */
__attribute__((section(".bss.lowcold.records_table")))
#endif
static fzx_records_t table;          /* the RAM table (what the screens read) */
static fzx_records_t io;            /* one file's image for reads and writes */
static uint32_t file_gen[2];         /* generation of each file, 0 = absent or invalid */
static bool dirty;
static uint32_t recent[RECENT], recent_at;
static const char *const file_name[2] = { "FZXREC_A", "FZXREC_B" };

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (unsigned k = 0; k < 8u; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
#if ENG_MODES
#define REC_V1_BYTES ((uint32_t)offsetof(fzx_records_t, progress))
#endif
static uint32_t body_crc_n(const fzx_records_t *r, uint32_t bytes) {
    const uint8_t *p = (const uint8_t *)r;
    const uint32_t skip = (uint32_t)offsetof(fzx_records_t, crc) + 4u;
    return crc32(p + skip, bytes - skip);
}
static uint32_t body_crc(const fzx_records_t *r) { return body_crc_n(r, (uint32_t)sizeof(*r)); }

static void empty_time(fzx_rec_time_t *t) {
    memset(t, 0, sizeof(*t));
    t->ms = FZX_REC_MAX_TIMER;
    t->slider_bits = FZX_REC_SLIDER_DEFAULT;
}
static void defaults(fzx_records_t *r) {
    memset(r, 0, sizeof(*r));
    r->magic = REC_MAGIC; r->version = REC_VERSION; r->bytes = sizeof(*r);
    for (unsigned c = 0; c < FZX_REC_COURSES; ++c) {
        for (unsigned i = 0; i < FZX_REC_TOP; ++i) empty_time(&r->ta[c].top[i]);
        empty_time(&r->ta[c].best_lap);
        empty_time(&r->death_race[c]);
    }
}
static bool valid(const fzx_records_t *r) {
#if ENG_MODES
    /* Version 1 files (before cup progress) stay readable: their bytes end where progress begins. */
    if (r->magic == REC_MAGIC && r->version == 1u && r->bytes == REC_V1_BYTES && r->generation &&
        r->crc == body_crc_n(r, REC_V1_BYTES)) return true;
#endif
    return r->magic == REC_MAGIC && r->version == REC_VERSION && r->bytes == sizeof(*r) && r->generation &&
           r->crc == body_crc(r);
}

/* The backup library's calls that issue SMPC commands from this thread (bup_status and bup_write wrap their BIOS
 * call in RESDISA/RESENAB, bup.h; bup_init is the library's own) run inside a quiet SMPC: engine_smpc_quiet
 * (engine_main.c) keeps them apart from the INTBACK the vertical blank issues every field. */
static void ensure_bup(void) {
    if (bup_ready) return;
    engine_smpc_quiet(true);
    bup_init(&bup, &devices);
    engine_smpc_quiet(false);
    bup_ready = true;
}

/* Reads one file into `io`; true when it is present and valid. */
static bool read_file(unsigned i) {
    memset(&io, 0, sizeof(io));
    const bup_status_t s = bup_read(BUP_DEVICE_INTERNAL, file_name[i], &io);
    return s == BUP_OK && valid(&io);
}

static fzx_rec_result_t write_table(void) {
    ensure_bup();
    /* The older (or missing, or bad) file takes the write; the other stays the previous save. */
    const unsigned target = file_gen[0] <= file_gen[1] ? 0u : 1u;
    const uint32_t gen = (file_gen[0] > file_gen[1] ? file_gen[0] : file_gen[1]) + 1u;
    io = table;
    io.magic = REC_MAGIC; io.version = REC_VERSION; io.bytes = sizeof(io); io.generation = gen;
    io.crc = body_crc(&io);
    uint32_t size = sizeof(io);
    const uint32_t fault = POKED(fzx_rec_fault);
    if (fault == 1u) io.crc ^= 0x00010000u;          /* control: a bad CRC */
    else if (fault == 2u) size = sizeof(io) / 2u;    /* control: a write cut in half */
    POKED(fzx_rec_fault) = 0;
    bup_directory_t dir;
    memset(&dir, 0, sizeof(dir));
    strcpy(dir.filename, file_name[target]);
    strcpy(dir.comment, "FZX RECORD");
    dir.language = BUP_LANG_ENGLISH;
    dir.data_size = size;
    engine_smpc_quiet(true);
#if ENG_GHOST
    const uint16_t w0 = cpu_frt_count_get();
    const uint32_t bf0 = video_fields_get();
#endif
    const bup_status_t s = bup_write(BUP_DEVICE_INTERNAL, &dir, &io, BUP_WRITE_MODE_OVERWRITE);
#if ENG_GHOST
    fzx_rec_report[14] = (uint16_t)(cpu_frt_count_get() - w0); /* FRT ticks the write blocked the caller (a menu frame) */
    bup_log(bf0, w0, size, 1u);
#endif
    engine_smpc_quiet(false);
    fzx_rec_report[7] = s;
    fzx_rec_report[5] = target + 1u;
    if (s == BUP_NOT_ENOUGH_MEMORY) return FZX_REC_FULL;
    if (s != BUP_OK) return FZX_REC_FAILED;
    /* A write that stored a bad file (the controls) counts as interrupted: the file is
     * not a valid generation, the table stays dirty. */
    file_gen[target] = fault ? 0u : gen;
    table.generation = gen;
    if (fault) return FZX_REC_FAILED;
    dirty = false;
    return FZX_REC_OK;
}

fzx_rec_result_t fzx_records_load(void) {
    ensure_bup();
    defaults(&table);
    file_gen[0] = file_gen[1] = 0;
    bool present = false;
    unsigned best = 2u;
    for (unsigned i = 0; i < 2u; ++i) {
        bup_directory_t d[1];
        if (bup_dir(BUP_DEVICE_INTERNAL, file_name[i], 1, d) > 0) present = true;
        if (read_file(i)) {
            file_gen[i] = io.generation;
            if (best == 2u || io.generation > file_gen[best]) best = i;
        }
        fzx_rec_report[8u + i] = file_gen[i] ? 1u : 0u;
    }
    fzx_rec_result_t r;
    uint32_t boot = 0;
    if (best < 2u) {
        (void)read_file(best);
        table = io;
#if ENG_MODES
        table.version = REC_VERSION; table.bytes = sizeof(table); /* a version 1 file: progress is zero (read_file clears io) */
#endif
        boot = io.boot_id;
        r = FZX_REC_OK;
    } else r = present ? FZX_REC_FAILED : FZX_REC_ABSENT;
    fzx_rec_report[1] = best < 2u ? best + 1u : 0u;
    fzx_rec_report[2] = best < 2u ? file_gen[best] : 0u;
    /* A new boot id, saved at once (run ids never repeat across boots). */
    table.boot_id = boot + 1u;
    fzx_rec_report[3] = table.boot_id;
    {
        bup_stat_t st;
        memset(&st, 0, sizeof(st));
        engine_smpc_quiet(true);
        (void)bup_status(BUP_DEVICE_INTERNAL, sizeof(table), &st);
        engine_smpc_quiet(false);
        fzx_rec_report[12] = st.free_blocks; fzx_rec_report[13] = st.block_size;
    }
    dirty = true;
    const fzx_rec_result_t w = write_table();
    if (w == FZX_REC_FULL && r != FZX_REC_FAILED) r = FZX_REC_FULL;
    fzx_rec_report[0] = r;
    fzx_rec_report[6] = dirty;
    return r;
}

uint32_t fzx_records_boot_id(void) { return table.boot_id; }
const fzx_records_t *fzx_records_get(void) { return &table; }
const fzx_rec_course_t *fzx_records_course(uint32_t course) {
    return course < FZX_REC_COURSES ? &table.ta[course] : NULL;
}

/* The source rule (racer.c): strict improvement; a new time goes before the first slower
 * one, so older equal times stay ahead. */
static unsigned place_for(const fzx_rec_course_t *c, uint32_t ms) {
    for (unsigned i = 0; i < FZX_REC_TOP; ++i)
        if (ms < c->top[i].ms) return i + 1u;
    return 0;
}

/* Death Race's one course is 54, past the 24 course slots: its single time is slot 0 of death_race[] (the other slots stay empty). */
static unsigned death_slot(const fzx_run_result_t *run) { return run->course < FZX_REC_COURSES ? run->course : 0u; }
void fzx_records_rank(const fzx_run_result_t *run, fzx_rank_t *out) {
    memset(out, 0, sizeof(*out));
    if (!run || run->reason != FZX_END_FINISH || (run->course >= FZX_REC_COURSES && run->menu_mode != 2u)) return;
#if ENG_MODES
    if (!fzx_mode_keeps_times(run->menu_mode)) return; /* Grand Prix and Practice keep no times */
#endif
    if (run->menu_mode == 2u) { /* Death Race: its own single time */
        out->place = run->race_ms < table.death_race[death_slot(run)].ms ? 1u : 0u;
        return;
    }
    if (run->course >= FZX_REC_COURSES) return;
    const fzx_rec_course_t *c = &table.ta[run->course];
    out->place = (uint8_t)place_for(c, run->race_ms);
    if (run->best_lap_ms && run->best_lap_ms < c->best_lap.ms) { out->best_lap = 1; out->best_lap_index = run->best_lap_index; }
    if (run->max_speed > c->max_speed) out->max_speed = 1;
}

static void fill_time(fzx_rec_time_t *t, uint32_t ms, const fzx_run_result_t *run, const char name[4]) {
    memset(t, 0, sizeof(*t));
    t->ms = ms; t->slider_bits = run->slider_bits; t->machine_id = run->machine_id; t->skin = run->skin;
    if (name) for (unsigned i = 0; i < 3u && name[i]; ++i) t->name[i] = name[i];
}

fzx_rec_result_t fzx_records_commit(const fzx_run_result_t *run, const char name[4]) {
    if (!run) return FZX_REC_FAILED;
    for (unsigned i = 0; i < RECENT; ++i)
        if (recent[i] && recent[i] == run->run_id) { ++fzx_rec_report[11]; fzx_rec_report[4] = FZX_REC_ALREADY; return FZX_REC_ALREADY; }
    recent[recent_at++ % RECENT] = run->run_id;
    fzx_rank_t rk;
    fzx_records_rank(run, &rk);
    if (run->menu_mode == 2u) {
        if (rk.place) fill_time(&table.death_race[death_slot(run)], run->race_ms, run, name);
    } else if (run->course < FZX_REC_COURSES) {
        fzx_rec_course_t *c = &table.ta[run->course];
        if (rk.place) {
            for (unsigned i = FZX_REC_TOP - 1u; i >= rk.place; --i) c->top[i] = c->top[i - 1u];
            fill_time(&c->top[rk.place - 1u], run->race_ms, run, name);
        }
        if (rk.best_lap) { fill_time(&c->best_lap, run->best_lap_ms, run, rk.place ? name : NULL); c->best_lap_index = rk.best_lap_index; }
        if (rk.max_speed) { c->max_speed = run->max_speed; c->max_speed_machine_id = run->machine_id; }
    }
    ++fzx_rec_report[10];
    if (!rk.place && !rk.best_lap && !rk.max_speed) { fzx_rec_report[4] = FZX_REC_OK; return FZX_REC_OK; }
    dirty = true;
    const fzx_rec_result_t r = write_table();
    fzx_rec_report[4] = r; fzx_rec_report[6] = dirty;
    return r;
}

#if ENG_MODES
fzx_rec_result_t fzx_records_cup_clear(uint8_t difficulty, uint8_t cup, uint16_t machine_id) {
    if (difficulty > 3u || cup >= FZX_REC_CUPS) return FZX_REC_FAILED;
    fzx_rec_progress_t *p = &table.progress;
    bool before = false; /* var_v1: some cup is still under three difficulties */
    for (unsigned i = 0; i < FZX_REC_CUPS; ++i) before |= p->cup_cleared[i] < 3u;
    if (machine_id < 30u) p->cup_done[difficulty][machine_id / 3u] |= (uint16_t)((1u << ((machine_id % 3u) * 5u)) << cup);
    if (p->cup_cleared[cup] < difficulty + 1u) p->cup_cleared[cup] = (uint8_t)(difficulty + 1u);
    bool all = true;
    for (unsigned i = 0; i < FZX_REC_CUPS; ++i) all &= p->cup_cleared[i] >= 3u;
    if (before && all) p->credits_due = 1;
    dirty = true;
    const fzx_rec_result_t r = write_table();
    fzx_rec_report[4] = r; fzx_rec_report[6] = dirty;
    return r;
}
uint32_t fzx_records_unlock_level(void) {
    const fzx_rec_progress_t *p = &table.progress;
    unsigned sum = 0;
    bool all3 = true, three2 = true;
    for (unsigned i = 0; i < FZX_REC_CUPS; ++i) { sum += p->cup_cleared[i]; all3 &= p->cup_cleared[i] >= 3u; }
    for (unsigned i = 0; i < 3u; ++i) three2 &= p->cup_cleared[i] >= 2u;
    return sum >= 16u ? 3u : all3 ? 2u : three2 ? 1u : 0u;
}
bool fzx_records_class_open(uint32_t difficulty) { return difficulty < 3u || (difficulty == 3u && fzx_records_unlock_level() >= 2u); }
bool fzx_records_credits_due(void) { return table.progress.credits_due != 0u; }
fzx_rec_result_t fzx_records_credits_seen(void) {
    if (!table.progress.credits_due) return FZX_REC_OK;
    table.progress.credits_due = 0; dirty = true;
    return write_table();
}
#endif

#if ENG_GHOST
/* The Time Attack ghost: one slot kept as two slots (A, B), written alternately with a generation, as the records are.
 * A slot is a head file (FZXGH_HA: the first GH_CHUNK bytes of the ghost image, the 44-byte header and the start of the stream) and
 * its chunk files (FZXGH_CA01, ...02: GH_CHUNK bytes each, the last the remainder). A backup-RAM write blocks its caller for as long
 * as it takes (about half a field for 1 KB on the emulator), so the save is a queue: fzx_records_ghost_save_begin stamps the image, and
 * fzx_records_ghost_pump writes one chunk file per menu frame (RESULTS keeps presenting every two fields); the head file goes last and
 * is the commit. Until it is written the target slot is not a valid ghost (a stale head with new chunks fails the whole-image CRC and
 * the decode), so a power cut leaves the other slot, the previous best. A head file as large as the whole image (the first format)
 * is read as one piece. */
#define GH_CHUNK 1024u
#define GH_MAX_CHUNKS ((FZX_GHOST_HEAD_BYTES + FZX_GHOST_STREAM_MAX + GH_CHUNK - 1u) / GH_CHUNK)
/* The backup library finds files by the beginning of their name (BUP_Dir), so no file name may begin with another file's: head FZXGH_HA, chunks
 * FZXGH_CA01...; the first format's single file (FZXGHO_A / _B, never chunked) is still read. */
static const char *const ghost_file[2] = { "FZXGH_HA", "FZXGH_HB" };
static const char *const ghost_legacy[2] = { "FZXGHO_A", "FZXGHO_B" };
static fzx_ghost_t ghost_io;            /* one image; high work RAM, like `io` */
static uint32_t ghost_gen[2];
volatile uint32_t fzx_ghost_fault;       /* captures: 1 = the next ghost write stores a wrong CRC; 2 = it stops half way (no head file) */
/* [0] last load (1 found, 0 none), [1] generation loaded, [2] slot loaded (1 A, 2 B), [3] A valid, [4] B valid, [5] last save result,
 * [6] slot written, [7] bup status of the last write, [8] bytes of the last image, [9] free blocks after it, [10] FRT ticks the last chunk
 * write blocked (16 bits), [11] saves begun, [12] chunk files written, [13] saves completed, [14] chunks of the last image,
 * [15] most FRT ticks any chunk write blocked */
volatile uint32_t fzx_ghost_store[16];
static struct { bool active; unsigned slot, n, next; uint32_t gen, size; bool half, bad; } gw;   /* the queued save; next counts 1..n-1 then 0 (the head) */

static void ghost_name(char out[12], unsigned slot, unsigned k) {
    static const char hex[] = "0123456789abcdef";
    strcpy(out, ghost_file[slot]);
    if (k) { out[6] = 'C'; out[8] = hex[(k >> 4) & 15u]; out[9] = hex[k & 15u]; out[10] = 0; }
}

static bool ghost_read_as(const char *hname, unsigned i, uint32_t *gen) {
    bup_directory_t d[1];
    memset(&d[0], 0, sizeof(d[0]));
    if (bup_dir(BUP_DEVICE_INTERNAL, hname, 1, d) <= 0 || d[0].data_size > sizeof(ghost_io) || d[0].data_size < FZX_GHOST_HEAD_BYTES) return false;
    const uint32_t head = d[0].data_size;
    memset(&ghost_io, 0, FZX_GHOST_HEAD_BYTES);
    if (bup_read(BUP_DEVICE_INTERNAL, hname, &ghost_io) != BUP_OK) return false;
    if (ghost_io.bytes > FZX_GHOST_STREAM_MAX) return false;
    const uint32_t total = FZX_GHOST_FILE_BYTES(&ghost_io);
    if (head < total) {   /* chunked: the head is one chunk, the rest are the slot's chunk files */
        if (head != GH_CHUNK) return false;
        for (uint32_t off = head, k = 1; off < total; off += GH_CHUNK, ++k) {
            char nm[12];
            ghost_name(nm, i, k);
            const uint32_t want = total - off < GH_CHUNK ? total - off : GH_CHUNK;
            bup_directory_t c[1];
            memset(&c[0], 0, sizeof(c[0]));
            if (bup_dir(BUP_DEVICE_INTERNAL, nm, 1, c) <= 0 || c[0].data_size != want) return false;
            if (bup_read(BUP_DEVICE_INTERNAL, nm, (uint8_t *)&ghost_io + off) != BUP_OK) return false;
        }
    } else if (head != total) return false;
    if (!fzx_ghost_valid(&ghost_io, total)) return false;
    *gen = ghost_io.generation;
    return true;
}
static bool ghost_read(unsigned i, uint32_t *gen) { return ghost_read_as(ghost_file[i], i, gen) || ghost_read_as(ghost_legacy[i], i, gen); }

bool fzx_records_ghost_load(fzx_ghost_t *dst) {
    ensure_bup();
    ghost_gen[0] = ghost_gen[1] = 0;
    unsigned best = 2u;
    for (unsigned i = 0; i < 2u; ++i) {
        uint32_t g;
        if (ghost_read(i, &g)) {
            ghost_gen[i] = g;
            fzx_ghost_store[3u + i] = 1u;
            if (best == 2u || g > ghost_gen[best]) { best = i; memcpy(dst, &ghost_io, FZX_GHOST_FILE_BYTES(&ghost_io)); }
        } else fzx_ghost_store[3u + i] = 0u;
    }
    fzx_ghost_store[0] = best < 2u; fzx_ghost_store[1] = best < 2u ? ghost_gen[best] : 0u; fzx_ghost_store[2] = best < 2u ? best + 1u : 0u;
    return best < 2u;
}

/* Stamps the image and queues its files; nothing is written here. The queue holds one save (a second begin first finishes the first). */
fzx_rec_result_t fzx_records_ghost_save_begin(const fzx_ghost_t *g) {
    ensure_bup();
    while (gw.active) (void)fzx_records_ghost_pump();
    gw.slot = ghost_gen[0] <= ghost_gen[1] ? 0u : 1u;
    gw.gen = (ghost_gen[0] > ghost_gen[1] ? ghost_gen[0] : ghost_gen[1]) + 1u;
    gw.size = FZX_GHOST_FILE_BYTES(g);
    memcpy(&ghost_io, g, gw.size);
    fzx_ghost_stamp(&ghost_io, gw.gen);
    const uint32_t fault = POKED(fzx_ghost_fault);
    if (fault == 1u) ghost_io.crc ^= 0x00010000u;
    gw.half = fault == 2u;
    gw.bad = fault == 1u;
    POKED(fzx_ghost_fault) = 0;
    gw.n = (gw.size + GH_CHUNK - 1u) / GH_CHUNK;
    gw.next = gw.n > 1u ? 1u : 0u;
    gw.active = true;
    ++fzx_ghost_store[11];
    fzx_ghost_store[6] = gw.slot + 1u; fzx_ghost_store[8] = gw.size; fzx_ghost_store[14] = gw.n;
    return FZX_REC_OK;
}

bool fzx_records_ghost_pending(void) { return gw.active; }

/* One backup-RAM write of the queued save (a chunk file, or last the head). True while more is queued. */
bool fzx_records_ghost_pump(void) {
    if (!gw.active) return false;
    const unsigned k = gw.next;
    const bool head = k == 0u;
    /* Control 2: the save stops half way, the head never written (a power cut). */
    if (gw.half && (gw.n == 1u || (!head && k > gw.n / 2u))) {
        gw.active = false;
        fzx_ghost_store[5] = FZX_REC_FAILED;
        return false;
    }
    bup_directory_t dir;
    memset(&dir, 0, sizeof(dir));
    char nm[12];
    ghost_name(nm, gw.slot, k);
    strcpy(dir.filename, nm);
    strcpy(dir.comment, "FZX GHOST");
    dir.language = BUP_LANG_ENGLISH;
    const uint32_t off = head ? 0u : k * GH_CHUNK;
    const uint32_t len = gw.n == 1u ? gw.size : head ? GH_CHUNK : (gw.size - off < GH_CHUNK ? gw.size - off : GH_CHUNK);
    dir.data_size = len;
    engine_smpc_quiet(true);
    const uint16_t w0 = cpu_frt_count_get();
    const uint32_t bf0 = video_fields_get();
    const bup_status_t s = bup_write(BUP_DEVICE_INTERNAL, &dir, (uint8_t *)&ghost_io + off, BUP_WRITE_MODE_OVERWRITE);
    const uint16_t dt = (uint16_t)(cpu_frt_count_get() - w0);
    fzx_ghost_store[10] = dt;
    if (dt > fzx_ghost_store[15]) fzx_ghost_store[15] = dt;
    bup_log(bf0, w0, len, 2u);
    ++fzx_ghost_store[12];
    bup_stat_t st;
    memset(&st, 0, sizeof(st));
    (void)bup_status(BUP_DEVICE_INTERNAL, 0, &st);
    engine_smpc_quiet(false);
    fzx_ghost_store[7] = s; fzx_ghost_store[9] = st.free_blocks;
    if (s != BUP_OK) {
        gw.active = false;
        fzx_ghost_store[5] = s == BUP_NOT_ENOUGH_MEMORY ? FZX_REC_FULL : FZX_REC_FAILED;
        return false;
    }
    if (head) {
        gw.active = false;
        ++fzx_ghost_store[13];
        if (gw.bad) { fzx_ghost_store[5] = FZX_REC_FAILED; return false; }   /* control 1: the stored image is wrong; not a generation */
        ghost_gen[gw.slot] = gw.gen;
        fzx_ghost_store[5] = FZX_REC_OK;
        return false;
    }
    gw.next = k + 1u < gw.n ? k + 1u : 0u;
    return true;
}
#endif

fzx_rec_result_t fzx_records_rewrite(void) {
    dirty = true;
    const fzx_rec_result_t r = write_table();
    fzx_rec_report[4] = r; fzx_rec_report[6] = dirty;
    return r;
}

fzx_rec_result_t fzx_records_flush(void) {
    if (!dirty) return FZX_REC_OK;
    const fzx_rec_result_t r = write_table();
    fzx_rec_report[4] = r; fzx_rec_report[6] = dirty;
    return r;
}
