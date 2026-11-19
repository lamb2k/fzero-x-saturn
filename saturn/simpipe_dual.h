/* FZX_SIM_PIPELINE=1: the slave SH-2 job channel for the simulation pipeline
 * (generated/SIM-PIPELINE-PLAN.md, checkpoint 4). One owner: under this knob
 * the slave runs only these jobs (FZX_SLAVE_ROAD is off; the road is drawn on
 * the master). Same guarded stack reservation and handshake as slave_road.c. */
#ifndef FZX_SIMPIPE_DUAL_H
#define FZX_SIMPIPE_DUAL_H
#include <yaul.h>

typedef void (*fzx_simpipe_job_fn)(void *arg);

/* Counters, read by captures: jobs, late jobs (over 4 fields), master wait
 * ticks (sum, max), slave busy ticks (sum, max), slave stack bytes never used
 * (minimum), status (1 ready, 7 guard tripped, 8 stack not at the reservation,
 * 9 timeout, 10 started while busy), guard trips, lowest slave SP. */
extern volatile uint32_t fzx_simpipe_dual_jobs,fzx_simpipe_dual_late;
extern volatile uint32_t fzx_simpipe_dual_wait_sum,fzx_simpipe_dual_wait_max;
extern volatile uint32_t fzx_simpipe_dual_busy_sum,fzx_simpipe_dual_busy_max;
extern volatile uint32_t fzx_simpipe_dual_stack_unused,fzx_simpipe_dual_status;
extern volatile uint32_t fzx_simpipe_dual_guard_trips,fzx_simpipe_dual_sp;

/* Boot, once. */
void fzx_simpipe_dual_init(void);
/* Master: hand FN(ARG) to the slave. Refused (status 10) while a job is out. */
bool fzx_simpipe_dual_start(fzx_simpipe_job_fn fn, void *arg);
/* Master: is a job outstanding (started and not yet collected)? */
bool fzx_simpipe_dual_outstanding(void);
/* Master: bounded wait for the outstanding job, then guards, then purge the
 * master cache so the slave's writes are seen. Returns false on timeout or a
 * guard trip (fail-stop with the status). True also when nothing was out. */
bool fzx_simpipe_dual_collect(const volatile uint32_t *video_fields);
#endif
