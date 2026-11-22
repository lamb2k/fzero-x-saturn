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
#ifndef FZX_P3_BASELINE
       FZX_SCN_MODES, FZX_SCN_OPTIONS, FZX_SCN_ENDING,
#endif
       FZX_SCN_COUNT };
enum { FZX_END_NONE, FZX_END_FINISH, FZX_END_RETIRE, FZX_END_QUIT, FZX_END_PAUSE_RETRY };

/* Session selection (SCENES.md section 3), outside every scene arena. */
typedef struct {
    uint16_t version;
    uint8_t menu_mode, course;
    uint16_t machine_id;
    uint8_t skin, players, difficulty, cup, player_render, confirmed;
    uint32_t slider_bits;
} fzx_session_t;
#ifndef FZX_P3_BASELINE
extern fzx_session_t fzx_session;
#endif

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
/* Per stamp: [i][0] CD sectors read, [i][1] fields inside CD reads, [i][2] longest
 * interval between loading-screen turns so far (fields). */
extern volatile uint32_t fzx_scn_load_cd[8][3];
void fzx_scn_stamp(unsigned i);
/* Called from the VBlank-out interrupt (inside the multiply-register wrapper). */
extern void (*volatile fzx_scn_vblank_hook)(void);

#ifndef FZX_P3_BASELINE
uintptr_t fzx_title_cart_limit(void);
#endif
void fzx_scn_main(void) __attribute__((noreturn));

/* ---- Menu scene hooks (SCENES.md sections 2 and 8; C-070) ----------------------------
 * A drawing frame's VDP1 list is: system clip, local origin, a black full-screen erase,
 * then the scene's span of at most FZX_SCN_MENU_SPAN commands, then END. The span is
 * sized from the section 8 leases (the largest C8 screen's commands plus margin); the
 * retirement frames send an art-free list (prelude, erase, END). */
#define FZX_SCN_MENU_SPAN 256u
#include <yaul.h>
typedef struct {
    /* One bounded chunk of the scene's art upload, called once per presented frame from
     * the confirmed retirement of the previous scene until it returns true; `budget_frt`
     * is the chunk's time budget in FRT ticks (master clock / 32). VRAM written here is
     * the scene's own lease: the previous owner's lists have been replaced already. */
    bool (*preload_step)(uint32_t budget_frt);
    /* A preload abandoned (soft reset, or a redirect to another scene): drop its state. */
    void (*preload_cancel)(void);
    /* The scene's commands for this frame, written into `span` (at most `capacity`);
     * returns the count. A count above capacity is refused: the frame draws none. */
    unsigned (*draw)(vdp1_cmdt_t *span, unsigned capacity);
} fzx_scn_menu_hooks_t;
/* Installs the hooks of one menu scene (FZX_SCN_TITLE, _SELECT, _RESULTS, _RECORDS). */
void fzx_scn_menu_hooks_set(uint32_t scene, const fzx_scn_menu_hooks_t *hooks);
/* Appends `n` commands to the current frame's span (inside draw only): all or none;
 * false (and nothing appended) past FZX_SCN_MENU_SPAN. */
bool fzx_scn_span_append(const vdp1_cmdt_t *cmds, unsigned n);
/* The menu texture lease: true when no displayed list references the previous owner
 * (a preload may write VRAM); false while it is still referenced (the preload defers). */
bool fzx_scn_lease_free(void);
extern volatile uint32_t fzx_scn_ctl[8];
/* The machine family LOAD loads the 3D player pack for. */
uint32_t fzx_scn_player_family(void);

/* engine_main.c (shared text with the plain image's main). */
uint32_t video_fields_get(void);
void fzx_race_render_callback(bool on);
vdp1_cmdt_t *fzx_race_list(unsigned *count, unsigned *capacity);
void fzx_race_load(void);
void fzx_race_begin(void);
bool fzx_race_frame(void);
#endif
