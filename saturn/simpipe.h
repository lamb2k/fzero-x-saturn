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

#define FZX_SIMPIPE_AREA_MAGIC 0x53504442u  /* 'SPDB' */
#define FZX_SIMPIPE_SLOT_MAGIC 0x53504c54u  /* 'SPLT' */
#define FZX_SIMPIPE_JOB_MAGIC 0x53504a42u   /* 'SPJB' */
#define FZX_SIMPIPE_GUARD 0x5a17c3e9u
#define FZX_SIMPIPE_MAX_TICKS 6u            /* five PAL fields give six 60 Hz ticks (C-033) */
#define FZX_SIMPIPE_MAX_PARTS 24u

/* One draw-facing publication output: its address and size. */
typedef struct { void *address; uint32_t bytes; } fzx_simpipe_part_t;

/* Slot header; the parts follow in table order, each 4-byte aligned. */
typedef struct { uint32_t magic, generation, sequence, ticks; } fzx_simpipe_slot_t;

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
 * copy-ins (control only), status (1 ready, 2 carve failed, 3 guard, 4 slot). */
typedef struct {
    uint32_t report[17];
    uint32_t sequence, generation, parts;
    fzx_simpipe_slot_t *slot[2];
    fzx_simpipe_job_t *job;
    volatile uint32_t *guard[4];
    fzx_simpipe_part_t part[FZX_SIMPIPE_MAX_PARTS];
} fzx_simpipe_area_t;

extern fzx_simpipe_area_t *fzx_simpipe_area;
#endif
