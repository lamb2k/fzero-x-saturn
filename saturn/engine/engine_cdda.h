/* Engine CD audio (ENG_CDDA): music as Red Book tracks played by the CD block.
 * The SH-2 never touches the audio data: the drive streams it into the SCSP's CD input.
 * See generated/SOUND-PLAN.md (design) and tools/bgm_cdda.py / tools/disc_cdda.py (disc). */
#ifndef ENGINE_CDDA_H
#define ENGINE_CDDA_H

#include <stdint.h>

/* Read by captures (EMULATOR evidence): what the player did and saw. */
typedef struct engine_cdda_stats {
    uint32_t state;          /* engine_cdda_state_t */
    uint32_t track;          /* CD track playing */
    uint32_t track_fad;      /* track INDEX 01 from the TOC */
    uint32_t loop_fad;       /* loop-back sector, absolute */
    uint32_t end_fad;        /* first sector after the track */
    uint32_t commands;       /* Play Disc commands sent */
    uint32_t loop_backs;     /* times the loop range was (re)started by command */
    uint32_t last_status;    /* periodic CD status byte */
    uint32_t last_fad;       /* periodic pickup position */
    uint32_t busy_frames;    /* frames a command waited for CMOK */
    uint32_t init_result;    /* 1 ok; >= 16 a failure code */
} engine_cdda_stats_t;

typedef enum {
    ENGINE_CDDA_OFF = 0,
    ENGINE_CDDA_INTRO_SENT,
    ENGINE_CDDA_INTRO_PLAYING,
    ENGINE_CDDA_LOOP_SENT,
    ENGINE_CDDA_LOOPING,
    ENGINE_CDDA_FAILED
} engine_cdda_state_t;

extern volatile engine_cdda_stats_t engine_cdda_stats;

/* Sound power-up: the 68000 parked on an idle loop, SCSP master volume, CD input level.
 * Call from user_init before smpc_peripheral_init (no INTBACK may be pending). */
void engine_cdda_power(void);
/* After the last CD data load: read the TOC and start music id `music_id`.
 * start_offset_sectors > 0 starts that many sectors before the track end (loop-back test). */
int engine_cdda_start(uint32_t music_id, uint32_t start_offset_sectors);
/* Once per frame: never waits; a busy CD block gets its command next frame. */
void engine_cdda_service(void);

#endif
