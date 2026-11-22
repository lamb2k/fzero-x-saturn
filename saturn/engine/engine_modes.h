/* Race modes (ENG_MODES=1; plan P3 item 3): what each menu mode asks of the race, in one table.
 * Plain C: the same file builds for HOST tests. MIT; see ../../LICENSE.
 *
 * menu_mode is the US rev0 ModeSelect ID the session carries (SCENES.md section 3): GP 0, Time Attack 1,
 * Death Race 2, VS 3, Practice 5. Source facts (decomp b944a413):
 * - Time Attack races alone (racer.c: gTotalRacers = 1); every other race mode races 30.
 * - Practice and Death Race never finish at the lap count (racer.c ~5254: the lap wraps); Death Race ends
 *   when its 29 rivals are retired (racer.c ~979) and records that time.
 * - Death Race is Master, one player, rivals at half energy (racer.c ~2230).
 * - Only Time Attack and Death Race keep times (save.c); Grand Prix and Practice keep none. */
#ifndef FZX_ENGINE_MODES_H
#define FZX_ENGINE_MODES_H
#include <stdbool.h>
#include <stdint.h>

enum { FZX_MODE_GP = 0, FZX_MODE_TA = 1, FZX_MODE_DEATH = 2, FZX_MODE_VS = 3, FZX_MODE_PRACTICE = 5 };
#define FZX_MODE_NO_RECORD 0xFFu
#define FZX_MODE_REC_TA 0u    /* = FZX_REC_TA, FZX_REC_DEATH_RACE (engine_records.h; checked in engine_scenes.c) */
#define FZX_MODE_REC_DEATH 1u

typedef struct {
    uint8_t menu_mode;
    uint8_t field;        /* the rival field races (0: the player alone) */
    uint8_t endless;      /* no finish at the lap count: laps keep counting */
    uint8_t class_pick;   /* SELECT chooses the class; else forced_class */
    uint8_t forced_class; /* 0 Novice .. 3 Master, when !class_pick */
    uint8_t cup;          /* the Grand Prix cup manager runs the rounds */
    uint8_t record;       /* FZX_MODE_REC_TA / _DEATH, or FZX_MODE_NO_RECORD */
    uint8_t avail;        /* the catalog bit the course needs (FZX_AVAIL_TA / FZX_AVAIL_GP) */
    uint16_t rival_energy_q8; /* rivals' energy, 256 = as the field has it */
    uint8_t win_all_retired;  /* the race is won when every rival is retired (Death Race) */
} fzx_mode_rules_t;

/* NULL for a mode the engine does not run (Versus, Options, unknown IDs). */
const fzx_mode_rules_t *fzx_mode_rules(uint32_t menu_mode);
/* The class the field races: the session's difficulty when the mode lets SELECT choose, else the mode's own. */
uint32_t fzx_mode_class(uint32_t menu_mode, uint32_t session_difficulty);
/* Records: true when a finished run of this mode may rank in a table (fzx_mode_rules()->record != none). */
bool fzx_mode_keeps_times(uint32_t menu_mode);
#endif
