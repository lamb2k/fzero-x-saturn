#ifndef FZX_MENU_TABLES_LOW_H
#define FZX_MENU_TABLES_LOW_H
/* FZX_MENU_TABLES_LOW=1: the four menu image tables live in reserved low work
 * RAM (generated/menu_tables/menu_tables_identity.h holds the one layout).
 * They are loaded once at boot from MENUTAB.PAK and never written again. */
#include <stdbool.h>
#include <stdint.h>
#include "menu_tables_identity.h"

/* Fixed-address view of a relocated table. PROTO is the generated static
 * array, used only for its type (the static copy is never referenced). */
#define FZX_MENU_TABLE(proto, addr) (*(const __typeof__(proto) *)(uintptr_t)(addr))

/* Bits returned by fzx_menu_tables_low_check (0 = intact). */
enum {
    FZX_MENU_TABLES_NOT_LOADED = 1u << 0, /* boot load did not complete */
    FZX_MENU_TABLES_GUARD_BAD = 1u << 1,  /* a guard slot's pattern words changed */
    FZX_MENU_TABLES_SLOT_PAD_BAD = 1u << 2, /* a guard slot's zero half changed */
    FZX_MENU_TABLES_TABLE_PAD_BAD = 1u << 3, /* zero padding after a table changed */
    FZX_MENU_TABLES_SPARE_BAD = 1u << 4,  /* the zeroed spare up to the stand-ins changed */
    FZX_MENU_TABLES_HASH_BAD = 1u << 5,   /* full check only: resident bytes differ from the pack */
};

/* Boot only, once, before the title. Checks the reservation against the
 * stand-in base, zeroes the spare, loads MENUTAB.PAK through the bounded CD
 * loader (exact size, FNV-1a, every store verified) and verifies the result.
 * Returns true on success. fzx_menu_tables_low_status: 1 loaded; 2 bad
 * reservation/stand-in overlap; 3 + texture_cd status on load failure;
 * 20 + check bits when the loaded region fails verification. */
bool fzx_menu_tables_low_load(void *scratch, uint32_t scratch_bytes);

/* Side-effect free and bounded (reads at most FZX_MENU_TABLES_RESERVED bytes;
 * writes nothing; never reloads, reinitialises or reseals). Verifies the
 * still-resident tables: every guard slot, slot padding, table padding and
 * the spare; with full=true also the FNV-1a of the whole pack image against
 * FZX_MENU_TABLES_HASH (about 44 KB read). Returns 0 when intact, otherwise
 * the bits above. Safe in any scene after boot; the ordinary-retry call site
 * is owned by C8 and its qualification is pending. */
uint32_t fzx_menu_tables_low_check(bool full);

extern volatile uint32_t fzx_menu_tables_low_status;
/* Last result recorded by main.c's boot hook (0 = intact). */
extern volatile uint32_t fzx_menu_tables_low_report;
#endif
