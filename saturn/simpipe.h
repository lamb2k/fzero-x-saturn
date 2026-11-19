/* Sim-pipeline checkpoint 3 (FZX_SIMPIPE_DBUF, default 0): serial double
 * buffering of the publication outputs (generated/SIM-PIPELINE-PLAN.md).
 * Everything lives in low work RAM, carved after the records allocation and
 * before the frame profile: a control block (layout report, part table,
 * sequence), two guarded slots and a guarded six-tick job block. High work
 * RAM holds only the pointer fzx_simpipe_area and the copy code, because each
 * byte there comes out of the master stack (C-036). One CPU: exact. */
#ifndef FZX_SIMPIPE_H
#define FZX_SIMPIPE_H
#include <stdint.h>
#include "fzx.h"

#define FZX_SIMPIPE_AREA_MAGIC 0x53504442u  /* 'SPDB' */
#define FZX_SIMPIPE_SLOT_MAGIC 0x53504c54u  /* 'SPLT' */
#define FZX_SIMPIPE_JOB_MAGIC 0x53504a42u   /* 'SPJB' */
#define FZX_SIMPIPE_GUARD 0x5a17c3e9u
#define FZX_SIMPIPE_MAX_TICKS 6u            /* five PAL fields give six 60 Hz ticks (C-033) */
#define FZX_SIMPIPE_MAX_PARTS 24u
/* The road loader's CD file list (7,168 B in high WRAM otherwise) lives in a
 * reservation at the top of low WRAM: guard, list, guard. The road loads once,
 * after the menus have released their scratch and before any race allocation,
 * and nothing else (race allocations, this area, the frame profile) ever
 * reaches the reservation. */
#define FZX_SIMPIPE_ROAD_LIST_BYTES 7168u
#define FZX_SIMPIPE_TOP_RESERVE_BYTES (32u + FZX_SIMPIPE_ROAD_LIST_BYTES + 32u)
#define FZX_SIMPIPE_TOP_RESERVE_BASE (0x00300000u - FZX_SIMPIPE_TOP_RESERVE_BYTES)
#define FZX_SIMPIPE_ROAD_LIST (FZX_SIMPIPE_TOP_RESERVE_BASE + 32u)

/* One ordinary (non-volatile) draw-facing publication output: the core's
 * descriptor type, shared rather than redeclared (C-037). */
typedef fzx_part_t fzx_simpipe_part_t;

/* The volatile publication words (statuses and the road-plan sequence) are
 * not parts: they travel as typed values in the slot header, moved with
 * volatile loads and stores (C-037). */
#define FZX_SIMPIPE_VOLATILE_WORDS 5u

/* Slot header; the ordinary parts follow in table order, each 4-byte aligned. */
typedef struct {
    uint32_t magic, generation, sequence, ticks;
    uint32_t words[FZX_SIMPIPE_VOLATILE_WORDS];
} fzx_simpipe_slot_t;

/* The job a slave would run for frame N+1 (sized here; used from checkpoint 4). */
typedef struct {
    uint32_t magic, generation, sequence, ticks;
    uint32_t words[FZX_SIMPIPE_MAX_TICKS];      /* buttons<<16 | stick x<<8 | stick y */
    uint32_t connected[FZX_SIMPIPE_MAX_TICKS];  /* digital.connected per tick */
    uint32_t status, done, reserved[2];
} fzx_simpipe_job_t;

/* Control block at the start of the area. report: magic 'SPDB', version, area
 * base, slot0, slot1, job, area end, slot bytes, job bytes, parts, payload
 * bytes per slot, guard trips, sequence mismatches, copy-outs, copy-ins, stale
 * copy-ins (control only), status (1 ready, 2 carve failed, 3 guard, 4 slot),
 * then the tick binding: presented frames whose slot tick differs from the
 * tick count at presentation, and the first such {tick count, slot tick}.
 * dynamic: parts whose address is known only at run time (countdown scene). */
typedef struct {
    uint32_t report[20];
    uint32_t sequence, generation;
    fzx_simpipe_part_t dynamic[2];
} fzx_simpipe_area_t;

extern fzx_simpipe_area_t *fzx_simpipe_area;
#endif
