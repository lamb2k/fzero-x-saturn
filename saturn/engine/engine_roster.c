/* Engine: selective roster load (ENG_ROSTER; see engine_roster.h). */
#include <string.h>
#include <yaul.h>
#include "engine_roster.h"
#include "texture_cd.h"
#include "rosterz_identity.h"

volatile uint32_t fzx_roster_report[16];

#define ARENA_BYTES 8192u
static uint8_t arena[ARENA_BYTES] __attribute__((aligned(4)));
static uint8_t lens[FZX_RZ_LENS_BYTES] __attribute__((aligned(4)));
static sh2z_cell4z_tables_t tables;
static const uint8_t *block_at[FZX_RZ_SETS]; /* loaded block of each set, NULL if not loaded */

static int find(uint32_t family, unsigned prof) {
    static const char names[2][4] = { { 'n', 'e', 'a', 'r' }, { 'f', 'a', 'r', 0 } };
    const char key[8] = { 'c', (char)('0' + family / 10u), (char)('0' + family % 10u), 's', '1', 0, 0, 0 };
    for (unsigned i = 0; i < FZX_RZ_SETS; ++i)
        if (!memcmp(fzx_rz_sets[i].key, key, 8) && !memcmp(fzx_rz_sets[i].prof, names[prof & 1u], 4)) return (int)i;
    return -1;
}

bool fzx_roster_select_load(const uint8_t *families, unsigned n, uintptr_t cart_base, uintptr_t cart_limit,
                            void *scratch, uint32_t scratch_bytes) {
    memset((void *)fzx_roster_report, 0, sizeof(fzx_roster_report));
    memset(block_at, 0, sizeof(block_at));
    /* The shared code-length block (480 bytes), then the 16 context tables. */
    if (!fzx_texture_cd_load_range("ROSTERZ.PAK", FZX_RZ_BYTES, FZX_RZ_LENS_AT, FZX_RZ_LENS_BYTES, FZX_RZ_LENS_FNV,
                                   lens, scratch, scratch_bytes)) { fzx_roster_report[0] = 2; return false; }
    if (!sh2z_cell4z_build(arena, sizeof(arena), lens, &tables)) { fzx_roster_report[0] = 3; return false; }
    uintptr_t at = (cart_base + 7u) & ~(uintptr_t)7u;
    uint32_t sets = 0, bytes = 0;
    for (unsigned i = 0; i < n; ++i)
        for (unsigned prof = 0; prof < 2u; ++prof) {
            const int s = find(families[i], prof);
            if (s < 0) { fzx_roster_report[0] = 4; return false; }
            if (block_at[s]) continue; /* a family twice in the field */
            const fzx_rz_set_t *e = &fzx_rz_sets[s];
            /* Each block is followed by the decoder's slack (zero bytes it may read). */
            if (at + e->bytes + FZX_ROSTER_SLACK > cart_limit) { fzx_roster_report[0] = 5; return false; }
            if (!fzx_texture_cd_load_range("ROSTERZ.PAK", FZX_RZ_BYTES, e->at, e->bytes, e->fnv, (void *)at,
                                           scratch, scratch_bytes)) {
                fzx_roster_report[0] = 6; fzx_roster_report[7] = (uint32_t)s; return false;
            }
            for (uint32_t k = 0; k < FZX_ROSTER_SLACK; k += 4u) *(volatile uint32_t *)(at + e->bytes + k) = 0;
            block_at[s] = (const uint8_t *)at;
            at += (e->bytes + FZX_ROSTER_SLACK + 7u) & ~7u;
            ++sets; bytes += e->bytes;
        }
    fzx_roster_report[0] = 1; fzx_roster_report[1] = sets; fzx_roster_report[2] = bytes;
    fzx_roster_report[3] = (uint32_t)cart_base; fzx_roster_report[4] = (uint32_t)at;
    return true;
}

const sh2z_rspr_set_t *fzx_roster_set(uint32_t family, unsigned prof) {
    const int s = find(family, prof);
    return s >= 0 && block_at[s] ? (const sh2z_rspr_set_t *)(const void *)block_at[s] : NULL;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

bool fzx_roster_cell_source(uint32_t family, unsigned prof, uint32_t cell, uint32_t *src_cell, bool *flip) {
    const int s = find(family, prof);
    if (s < 0 || !block_at[s]) return false;
    const sh2z_rspr_set_t *set = (const sh2z_rspr_set_t *)(const void *)block_at[s];
    const uint32_t cells = (uint32_t)set->yaw_count * set->pitch_count;
    if (cell >= cells) return false;
    const uint8_t *table = block_at[s] + fzx_rz_sets[s].table_at;
    const uint16_t a = (uint16_t)((table[8u * cells + 2u * cell] << 8) | table[8u * cells + 2u * cell + 1u]);
    *src_cell = (a & 0x8000u) ? (uint32_t)(a & 0x7FFFu) : cell;
    *flip = (a & 0x8000u) != 0;
    return true;
}

bool fzx_roster_cell_decode(uint32_t family, unsigned prof, uint32_t cell, volatile uint16_t *dst) {
    const int s = find(family, prof);
    if (s < 0 || !block_at[s]) return false;
    const sh2z_rspr_set_t *set = (const sh2z_rspr_set_t *)(const void *)block_at[s];
    const uint32_t cells = (uint32_t)set->yaw_count * set->pitch_count;
    if (cell >= cells) return false;
    const uint8_t *table = block_at[s] + fzx_rz_sets[s].table_at;
    const uint32_t off = be32(table + 8u * cell), n = be32(table + 8u * cell + 4u);
    if (!n) return false; /* an alias: decode its source cell and draw it flipped */
    const uint8_t *streams = table + 10u * cells;
    const sh2z_rspr_cell_t *c = sh2z_rspr_cells(set) + cell;
    const uint8_t *stream = streams + off;
    const size_t readable = (size_t)((block_at[s] + fzx_rz_sets[s].bytes + FZX_ROSTER_SLACK) - stream);
    size_t used = 0;
    if (!sh2z_cell4z_decode(dst, c->width, c->height, stream, readable, arena, &tables, &used)) return false;
    return used <= (size_t)n * 8u;
}
