/* Time Attack ghost: see engine_ghost.h. */
#include <stddef.h>
#include <string.h>
#include "engine_ghost.h"

typedef char fzx_ghost_head_size_check[offsetof(fzx_ghost_t, stream) == FZX_GHOST_HEAD_BYTES ? 1 : -1];

static uint32_t crc32(const uint8_t *p, uint32_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (unsigned k = 0; k < 8u; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static uint32_t body_crc(const fzx_ghost_t *g) {
    const uint32_t skip = (uint32_t)offsetof(fzx_ghost_t, crc) + 4u;
    return crc32((const uint8_t *)g + skip, FZX_GHOST_HEAD_BYTES + g->bytes - skip);
}

/* ---- nibble stream ------------------------------------------------------------------------------------------------- */
static void put_nib(fzx_ghost_t *g, uint32_t n, uint32_t v) {
    uint8_t *b = &g->stream[n >> 1];
    *b = (n & 1u) ? (uint8_t)((*b & 0xF0u) | (v & 0xFu)) : (uint8_t)((v & 0xFu) << 4);
}
static uint32_t get_nib(const uint8_t *s, uint32_t n) { return (n & 1u) ? (s[n >> 1] & 0xFu) : (uint32_t)(s[n >> 1] >> 4); }

/* The recorder keeps its state in the unused tail of nothing: static, one recording at a time. */
static struct { uint32_t tick, nib, samples; int32_t prev[3], dprev[3]; bool over; } rec;

void fzx_ghost_rec_begin(fzx_ghost_t *g, uint32_t course) {
    memset(g, 0, FZX_GHOST_HEAD_BYTES);
    g->magic = FZX_GHOST_MAGIC; g->version = 1u; g->course = course;
    memset(&rec, 0, sizeof(rec));
}

static int32_t units(int32_t q16) { return (q16 + 0x8000) >> 16; }

void fzx_ghost_rec_tick(fzx_ghost_t *g, const int32_t pos_q16[3]) {
    if (rec.over || (rec.tick++ & 1u)) return;
    int32_t v[3];
    for (int i = 0; i < 3; ++i) v[i] = units(pos_q16[i]);
    if (rec.samples == 0u) {
        for (int i = 0; i < 3; ++i) { g->start[i] = v[i]; rec.prev[i] = v[i]; rec.dprev[i] = 0; }
        rec.samples = 1u;
        return;
    }
    uint32_t n = rec.nib;
    for (int i = 0; i < 3; ++i) {
        const int32_t d = v[i] - rec.prev[i], dd = d - rec.dprev[i];
        if (dd >= -7 && dd <= 7) {
            if (n + 1u > FZX_GHOST_STREAM_MAX * 2u) { rec.over = true; return; }
            put_nib(g, n++, (uint32_t)(dd + 8));
        } else if (dd >= -32768 && dd <= 32767) {
            if (n + 5u > FZX_GHOST_STREAM_MAX * 2u) { rec.over = true; return; }
            put_nib(g, n++, 0u);
            for (int k = 3; k >= 0; --k) put_nib(g, n++, ((uint32_t)dd >> (4 * k)) & 0xFu);
        } else { rec.over = true; return; }
        rec.prev[i] = v[i]; rec.dprev[i] = d;
    }
    rec.nib = n; ++rec.samples;
}

bool fzx_ghost_rec_overrun(const fzx_ghost_t *g) { (void)g; return rec.over; }

bool fzx_ghost_rec_end(fzx_ghost_t *g, uint32_t race_ms) {
    if (rec.over || rec.samples < 2u) return false;
    g->race_ms = race_ms; g->samples = rec.samples; g->bytes = (rec.nib + 1u) >> 1;
    g->crc = body_crc(g);
    return true;
}

void fzx_ghost_stamp(fzx_ghost_t *g, uint32_t generation) { g->generation = generation; g->crc = body_crc(g); }

/* ---- playback ------------------------------------------------------------------------------------------------------ */
/* Decodes the next sample into p->next (and advances); false at the end or on a bad stream. */
static bool step_decode(fzx_ghost_play_t *p) {
    if (p->left == 0u) return false;
    const uint8_t *s = p->g->stream;
    const uint32_t end = p->g->bytes * 2u;
    for (int i = 0; i < 3; ++i) {
        if (p->nib >= end) return false;
        uint32_t v = get_nib(s, p->nib++);
        int32_t dd;
        if (v == 0u) {
            if (p->nib + 4u > end) return false;
            uint32_t u = 0;
            for (int k = 0; k < 4; ++k) u = (u << 4) | get_nib(s, p->nib++);
            dd = (int32_t)(int16_t)u;
        } else dd = (int32_t)v - 8;
        const int32_t d = p->dprev[i] + dd;
        p->next[i] = p->next[i] + d;
        p->dprev[i] = d;
    }
    --p->left;
    return true;
}

bool fzx_ghost_valid(const fzx_ghost_t *g, uint32_t size) {
    if (size < FZX_GHOST_HEAD_BYTES || g->magic != FZX_GHOST_MAGIC || g->version != 1u) return false;
    if (g->bytes > FZX_GHOST_STREAM_MAX || size != FZX_GHOST_HEAD_BYTES + g->bytes || g->samples < 2u) return false;
    if (g->crc != body_crc(g)) return false;
    fzx_ghost_play_t p;
    fzx_ghost_play_begin(&p, g);
    uint32_t n = p.live ? 2u : 1u;   /* the start and the sample play_begin decoded */
    while (step_decode(&p)) ++n;
    return n == g->samples && p.nib >= g->bytes * 2u - 1u;
}

void fzx_ghost_play_begin(fzx_ghost_play_t *p, const fzx_ghost_t *g) {
    memset(p, 0, sizeof(*p));
    p->g = g; p->left = g->samples > 0u ? g->samples - 1u : 0u; p->live = g->samples >= 2u;
    for (int i = 0; i < 3; ++i) { p->cur[i] = g->start[i]; p->next[i] = g->start[i]; }
    if (p->live && !step_decode(p)) p->live = false;
}

bool fzx_ghost_play_tick(fzx_ghost_play_t *p, int32_t out[3]) {
    if (!p->live || p->tick > 2u * (p->g->samples - 1u) + 1u) return false;
    if (p->tick & 1u) {
        for (int i = 0; i < 3; ++i) out[i] = (int32_t)(((int64_t)p->cur[i] + p->next[i]) << 15);
        ++p->tick;
        return true;
    }
    if (p->tick && p->left + 1u > 0u) {         /* move to the next sample; at the last one, hold it */
        for (int i = 0; i < 3; ++i) p->cur[i] = p->next[i];
        if (p->tick < 2u * (p->g->samples - 1u) && !step_decode(p)) { p->live = false; return false; }
    }
    for (int i = 0; i < 3; ++i) out[i] = (int32_t)((int64_t)p->cur[i] << 16);
    ++p->tick;
    return true;
}
