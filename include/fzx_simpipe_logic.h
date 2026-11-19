/* Sim-pipeline checkpoint 4 (FZX_SIM_PIPELINE): the CPU-independent rules,
 * compiled for the Saturn and for HOST tests (tests/test_simpipe_logic.py).
 * generated/SIM-PIPELINE-PLAN.md, C-033/C-035/C-038/C-039. */
#ifndef FZX_SIMPIPE_LOGIC_H
#define FZX_SIMPIPE_LOGIC_H
#include <stdbool.h>
#include <stdint.h>

#define FZX_SIMPIPE_JOB_TICKS 6u   /* five PAL fields give six 60 Hz ticks */

/* ---- Independent expected-tick count (master) -------------------------
 * K is the loop's own tick count from its field arithmetic. Only in replay
 * mode do the replay and log bounds apply: no tick runs once the log index
 * reaches the replay count or the log size. The slave's report never enters
 * this computation. */
uint32_t fzx_simpipe_expected_ticks(uint32_t k, uint32_t replay_count, uint32_t input_count,
                                    uint32_t log_ticks);

/* A batch result as the slave reports it: ticks executed, and whether it
 * stopped before a natural transition (the tick at index EXECUTED then runs
 * once on the master, in source order, followed by the rest serially). */
typedef struct { uint32_t executed; uint32_t cut; } fzx_simpipe_batch_t;
/* 0 = consistent with EXPECTED; otherwise the first rule broken:
 * 1 executed > expected, 2 a short batch without a cut, 3 a cut at or after
 * the expected end, 4 expected beyond the job size. */
uint32_t fzx_simpipe_batch_check(uint32_t expected, const fzx_simpipe_batch_t *b);

/* ---- Intended-submission FIFO and the observed swap ---------------------
 * The master records each intended submission {generation, bank, sequence,
 * executed tick} when it hands a command list to VDP1. The VBlank handler
 * observes the swap and the tag written into the swapped bank; it must equal
 * the oldest pending submission, which it then consumes. A field without a
 * swap re-presents the same frame and consumes nothing. */
typedef struct { uint32_t generation, bank, sequence, tick; } fzx_simpipe_tag_t;
#define FZX_SIMPIPE_FIFO 4u
typedef struct {
    fzx_simpipe_tag_t pending[FZX_SIMPIPE_FIFO];
    uint32_t head, count, generation, next_sequence;
    fzx_simpipe_tag_t presented;   /* last consumed */
    uint32_t presented_valid, repeats, swaps, errors, last_error;
} fzx_simpipe_fifo_t;
enum {
    FZX_SIMPIPE_FIFO_OK = 0,
    FZX_SIMPIPE_FIFO_FULL = 1,          /* submit with no free entry */
    FZX_SIMPIPE_FIFO_BANK_PENDING = 2,  /* submit would overwrite a pending tag on that bank */
    FZX_SIMPIPE_FIFO_GENERATION = 3,    /* tag from another generation */
    FZX_SIMPIPE_FIFO_SEQUENCE = 4,      /* submit sequence not the next one */
    FZX_SIMPIPE_FIFO_EMPTY = 5,         /* swap observed with nothing pending */
    FZX_SIMPIPE_FIFO_MISMATCH = 6,      /* observed tag differs from the oldest pending */
    FZX_SIMPIPE_FIFO_TICK_ORDER = 7,    /* presented tick moved backwards in a generation */
};
void fzx_simpipe_fifo_reset(fzx_simpipe_fifo_t *f, uint32_t generation);
uint32_t fzx_simpipe_fifo_submit(fzx_simpipe_fifo_t *f, const fzx_simpipe_tag_t *t);
/* SWAPPED false: a repeat field. Otherwise OBSERVED is the swapped bank's tag. */
uint32_t fzx_simpipe_fifo_observe(fzx_simpipe_fifo_t *f, bool swapped, const fzx_simpipe_tag_t *observed);

/* ---- Drain and resume ----------------------------------------------------
 * Ownership of the simulation state: RUNNING (pipelined; a job may be
 * outstanding), DRAINED (the master owns the state; no job outstanding;
 * caches purged), and RESUMING (drained; waiting for the first publication of
 * the new state before pipelining again). The hooks do the hardware work. */
typedef struct {
    uint32_t state, outstanding, generation, completed_sequence, drains, resumes, errors, last_error;
} fzx_simpipe_owner_t;
enum { FZX_SIMPIPE_RUNNING = 1, FZX_SIMPIPE_DRAINED = 2, FZX_SIMPIPE_RESUMING = 3 };
typedef struct {
    /* Bounded wait for the outstanding job; false on timeout. Never called
     * from the slave (the caller checks it is the master). */
    bool (*wait)(void *context, uint32_t *completed_sequence);
    void (*purge)(void *context);
    bool (*on_master)(void *context);
    void *context;
} fzx_simpipe_owner_hooks_t;
enum {
    FZX_SIMPIPE_OWNER_OK = 0,
    FZX_SIMPIPE_OWNER_TIMEOUT = 1,     /* bounded wait expired */
    FZX_SIMPIPE_OWNER_NOT_MASTER = 2,  /* drain or resume called off the master */
    FZX_SIMPIPE_OWNER_STATE = 3,       /* resume without a drain, or dispatch while drained */
    FZX_SIMPIPE_OWNER_NOT_READY = 4,   /* resume before the first new publication */
};
void fzx_simpipe_owner_init(fzx_simpipe_owner_t *o, uint32_t generation);
/* The master is about to dispatch a job (RUNNING only). */
uint32_t fzx_simpipe_owner_dispatch(fzx_simpipe_owner_t *o);
/* The master collected the job normally. */
void fzx_simpipe_owner_collected(fzx_simpipe_owner_t *o, uint32_t completed_sequence);
/* Bounded drain: complete any outstanding job, acquire its sequence, purge,
 * and hand the state to the master. Idempotent when already drained. A reset
 * passes NEW_GENERATION != current: the drained state then belongs to it. */
uint32_t fzx_simpipe_owner_drain(fzx_simpipe_owner_t *o, const fzx_simpipe_owner_hooks_t *h, uint32_t new_generation);
/* Resume pipelining: only after a drain, and only once FIRST_PUBLICATION_READY
 * reports the new state's first serial publication. */
uint32_t fzx_simpipe_owner_resume(fzx_simpipe_owner_t *o, const fzx_simpipe_owner_hooks_t *h,
                                  bool first_publication_ready);
#endif
