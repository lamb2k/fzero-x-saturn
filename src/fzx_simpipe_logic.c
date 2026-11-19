/* Sim-pipeline checkpoint 4: CPU-independent rules (see fzx_simpipe_logic.h). */
#include "fzx_simpipe_logic.h"

uint32_t fzx_simpipe_expected_ticks(uint32_t k, uint32_t replay_count, uint32_t input_count,
                                    uint32_t log_ticks)
{
    if (replay_count == 0) return k;              /* live, autopilot or forced: every dispatch ticks */
    const uint32_t bound = replay_count < log_ticks ? replay_count : log_ticks;
    if (input_count >= bound) return 0;
    const uint32_t room = bound - input_count;
    return k < room ? k : room;
}

uint32_t fzx_simpipe_batch_check(uint32_t expected, const fzx_simpipe_batch_t *b)
{
    if (expected > FZX_SIMPIPE_JOB_TICKS) return 4;
    if (b->executed > expected) return 1;
    if (b->cut) return b->executed < expected ? 0 : 3;
    return b->executed == expected ? 0 : 2;
}

static bool tag_equal(const fzx_simpipe_tag_t *a, const fzx_simpipe_tag_t *b)
{
    return a->generation == b->generation && a->bank == b->bank && a->sequence == b->sequence && a->tick == b->tick;
}

static uint32_t fifo_fail(fzx_simpipe_fifo_t *f, uint32_t e) { ++f->errors; f->last_error = e; return e; }

void fzx_simpipe_fifo_reset(fzx_simpipe_fifo_t *f, uint32_t generation)
{
    /* A reset drops every pending tag: nothing crosses into the new generation. */
    f->head = 0; f->count = 0; f->generation = generation; f->next_sequence = 1;
    f->presented_valid = 0; f->presented = (fzx_simpipe_tag_t){0, 0, 0, 0};
}

uint32_t fzx_simpipe_fifo_submit(fzx_simpipe_fifo_t *f, const fzx_simpipe_tag_t *t)
{
    if (t->generation != f->generation) return fifo_fail(f, FZX_SIMPIPE_FIFO_GENERATION);
    if (t->sequence != f->next_sequence) return fifo_fail(f, FZX_SIMPIPE_FIFO_SEQUENCE);
    for (uint32_t i = 0; i < f->count; ++i)
        if (f->pending[(f->head + i) % FZX_SIMPIPE_FIFO].bank == t->bank)
            return fifo_fail(f, FZX_SIMPIPE_FIFO_BANK_PENDING);
    if (f->count == FZX_SIMPIPE_FIFO) return fifo_fail(f, FZX_SIMPIPE_FIFO_FULL);
    f->pending[(f->head + f->count) % FZX_SIMPIPE_FIFO] = *t;
    ++f->count; ++f->next_sequence;
    return FZX_SIMPIPE_FIFO_OK;
}

uint32_t fzx_simpipe_fifo_observe(fzx_simpipe_fifo_t *f, bool swapped, const fzx_simpipe_tag_t *observed)
{
    if (!swapped) { ++f->repeats; return FZX_SIMPIPE_FIFO_OK; }  /* repeat field: consume nothing */
    if (!f->count) return fifo_fail(f, FZX_SIMPIPE_FIFO_EMPTY);
    const fzx_simpipe_tag_t *want = &f->pending[f->head];
    if (observed->generation != f->generation) return fifo_fail(f, FZX_SIMPIPE_FIFO_GENERATION);
    if (!tag_equal(observed, want)) return fifo_fail(f, FZX_SIMPIPE_FIFO_MISMATCH);
    if (f->presented_valid && want->tick < f->presented.tick) return fifo_fail(f, FZX_SIMPIPE_FIFO_TICK_ORDER);
    f->presented = *want; f->presented_valid = 1; ++f->swaps;
    f->head = (f->head + 1) % FZX_SIMPIPE_FIFO; --f->count;
    return FZX_SIMPIPE_FIFO_OK;
}

static uint32_t owner_fail(fzx_simpipe_owner_t *o, uint32_t e) { ++o->errors; o->last_error = e; return e; }

void fzx_simpipe_owner_init(fzx_simpipe_owner_t *o, uint32_t generation)
{
    *o = (fzx_simpipe_owner_t){ FZX_SIMPIPE_RUNNING, 0, generation, 0, 0, 0, 0, 0 };
}

uint32_t fzx_simpipe_owner_dispatch(fzx_simpipe_owner_t *o)
{
    if (o->state != FZX_SIMPIPE_RUNNING || o->outstanding) return owner_fail(o, FZX_SIMPIPE_OWNER_STATE);
    o->outstanding = 1;
    return FZX_SIMPIPE_OWNER_OK;
}

void fzx_simpipe_owner_collected(fzx_simpipe_owner_t *o, uint32_t completed_sequence)
{
    o->outstanding = 0; o->completed_sequence = completed_sequence;
}

uint32_t fzx_simpipe_owner_drain(fzx_simpipe_owner_t *o, const fzx_simpipe_owner_hooks_t *h, uint32_t new_generation)
{
    if (!h->on_master(h->context)) return owner_fail(o, FZX_SIMPIPE_OWNER_NOT_MASTER);
    if (o->outstanding) {
        uint32_t seq = 0;
        if (!h->wait(h->context, &seq)) return owner_fail(o, FZX_SIMPIPE_OWNER_TIMEOUT);
        o->outstanding = 0; o->completed_sequence = seq;
    }
    if (o->state == FZX_SIMPIPE_RUNNING) h->purge(h->context);
    o->state = FZX_SIMPIPE_DRAINED;
    if (new_generation != o->generation) o->generation = new_generation;
    ++o->drains;
    return FZX_SIMPIPE_OWNER_OK;
}

uint32_t fzx_simpipe_owner_resume(fzx_simpipe_owner_t *o, const fzx_simpipe_owner_hooks_t *h,
                                  bool first_publication_ready)
{
    if (!h->on_master(h->context)) return owner_fail(o, FZX_SIMPIPE_OWNER_NOT_MASTER);
    if (o->state != FZX_SIMPIPE_DRAINED && o->state != FZX_SIMPIPE_RESUMING) return owner_fail(o, FZX_SIMPIPE_OWNER_STATE);
    if (!first_publication_ready) { o->state = FZX_SIMPIPE_RESUMING; return owner_fail(o, FZX_SIMPIPE_OWNER_NOT_READY); }
    o->state = FZX_SIMPIPE_RUNNING; ++o->resumes;
    return FZX_SIMPIPE_OWNER_OK;
}
