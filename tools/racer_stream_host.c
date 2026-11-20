/* Racer sprite streaming, HOST driver for SH2ZAM sprite-cache-1.
 *
 *   racer_stream_host decode PACK ROSTER [ARENA_OUT]
 *       Decode every cell of an RSZ1 pack (tools/racer_stream.py pack) with
 *       sh2z_cell4z_decode, compare with the raw RSP1 1.1 roster, and time it.
 *       ARENA_OUT receives the built context tables for tools/sh2_cycle_model.py.
 *   racer_stream_host replay TRACE LIVE CYC_PX BUDGET SIZE:COUNT...
 *       Replay a race trace (tools/racer_stream.py trace) through the slot
 *       cache: each frame draws its cells and requests the next frame's
 *       (prefetch). Misses queue for the slave, which fills slots in request
 *       order until BUDGET output bytes per frame are spent (0: no limit);
 *       a drawn cell that is not ready yet counts as late. CYC_PX is the
 *       modelled slave cycles per filled pixel. The start grid is filled
 *       before frame 0 (countdown). Prints one JSON line.
 *
 * Inputs are ROM-derived and stay local; this file holds no game data.
 */
#include "sh2zam/sprite_cache_experimental.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)len + 1);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { perror(path); exit(2); }
    fclose(f);
    *n = (size_t)len;
    return b;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec; }

static int decode_cmd(const char *pack_path, const char *roster_path, const char *arena_out) {
    size_t pn, rn;
    uint8_t *pk = slurp(pack_path, &pn), *ro = slurp(roster_path, &rn);
    if (memcmp(pk, "RSZ1", 4) != 0 || be16(pk + 4) != 0x0102) { fprintf(stderr, "not an RSZ1 1.2 pack\n"); return 2; }
    const uint32_t sets = be16(pk + 6);
    uint8_t len[SH2Z_CELL4Z_CONTEXTS][SH2Z_CELL4Z_SYMBOLS];
    memcpy(len, pk + 8, sizeof len);
    static uint8_t arena[16384];
    sh2z_cell4z_tables_t t;
    const size_t used = sh2z_cell4z_build(arena, sizeof arena, &len[0][0], &t);
    if (!used) { fprintf(stderr, "table build refused\n"); return 1; }
    if (arena_out) {
        FILE *f = fopen(arena_out, "wb");
        if (!f) { perror(arena_out); return 2; }
        fwrite(arena, 1, used, f);
        for (uint32_t k = 0; k < SH2Z_CELL4Z_CONTEXTS; ++k) { uint8_t o[2] = {(uint8_t)(t.offset[k] >> 8), (uint8_t)t.offset[k]}; fwrite(o, 1, 2, f); }
        fclose(f);
    }
    const size_t dir = 8u + sizeof len;
    uint64_t cells = 0, px = 0, bad = 0;
    double best = 1e30;
    static uint16_t out[504 * 255 / 4];
    for (int rep = 0; rep < 5; ++rep) {
        const double t0 = now();
        for (uint32_t s = 0; s < sets; ++s) {
            const uint8_t *e = pk + dir + 32u * s;
            const uint8_t *hdr = pk + be32(e + 12);
            const uint32_t st = be32(e + 20);
            const uint32_t nc = (uint32_t)be16(hdr + 8) * be16(hdr + 10);
            /* raw set for the comparison */
            const uint8_t *raw = 0;
            for (uint32_t i = 0; (i + 1) * 32u <= rn && be16(ro + 32u * i + 24) == 0x101; ++i)
                if (!memcmp(ro + 32u * i, e, 12)) { raw = ro + be32(ro + 32u * i + 12); break; }
            if (!raw) { fprintf(stderr, "set %u missing from roster\n", s); return 1; }
            for (uint32_t c = 0; c < nc; ++c) {
                const uint8_t *cd = hdr + 168u + 16u * c;
                const uint32_t w = be16(cd), h = be16(cd + 2), off = be32(cd + 8);
                if (be16(pk + st + 8u * nc + 2u * c) & 0x8000u) continue;   /* flip alias: no stream */
                const uint32_t so = st + 10u * nc + be32(pk + st + 8u * c), sb = be32(pk + st + 8u * c + 4);
                size_t bits = 0;
                if (!sh2z_cell4z_decode(out, w, h, pk + so, pn - so, arena, &t, &bits) || (bits + 15u) / 16u * 2u != sb) { ++bad; continue; }
                if (rep == 0) {
                    for (uint32_t i = 0; i < w * h / 4u; ++i)
                        if (out[i] != be16(raw + off + 2u * i)) { ++bad; break; }
                    ++cells;
                    px += (uint64_t)w * h;
                }
            }
        }
        const double dt = now() - t0;
        if (dt < best) best = dt;
    }
    printf("{\"cells\":%llu,\"pixels\":%llu,\"mismatches\":%llu,\"arena_bytes\":%zu,\"lmax\":%u,"
           "\"host_ns_per_px\":%.2f,\"host_us_per_cell\":%.2f}\n",
           (unsigned long long)cells, (unsigned long long)px, (unsigned long long)bad, used, t.lmax,
           1e9 * best / (double)px, 1e6 * best / (double)cells);
    return bad ? 1 : 0;
}

/* ---- replay ---- */
typedef struct { uint32_t key, bytes, prof; } req_t;
typedef struct { req_t *r; uint32_t n; } frame_t;

static int cmp_double(const void *a, const void *b) {
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static int replay_cmd(const char *trace, uint32_t live, double cyc_px, uint32_t budget, int argc, char **argv) {
    size_t n;
    char *txt = (char *)slurp(trace, &n);
    txt[n] = 0;
    uint32_t fcount = 0, rcount = 0;
    for (char *p = txt; *p; ++p) if ((p == txt || p[-1] == '\n') && (*p == 'F' || *p == 'R')) { if (*p == 'F') ++fcount; else ++rcount; }
    frame_t *fr = calloc(fcount, sizeof *fr);
    req_t *rq = calloc(rcount ? rcount : 1, sizeof *rq);
    uint32_t fi = 0, ri = 0;
    for (char *line = strtok(txt, "\n"); line; line = strtok(0, "\n")) {
        if (line[0] == 'F') { fr[fi].r = rq + ri; fr[fi].n = 0; ++fi; continue; }
        unsigned key, bytes, racer;
        char prof[8];
        if (sscanf(line, "R %u %u %7s %u", &key, &bytes, prof, &racer) != 4 || fi == 0) continue;
        rq[ri].key = key; rq[ri].bytes = bytes; rq[ri].prof = prof[0] == 'f' ? 2u : prof[0] == 'n' ? 1u : 0u;
        ++ri; ++fr[fi - 1].n;
    }
    uint32_t sizes[8], counts[8], classes = 0, pool = 0;
    for (int i = 0; i < argc && classes < 8; ++i) {
        if (sscanf(argv[i], "%u:%u", &sizes[classes], &counts[classes]) != 2) return 2;
        pool += sizes[classes] * counts[classes];
        ++classes;
    }
    static sh2z_scache_slot_t slots[1024];
    static uint32_t queue[1024], qbytes[1024];
    uint32_t qh = 0, qn = 0;
    uint32_t end = 0;
    const uint32_t ns = sh2z_scache_layout(slots, 1024u, 0u, 0x80000u, sizes, counts, classes, &end);
    sh2z_scache_t c;
    if (!ns || !sh2z_scache_init(&c, slots, ns, live)) { fprintf(stderr, "bad pool\n"); return 2; }
#define ENQ(slot_, bytes_) do { queue[(qh + qn) % 1024u] = (slot_); qbytes[(qh + qn) % 1024u] = (bytes_); ++qn; } while (0)
    /* warm: the start grid is decoded during the countdown, before frame 0 */
    uint32_t warm_bytes = 0;
    sh2z_scache_frame(&c);
    for (uint32_t i = 0; fcount && i < fr[0].n; ++i) {
        uint32_t s;
        if (sh2z_scache_get(&c, fr[0].r[i].key, fr[0].r[i].bytes, true, &s) == SH2Z_SCACHE_MISS) { warm_bytes += fr[0].r[i].bytes; (void)sh2z_scache_ready(&c, s); }
    }
    uint64_t draws = 0, ready = 0, late = 0, dropped = 0, decodes = 0, dec_bytes = 0, requested = 0, stale = 0;
    uint32_t peak_bytes = 0, peak_cells = 0, peak_queue = 0;
    double sum_ms = 0, peak_ms = 0;
    double *ms_of = calloc(fcount ? fcount : 1, sizeof *ms_of);
    for (uint32_t f = 0; f < fcount; ++f) {
        sh2z_scache_frame(&c);
        uint32_t fb = 0, fc = 0;
        /* draw list of frame f */
        for (uint32_t i = 0; i < fr[f].n; ++i) {
            uint32_t s;
            const int r = sh2z_scache_get(&c, fr[f].r[i].key, fr[f].r[i].bytes, false, &s);
            ++draws;
            if (r == SH2Z_SCACHE_HIT) ++ready;
            else if (r == SH2Z_SCACHE_REFUSED) ++dropped;
            else {
                ++late;   /* not ready: draw the racer's previous cell or nothing this frame */
                if (r == SH2Z_SCACHE_MISS) { ENQ(s, fr[f].r[i].bytes); ++requested; }
            }
        }
        /* requests for frame f + 1 */
        if (f + 1u < fcount)
            for (uint32_t i = 0; i < fr[f + 1u].n; ++i) {
                uint32_t s;
                if (sh2z_scache_get(&c, fr[f + 1u].r[i].key, fr[f + 1u].r[i].bytes, true, &s) == SH2Z_SCACHE_MISS) { ENQ(s, fr[f + 1u].r[i].bytes); ++requested; }
            }
        if (qn > peak_queue) peak_queue = qn;
        /* the slave decodes in request order until its budget for this frame is spent */
        while (qn && (budget == 0u || fb < budget)) {
            if (!sh2z_scache_live(&c, queue[qh])) {   /* no longer wanted: drop the claim, skip the fill */
                (void)sh2z_scache_abandon(&c, queue[qh]);
                ++stale;
                qh = (qh + 1u) % 1024u;
                --qn;
                continue;
            }
            (void)sh2z_scache_ready(&c, queue[qh]);
            fb += qbytes[qh];
            ++fc;
            qh = (qh + 1u) % 1024u;
            --qn;
        }
        decodes += fc;
        dec_bytes += fb;
        const double ms = (double)fb * 2.0 * cyc_px / 28636.36;   /* 2 px per byte, 28.636 MHz */
        ms_of[f] = ms;
        sum_ms += ms;
        if (ms > peak_ms) peak_ms = ms;
        if (fb > peak_bytes) peak_bytes = fb;
        if (fc > peak_cells) peak_cells = fc;
    }
    qsort(ms_of, fcount, sizeof *ms_of, cmp_double);
    printf("{\"frames\":%u,\"pool_bytes\":%u,\"slots\":%u,\"live\":%u,\"budget_bytes\":%u,\"warm_bytes\":%u,\"draws\":%llu,\"ready\":%llu,"
           "\"late\":%llu,\"dropped\":%llu,\"decoded_cells\":%llu,\"miss_rate\":%.4f,\"late_rate\":%.4f,\"mean_bytes_per_frame\":%.0f,"
           "\"peak_bytes_per_frame\":%u,\"peak_cells_per_frame\":%u,\"peak_queue\":%u,\"mean_slave_ms\":%.3f,\"p99_slave_ms\":%.3f,"
           "\"peak_slave_ms\":%.3f,\"stale_skipped\":%llu,\"evictions\":%u,\"refused\":%u}\n",
           fcount, pool, ns, live, budget, warm_bytes, (unsigned long long)draws, (unsigned long long)ready, (unsigned long long)late,
           (unsigned long long)dropped, (unsigned long long)decodes, (double)requested / (double)draws,
           (double)(late + dropped) / (double)draws, (double)dec_bytes / fcount, peak_bytes, peak_cells, peak_queue,
           sum_ms / fcount, ms_of[fcount ? (fcount * 99u) / 100u : 0], peak_ms, (unsigned long long)stale, c.evictions, c.refused);
    free(ms_of);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 4 && !strcmp(argv[1], "decode")) return decode_cmd(argv[2], argv[3], argc > 4 ? argv[4] : 0);
    if (argc >= 7 && !strcmp(argv[1], "replay"))
        return replay_cmd(argv[2], (uint32_t)atoi(argv[3]), atof(argv[4]), (uint32_t)atoi(argv[5]), argc - 6, argv + 6);
    fprintf(stderr, "usage: racer_stream_host decode PACK ROSTER [ARENA_OUT] | replay TRACE LIVE CYC_PX BUDGET_BYTES SIZE:COUNT...\n");
    return 2;
}
