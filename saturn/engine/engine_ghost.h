/* Time Attack ghost (ENG_GHOST=1, needs ENG_MODES=1; plan P3 item 3, step F). Plain C, no hardware: builds for the HOST fixture
 * (tests/ghost_driver.c). MIT; see ../../LICENSE.
 *
 * Source (racer.c, save.c): Time Attack records the player's position every second physics tick (30 Hz), as the difference to the
 * previous sample, one signed byte per axis (three bytes when the difference leaves -127..127), at most 16200 bytes; the ghost
 * plays it back with half the difference added on the ticks between. The save keeps one ghost.
 * Here the stream is the second difference in nibbles (0 = escape, 1..15 = -7..+7, escape followed by a signed 16-bit value as four
 * nibbles), so a Mute City race is about 4.7 KB and no course passes 5.8 KB (the original format: 8 to 11.4 KB). Positions are whole
 * world units (the player's Q16 position rounded), as the source rounds them. The recording stops, and the ghost is not kept, when
 * the stream would pass FZX_GHOST_STREAM_MAX. */
#ifndef FZX_ENGINE_GHOST_H
#define FZX_ENGINE_GHOST_H
#include <stdbool.h>
#include <stdint.h>

#define FZX_GHOST_STREAM_MAX 12000u
#define FZX_GHOST_MAGIC 0x465A5847u /* "FZXG" */
typedef struct {
    uint32_t magic, version, generation, course, race_ms, samples, bytes, crc; /* crc over everything after it */
    int32_t start[3];                    /* the first sample, whole units */
    uint8_t stream[FZX_GHOST_STREAM_MAX];
} fzx_ghost_t;
#define FZX_GHOST_HEAD_BYTES 44u        /* offsetof(fzx_ghost_t, stream) */
#define FZX_GHOST_FILE_BYTES(g) (FZX_GHOST_HEAD_BYTES + (g)->bytes)

/* Recording (one call per physics tick while the race runs; the recorder takes every second tick). */
void fzx_ghost_rec_begin(fzx_ghost_t *g, uint32_t course);
void fzx_ghost_rec_tick(fzx_ghost_t *g, const int32_t pos_q16[3]);
/* The race ended at RACE_MS: true when the ghost is complete (not overrun) and stamped (crc). */
bool fzx_ghost_rec_end(fzx_ghost_t *g, uint32_t race_ms);
bool fzx_ghost_rec_overrun(const fzx_ghost_t *g);

/* A stored ghost: structure, size, crc and a whole decode (the sample count) all agree. SIZE is the file size. */
bool fzx_ghost_valid(const fzx_ghost_t *g, uint32_t size);
/* Sets the generation and refreshes the crc (the storage stamps a ghost as it writes it). */
void fzx_ghost_stamp(fzx_ghost_t *g, uint32_t generation);

/* Playback (one call per physics tick from the race's first tick): the ghost's position this tick in Q16. False once it has ended. */
typedef struct { const fzx_ghost_t *g; uint32_t tick, nib, left; int32_t cur[3], next[3], dprev[3]; bool live; } fzx_ghost_play_t;
void fzx_ghost_play_begin(fzx_ghost_play_t *p, const fzx_ghost_t *g);
bool fzx_ghost_play_tick(fzx_ghost_play_t *p, int32_t out_q16[3]);

/* Race side (engine_ghost_race.c, ENG_GHOST=1 on the scenes image). */
#ifdef FZX_GHOST_RACE_API
#include "engine_records.h"
#include "sh2zam/vehicle_dynamics_experimental.h"
void fzx_ghost_race_begin(void);                              /* every race start (drive reset): loads the kept ghost */
void fzx_ghost_tick(const sh2z_vd_body_t *body);              /* every physics tick */
fzx_rec_result_t fzx_ghost_race_save(const fzx_run_result_t *run); /* a new best was committed */
extern volatile uint32_t fzx_ghost_on, fzx_ghost_report[16];
extern volatile int32_t fzx_ghost_pos[3];
#endif
#endif
