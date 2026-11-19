/* Engine: selective roster load (ENG_ROSTER). A LOAD phase for the rival field.
 *
 * The roster stays compressed on disc and in the cartridge: ROSTERZ.PAK is Codex's RSP1
 * 1.1 roster pack (ROSTER.PAK, SHA-256 checked when the identity is generated) recoded
 * as RSZ1 by tools/racer_stream.py (each cell a cell4z stream, SH2ZAM sprite-cache-1).
 * The load reads only the near and far sets of the families in this race's field: per
 * set one contiguous block (header, cell table, CLUTs, stream table, alias map, streams),
 * read as a byte range of the file and checked against its FNV-1a from
 * generated/roster/rosterz_identity.h (tools/roster_select_identity.py) as the sectors
 * arrive; a wrong block refuses the load. Cells are expanded one at a time, when the
 * sprite slot cache claims a slot (fzx_roster_cell_decode into the slot's VRAM).
 *
 * For claude/e2-field: replace the whole-pack fzx_texture_cd_load("ROSTER.PAK") in
 * fzx_drive_roster_load with fzx_roster_select_load(field families), take each rival's
 * set header from fzx_roster_set (cell table and CLUTs as before), and in the slot fill
 * decode instead of copying: fzx_roster_cell_source gives the cell to fetch and whether
 * to flip it (the RSZ1 alias map), fzx_roster_cell_decode writes it into the slot. */
#ifndef FZX_ENGINE_ROSTER_H
#define FZX_ENGINE_ROSTER_H
#include <stdbool.h>
#include <stdint.h>
#include "sh2zam/rotation_sprite_experimental.h"
#include "sh2zam/sprite_cache_experimental.h"

#define FZX_ROSTER_NEAR 0u
#define FZX_ROSTER_FAR  1u
/* Zero bytes after each block: the decoder's read slack (SH2Z_CELL4Z_SLACK, widest cell). */
#define FZX_ROSTER_SLACK 1024u

/* Reads the near and far sets of `families` (stock families 0..29) into the cartridge from
 * cart_base. [0] status (1 ok; 2 code lengths, 3 tables, 4 family missing, 5 no room,
 * 6 a block refused: [7] its set), [1] sets, [2] bytes, [3] start, [4] end. */
bool fzx_roster_select_load(const uint8_t *families, unsigned n, uintptr_t cart_base, uintptr_t cart_limit,
                            void *scratch, uint32_t scratch_bytes);
extern volatile uint32_t fzx_roster_report[16];
/* The set's header (cell table, CLUTs) in the cartridge, NULL if not loaded. */
const sh2z_rspr_set_t *fzx_roster_set(uint32_t family, unsigned prof);
/* The cell whose stream to decode for `cell`, and whether it is drawn flipped. */
bool fzx_roster_cell_source(uint32_t family, unsigned prof, uint32_t cell, uint32_t *src_cell, bool *flip);
/* Decodes a non-alias cell (4 bpp, the RSP1 1.1 pixels) into dst. */
bool fzx_roster_cell_decode(uint32_t family, unsigned prof, uint32_t cell, volatile uint16_t *dst);
#endif
