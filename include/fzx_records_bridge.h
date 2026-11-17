#ifndef FZX_RECORDS_BRIDGE_H
#define FZX_RECORDS_BRIDGE_H
#include "fzx_records.h"

/* Read the original controller and RNG after the completed source tick. */
bool ref_records_input(fzx_records_input_t *,uint32_t frame,bool connected);
/* Publish RNG consumed by a successful records step. Reject a stale input
 * boundary rather than overwrite intervening source randomness. Call once,
 * immediately after stepping, only when the returned rng_calls is nonzero. */
bool ref_records_commit_rng(const fzx_records_input_t *,const fzx_records_snapshot_t *);
#endif
