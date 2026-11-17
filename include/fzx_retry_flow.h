/* Bounded ordinary Time Attack retry scheduling. Source-only integration seam.
 * Events are required calls, not evidence that their implementations ran. */
#ifndef FZX_RETRY_FLOW_H
#define FZX_RETRY_FLOW_H
#include <stdbool.h>
#include <stdint.h>

enum fzx_retry_event {
    FZX_RETRY_SETTLE_CONTROLLERS_LIVES = 1,
    FZX_RETRY_AUDIO_RESTORE,
    FZX_RETRY_AUDIO_MODE,
    FZX_RETRY_OBJECTS_RESET,
    FZX_RETRY_HUD_RESET,
    FZX_RETRY_CONTROLLER_RESET,
    FZX_RETRY_RACER_INIT,
    FZX_RETRY_CAMERA_INIT,
    FZX_RETRY_EFFECTS_INIT,
    FZX_RETRY_GADGETS_RESET,
    FZX_RETRY_CLEAR_PAUSE,
    FZX_RETRY_MENUS_INIT,
    FZX_RETRY_LIVES_COUNTER_RESET,
    FZX_RETRY_PORTRAITS_INIT,
    /* Menus_Update's retry dispatch has run before this event. The consumer
     * continues the other Race_Update calls, including racer/camera updates. */
    FZX_RETRY_RACE_UPDATE_REMAINDER
};
#define FZX_RETRY_EVENT_MAX 16
typedef struct {
    int32_t work0, work1, work2, work3, work4;
} fzx_retry_fade_info_t;
typedef struct {
    int32_t activeTransitionType, queuedTransitionType, state;
    int16_t timer, argument;
    uint16_t appearType, flags;
    fzx_retry_fade_info_t transitionInfo;
} fzx_retry_fade_t;
typedef struct {
    uint32_t frame, tick, reload_count;
    int32_t mode_state, menu_change, menu_flags, triggered, delay;
    int32_t transition_state;
    fzx_retry_fade_t fade;
    uint8_t events[FZX_RETRY_EVENT_MAX], event_count, initialized, fault;
} fzx_retry_flow_t;
typedef struct {
    uint32_t seed1, mask1, seed2, mask2, calls1, calls2;
} fzx_retry_rng_t;

/* The caller must prove single-player US Time Attack/course 0, no title demo,
 * no active transition/argument, no save dialog, retry as the only menu flag,
 * zero dispatch delay, and a source-confirmed retry request on confirm_frame.
 * These are preconditions, not fabricated checks of hidden source globals. */
bool fzx_retry_flow_begin(fzx_retry_flow_t *out, uint32_t confirm_frame);
/* Exactly one step at confirm_frame+1 and each following source tick.
 * Atomic packet generation; the consumer must execute every event in order.
 * This does not mutate race state, RNG, tuning, records or controller inputs. */
bool fzx_retry_flow_step(fzx_retry_flow_t *state, uint32_t frame);
/* Run only at original Menus_Init position, after earlier source init calls.
 * Separate from flow_step: earlier Racer_Init may itself consume live RNG.
 * On the first race this repairs missing history at initialization, never at
 * finish. On retry use the live words, never saved diagnostic restart words. */
bool fzx_retry_menus_init_rng(fzx_retry_rng_t *rng);
#endif
