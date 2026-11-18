#include "menu_tables_low.h"
#include "texture_cd.h"
#include <string.h>

extern unsigned char sat_stub_data_start[];
volatile uint32_t fzx_menu_tables_low_status;
volatile uint32_t fzx_menu_tables_low_report;

_Static_assert(FZX_MENU_TABLES_BYTES % 4u == 0u, "pack is word sized");
_Static_assert(FZX_MENU_TABLES_SPARE_OFFSET == FZX_MENU_TABLES_BYTES, "spare follows the pack");
_Static_assert(FZX_MENU_TABLES_BYTES + FZX_MENU_TABLES_SPARE_BYTES == FZX_MENU_TABLES_RESERVED, "spare ends the reservation");
_Static_assert(FZX_MENU_TABLES_BASE + FZX_MENU_TABLES_RESERVED == FZX_MENU_TABLES_STAND_IN_BASE, "stand-ins follow the reservation");
_Static_assert((FZX_MENU_TABLES_BASE & 7u) == 0u, "loader alignment");

static const uint32_t guard_offsets[] = FZX_MENU_TABLES_GUARD_OFFSETS;
static const struct { uint32_t offset, bytes, padded; } payloads[] = FZX_MENU_TABLES_PAYLOADS;
static bool loaded;

static const volatile uint8_t *region(void) { return (const volatile uint8_t *)(uintptr_t)FZX_MENU_TABLES_BASE; }
static bool zero(uint32_t offset, uint32_t bytes) {
    const volatile uint8_t *p = region() + offset;
    for (uint32_t i = 0; i < bytes; ++i) if (p[i]) return false;
    return true;
}

uint32_t fzx_menu_tables_low_check(bool full) {
    if (!loaded) return FZX_MENU_TABLES_NOT_LOADED;
    uint32_t bad = 0;
    for (unsigned g = 0; g < sizeof guard_offsets / sizeof guard_offsets[0]; ++g) {
        const volatile uint32_t *w = (const volatile uint32_t *)(region() + guard_offsets[g]);
        for (unsigned i = 0; i < FZX_MENU_TABLES_GUARD_WORDS; ++i)
            if (w[i] != FZX_MENU_TABLES_GUARD) bad |= FZX_MENU_TABLES_GUARD_BAD;
        if (!zero(guard_offsets[g] + 4u * FZX_MENU_TABLES_GUARD_WORDS, FZX_MENU_TABLES_SLOT - 4u * FZX_MENU_TABLES_GUARD_WORDS))
            bad |= FZX_MENU_TABLES_SLOT_PAD_BAD;
    }
    for (unsigned t = 0; t < sizeof payloads / sizeof payloads[0]; ++t)
        if (!zero(payloads[t].offset + payloads[t].bytes, payloads[t].padded - payloads[t].bytes))
            bad |= FZX_MENU_TABLES_TABLE_PAD_BAD;
    if (!zero(FZX_MENU_TABLES_SPARE_OFFSET, FZX_MENU_TABLES_SPARE_BYTES)) bad |= FZX_MENU_TABLES_SPARE_BAD;
    if (full) {
        uint32_t hash = 2166136261u;
        const volatile uint8_t *p = region();
        for (uint32_t i = 0; i < FZX_MENU_TABLES_BYTES; ++i) hash = (hash ^ p[i]) * 16777619u;
        if (hash != FZX_MENU_TABLES_HASH) bad |= FZX_MENU_TABLES_HASH_BAD;
    }
    return bad;
}

bool fzx_menu_tables_low_load(void *scratch, uint32_t scratch_bytes) {
    loaded = false;
    /* The race-setup clear starts at sat_stub_data_start: it must lie at or above the reservation's end. */
    if ((uintptr_t)sat_stub_data_start != FZX_MENU_TABLES_STAND_IN_BASE ||
        (uintptr_t)scratch < FZX_MENU_TABLES_STAND_IN_BASE) {
        fzx_menu_tables_low_status = 2;
        return false;
    }
    memset((void *)(uintptr_t)(FZX_MENU_TABLES_BASE + FZX_MENU_TABLES_SPARE_OFFSET), 0, FZX_MENU_TABLES_SPARE_BYTES);
    if (!fzx_texture_cd_load("MENUTAB.PAK", FZX_MENU_TABLES_BYTES, FZX_MENU_TABLES_HASH,
                             (void *)(uintptr_t)FZX_MENU_TABLES_BASE, scratch, scratch_bytes)) {
        fzx_menu_tables_low_status = 3u + fzx_texture_cd_status.status;
        return false;
    }
    loaded = true;
    const uint32_t bad = fzx_menu_tables_low_check(true);
    if (bad) {
        loaded = false;
        fzx_menu_tables_low_status = 20u + bad;
        return false;
    }
    fzx_menu_tables_low_status = 1;
    return true;
}
