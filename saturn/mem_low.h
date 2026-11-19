/* Master-stack headroom: low-WRAM reservations taken out of high WRAM
 * (generated/MEM-HEADROOM-PLAN.md, C-046). All knobs default 0; with every
 * knob off the scratch ceiling below is the literal 0x00300000u it replaces,
 * so the knob-off images are unchanged.
 *
 * Low-WRAM top, high to low:
 *   [0x002FE3C0, 0x00300000)  road file-list reservation: guard, 7,168 B list,
 *                             guard (sim-pipeline checkpoint 3 geometry; used
 *                             by FZX_SIMPIPE_DBUF or FZX_MEM_ROADLIST_LOW)
 *   [0x002EE580, 0x002FE3C0)  ghost reservation (FZX_MEM_GHOSTS_LOW): guard,
 *                             65,024 B payload for the closure's .bss.gGhosts
 *                             and .bss.sGhostReplayRecordingBuffer (64,968 B,
 *                             placed by mem_ghosts_low.ld), guard
 * The ghost arrays keep BSS lifetime: zeroed once at boot, before any low-WRAM
 * scratch user runs, and never again, so a ghost saved at one finish is still
 * there for the next race. Every low-WRAM scratch and allocation bound uses
 * FZX_LOW_SCRATCH_TOP, which stops below the ghost reservation when it is on. */
#ifndef FZX_MEM_LOW_H
#define FZX_MEM_LOW_H
#include <stdint.h>

#ifndef FZX_MEM_GHOSTS_LOW
#define FZX_MEM_GHOSTS_LOW 0
#endif
#ifndef FZX_MEM_ROADLIST_LOW
#define FZX_MEM_ROADLIST_LOW 0
#endif

#define FZX_MEM_LOW_TOP 0x00300000u
#define FZX_MEM_ROADLIST_RESERVE_BYTES (32u + 7168u + 32u)
#define FZX_MEM_ROADLIST_RESERVE_BASE (FZX_MEM_LOW_TOP - FZX_MEM_ROADLIST_RESERVE_BYTES) /* 0x002FE3C0 */
#define FZX_MEM_ROADLIST_BYTES 7168u
#define FZX_MEM_ROADLIST (FZX_MEM_ROADLIST_RESERVE_BASE + 32u)
#define FZX_MEM_ROADLIST_GUARD 0x5a17c3e9u /* the checkpoint-3 guard word, unchanged */

#define FZX_MEM_GUARD 0x6d3a1c55u
/* Payload room for the two arrays: 48,768 + 16,200 = 64,968 B, rounded up to
 * a 32-byte multiple. mem_ghosts_low.ld asserts the exact 64,968 B and these
 * bounds; the boot check compares the linker symbols with these constants. */
#define FZX_MEM_GHOSTS_PAYLOAD_BYTES 65024u
#define FZX_MEM_GHOSTS_EXPECTED_BYTES 64968u
#define FZX_MEM_GHOSTS_RESERVE_BYTES (32u + FZX_MEM_GHOSTS_PAYLOAD_BYTES + 32u)
#define FZX_MEM_GHOSTS_RESERVE_BASE (FZX_MEM_ROADLIST_RESERVE_BASE - FZX_MEM_GHOSTS_RESERVE_BYTES) /* 0x002EE580 */
#define FZX_MEM_GHOSTS_START (FZX_MEM_GHOSTS_RESERVE_BASE + 32u)                                    /* 0x002EE5A0 */
#define FZX_MEM_GHOSTS_GUARD_HIGH (FZX_MEM_GHOSTS_START + FZX_MEM_GHOSTS_PAYLOAD_BYTES)              /* 0x002FE3A0 */

/* Highest address any low-WRAM scratch user or allocation may reach. */
#if FZX_MEM_GHOSTS_LOW
#define FZX_LOW_SCRATCH_TOP FZX_MEM_GHOSTS_RESERVE_BASE
#else
#define FZX_LOW_SCRATCH_TOP 0x00300000u
#endif

#if FZX_MEM_GHOSTS_LOW
/* 1 intact, 2 linker placement differs from these constants, 3 a guard word
 * changed. Written at boot and by every check. */
extern volatile uint32_t fzx_mem_ghosts_status;
extern volatile uint32_t fzx_mem_ghosts_checks;
void fzx_mem_ghosts_boot(void);
uint32_t fzx_mem_ghosts_check(void);
#endif
#endif
