/* Engine scenes (ENG_SCENES=1; saturn/engine/SCENES.md): the boot-to-reset loop.
 *
 * BOOT -> TITLE -> SELECT -> LOAD -> RACE -> RESULTS -> retry (LOAD -> RACE) or TITLE,
 * TITLE -> RECORDS -> TITLE, and the soft reset chord (A+B+C+Start) to TITLE from any
 * scene. Sequenced by SH2ZAM scene-1. TITLE, SELECT, RESULTS and RECORDS are
 * PLACEHOLDER screens in the original font until Codex's C8 screens bind; LOAD and
 * RACE are the engine's own. */
#ifndef FZX_ENGINE_SCENES_H
#define FZX_ENGINE_SCENES_H
#include <stdbool.h>
#include <stdint.h>

enum { FZX_SCN_BOOT, FZX_SCN_TITLE, FZX_SCN_SELECT, FZX_SCN_LOAD, FZX_SCN_RACE, FZX_SCN_RESULTS, FZX_SCN_RECORDS,
       FZX_SCN_COUNT };
enum { FZX_END_NONE, FZX_END_FINISH, FZX_END_RETIRE, FZX_END_QUIT };

/* Session selection (SCENES.md section 3), outside every scene arena. */
typedef struct {
    uint16_t version;
    uint8_t menu_mode, course;
    uint16_t machine_id;
    uint8_t skin, players, difficulty, cup, player_render, confirmed;
    uint32_t slider_bits;
} fzx_session_t;

/* Immutable finish snapshot (SCENES.md section 4). */
#define FZX_LAPS_MAX 8u
typedef struct {
    uint32_t run_id;
    uint8_t reason, menu_mode, course, skin;
    uint16_t machine_id;
    uint8_t laps_done, best_lap_index;
    uint32_t slider_bits, race_ms, lap_ms[FZX_LAPS_MAX], best_lap_ms, max_speed;
} fzx_run_result_t;

/* Field stamps of the race load steps (engine_main.c): [0] start, [1] path + physics,
 * [2] sky, skyline, cloud and floor planes, [3] track pack + stream, [4] sprite set,
 * [5] 3D player pack, [6] end. */
extern volatile uint32_t fzx_scn_load_fields[8];
/* Called from the VBlank-out interrupt (inside the multiply-register wrapper). */
extern void (*volatile fzx_scn_vblank_hook)(void);

void fzx_scn_main(void) __attribute__((noreturn));
/* The machine family LOAD loads the 3D player pack for. */
uint32_t fzx_scn_player_family(void);

/* engine_main.c (shared text with the plain image's main). */
uint32_t video_fields_get(void);
void fzx_race_load(void);
void fzx_race_begin(void);
bool fzx_race_frame(void);
#endif
