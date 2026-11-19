/* Engine scenes (ENG_SCENES=1): the boot-to-reset loop. See engine_scenes.h and
 * SCENES.md. The sequencing (fades, ranked requests, confirmed retirement, arenas,
 * press edges) is SH2ZAM scene-1; this file holds the game's scene table.
 *
 * TITLE, SELECT, RESULTS and RECORDS are PLACEHOLDER screens drawn as text in the
 * original font on NBG3 (the manager's layer) until Codex's C8 screens bind. LOAD
 * shows a text loading card; RACE is the E1 drive with the 3D player (engine_main.c).
 *
 * Video: the manager owns the VDP1 command list outside RACE (an empty list each
 * frame), NBG3 and colour offset A (every fade). RACE owns everything else while it
 * runs; its release turns RBG0 off and puts the back screen to black. */
#include <string.h>
#include <yaul.h>
#include "engine_scenes.h"
#include "engine_closeup.h"
#include "engine_drive.h"
#include "sh2zam/scene_experimental.h"
#include "sh2zam/vehicle_dynamics_experimental.h"

#ifndef FZX_GUEST_MASK
#define FZX_GUEST_MASK 0ull
#endif
#ifndef E1_DRIVER
#define E1_DRIVER 0
#endif

const sh2z_vd_body_t *fzx_drive_state(void);
void fzx_drive_reset(void);

enum { RANK_GOTO = 1u, RANK_END_RACE = 2u, RANK_RESET = 3u };
enum { FADE_FRAMES = 16u, RETIRE_MIN = 1u };
#define PAD_RIGHT 0x8000u
#define PAD_LEFT  0x4000u
#define PAD_DOWN  0x2000u
#define PAD_UP    0x1000u
#define PAD_START 0x0800u
#define PAD_A     0x0400u
#define PAD_C     0x0200u
#define PAD_B     0x0100u
#define PAD_CHORD (PAD_A | PAD_B | PAD_C | PAD_START)
#define SCN_MAGIC 0x53434E31u /* "SCN1" */

/* ---- capture interface (read and poked by the harness) ------------------- */
/* [0] magic, [1] scene, [2] phase, [3] fade, [4] epoch, [5] frames, [6] switches,
 * [7] retries, [8] cold loads, [9] soft resets, [10] last load fields, [11] last load
 * kind (1 cold, 2 retry), [12] run_id, [13] last end reason, [14] probe: select
 * result (bit 8 = done), [15] probe: load admits, [16] status (0 ok, else fault),
 * [17] menu arena high, [18] race arena high, [19] fields, [20] steps, [21] chord
 * fires, [22] own frames, [23] 3D pack status, [24] family loaded, [25] redirects,
 * [26] sound events delivered, [27] sound events rejected, [28] max speed km/h,
 * [29] loaded resident key, [30] edges consumed, [31] probe count. */
volatile uint32_t fzx_scn_report[32];
volatile uint32_t fzx_scn_pad;        /* bit 16 set: use bits 0..15 as port 1 (captures) */
volatile uint32_t fzx_scn_force_end;  /* captures: nonzero ends the race now with this reason */
volatile uint32_t fzx_scn_probe;      /* captures: 0x100 | machine id, tried in SELECT */
volatile uint32_t fzx_scn_load_fields[8];
/* Words the harness writes are read through the cache-through mirror (a debugger
 * write does not reach a cached line). */
#define POKED(w) (*(volatile uint32_t *)((uintptr_t)&(w) | CPU_CACHE_THROUGH))
void (*volatile fzx_scn_vblank_hook)(void);
fzx_session_t fzx_session;
fzx_run_result_t fzx_scn_result;

/* ---- arenas ---------------------------------------------------------------- */
static uint8_t menu_heap[8192] __attribute__((aligned(16)));
static uint8_t race_heap[1024] __attribute__((aligned(16)));
static sh2z_arena_t menu_arena, race_arena;
static sh2z_arena_t *const menu_arenas[1] = { &menu_arena };
static sh2z_arena_t *const race_arenas[1] = { &race_arena };

/* ---- session and identity ------------------------------------------------ */
typedef struct { uint16_t id; const char *name; } machine_t;
/* C-069 test entrants, then the guests (each behind its FZX_GUEST_MASK bit). */
static const machine_t machines[] = { { 0, "BLUE FALCON" }, { 1, "GOLDEN FOX" }, { 3, "FIRE STINGRAY" },
                                      { 33, "GUEST 33" }, { 34, "GUEST 34" } };
#define MACHINE_COUNT (sizeof(machines) / sizeof(machines[0]))

static bool machine_allowed(uint32_t id) {
    if (id >= 33u && id <= 34u) return ((FZX_GUEST_MASK >> id) & 1ull) != 0ull;
    for (unsigned i = 0; i < 3u; ++i) if (machines[i].id == id) return true;
    return false;
}

static void session_defaults(void) {
    memset(&fzx_session, 0, sizeof(fzx_session));
    fzx_session.version = 1; fzx_session.menu_mode = 1; /* Time Attack */
    fzx_session.machine_id = 0; fzx_session.skin = 1; fzx_session.player_render = 1; /* 3D */
    fzx_session.slider_bits = 0x3F000000u; /* 0.5 */
}

/* SELECT's only write path (SCENES.md section 3). */
static bool session_edit(uint32_t machine_id) {
    if (!machine_allowed(machine_id)) return false;
    fzx_session.machine_id = (uint16_t)machine_id;
    fzx_session.confirmed = 1;
    return true;
}

uint32_t fzx_scn_player_family(void) { return fzx_session.machine_id; }

/* ---- the manager --------------------------------------------------------- */
static sh2z_scene_seq_t seq;
static sh2z_scene_input_t input;
static uint32_t frame_fields, steps_this_frame, chord_latched, own_frames, run_counter;
static uint32_t resident_key;   /* 0: nothing resident; else family + 1 */
static bool retry_pending, text_dirty;
/* VDP2 registers at boot (dbgio's NBG3, black back screen): the baseline every scene's
 * release returns to. RACE's own set is kept at its release so a retry, which loads
 * nothing, gets the race's VDP2 state back unchanged (its VRAM and CRAM are untouched
 * by the placeholder screens). */
static vdp2_ioregs_t baseline_regs, race_regs;
static bool race_regs_kept, retry_entering;
static uint32_t snd_epoch_queue[8], snd_count;

static void fault(uint32_t code, const char *text) {
    fzx_scn_report[16] = code;
    dbgio_printf("\x1b[H\x1b[2J SCENES %s (%lu)\n", text, (unsigned long)code);
    dbgio_flush(); vdp2_sync(); vdp2_sync_wait();
    for (;;) {}
}

static void apply_fade(uint32_t level) {
    vdp2_ioregs_t *r = vdp2_regs_get();
    const uint16_t v = (uint16_t)((0u - level * 8u) & 0x1FFu); /* -8 per step: 32 = -256 (black) */
    r->clofen = level ? 0x007Fu : 0u; /* NBG0-3, RBG0, back screen, sprite */
    r->clofsl = 0u;
    r->coar = v; r->coag = v; r->coab = v;
}

static uint32_t read_pad(bool *connected) {
    const uint32_t o = POKED(fzx_scn_pad);
    if (o & 0x10000u) { *connected = true; return o & 0xFFFFu; }
    smpc_peripheral_digital_t d;
    memset(&d, 0, sizeof(d));
    smpc_peripheral_process();
    smpc_peripheral_digital_port(1, &d);
    *connected = d.connected;
    return d.connected ? d.pressed.raw : 0u;
}

/* Sound events (SCENES.md section 9): stamped with the poster's epoch; delivered once
 * at the frame boundary; posts after the poster's leave are rejected. No sound
 * driver is linked yet: delivery is counted. */
static bool leaving;
static void snd_event(uint32_t id) {
    if (leaving) { ++fzx_scn_report[27]; return; }
    if (snd_count < 8u) snd_epoch_queue[snd_count++] = (seq.epoch << 16) | (id & 0xFFFFu);
}
static void snd_deliver(void) { fzx_scn_report[26] += snd_count; snd_count = 0; }

/* The manager's own frames (menus, loading, transitions) send VDP1 nothing: a list
 * outside the race's frame flow stalls Yaul's VDP1 sync in this image (2026-11-19,
 * Ymir). The sprite layer is hidden instead (VDP2 sprite priority 0) whenever RACE is
 * not drawing, so the race's last VDP1 image is never shown behind a menu. */
static void own_frame(void) {
    vdp2_sync();
    vdp2_sync_wait();
}

static void text_frame(void) {
    dbgio_flush();
    own_frame();
}

static bool request(uint32_t target, uint32_t rank) {
    const bool ok = sh2z_scene_request(&seq, target, rank);
    if (ok) { sh2z_scene_input_consume(&input); ++fzx_scn_report[30]; }
    return ok;
}

/* ---- placeholder screens -------------------------------------------------- */
static uint32_t cursor;

static bool title_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    (void)sh2z_arena_alloc(&menu_arena, 512u, 16u);
    text_dirty = true;
    return true;
}
static void title_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (input.edges & PAD_START) { snd_event(1); (void)request(FZX_SCN_SELECT, RANK_GOTO); }
    else if (input.edges & PAD_B) (void)request(FZX_SCN_RECORDS, RANK_GOTO);
}
static void title_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    dbgio_printf("\x1b[H\x1b[2J\n\n\n     F-ZERO X  SATURN ENGINE\n\n     [PLACEHOLDER TITLE]\n\n\n"
                 "     PRESS START\n\n     B: RECORDS\n\n\n\n     ENGINE SCENES LOOP - TEST BUILD\n");
}

static bool select_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    (void)sh2z_arena_alloc(&menu_arena, 1024u, 16u);
    cursor = 0;
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) if (machines[i].id == fzx_session.machine_id) cursor = i;
    text_dirty = true;
    return true;
}
static void select_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    const uint32_t probe = POKED(fzx_scn_probe);
    if (probe & 0x100u) {
        /* Capture check: can identity `probe & 0xFF` be selected, and would LOAD admit it? */
        const uint32_t id = probe & 0xFFu;
        const fzx_session_t keep = fzx_session;
        const bool sel = session_edit(id);
        fzx_session = keep;
        fzx_scn_report[14] = 0x100u | (sel ? 1u : 0u) | (id << 16);
        fzx_scn_report[15] = (machine_allowed(id) && fzx_closeup_has_family(id)) ? 1u : 0u;
        POKED(fzx_scn_probe) = 0;
    }
    uint32_t n = 0, visible[MACHINE_COUNT];
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) if (machine_allowed(machines[i].id)) visible[n++] = i;
    unsigned at = 0;
    for (unsigned i = 0; i < n; ++i) if (visible[i] == cursor) at = i;
    if (input.repeat & PAD_DOWN) { at = (at + 1u) % n; text_dirty = true; }
    if (input.repeat & PAD_UP) { at = (at + n - 1u) % n; text_dirty = true; }
    cursor = visible[at];
    if (input.edges & PAD_A) {
        if (session_edit(machines[cursor].id)) { snd_event(2); retry_pending = false; (void)request(FZX_SCN_LOAD, RANK_GOTO); }
    } else if (input.edges & PAD_B) (void)request(FZX_SCN_TITLE, RANK_GOTO);
}
static void select_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    dbgio_printf("\x1b[H\x1b[2J\n\n   [PLACEHOLDER SELECT]\n\n   MODE   TIME ATTACK\n   COURSE MUTE CITY\n\n   MACHINE (3D PLAYER)\n\n");
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) {
        if (!machine_allowed(machines[i].id)) continue;
        dbgio_printf("   %s %-14s %s\n", i == cursor ? ">" : " ", machines[i].name,
                     fzx_closeup_has_family(machines[i].id) ? "" : "(NO 3D PACK)");
    }
    dbgio_printf("\n\n   A: RACE   B: BACK\n");
}

static bool records_enter(void *u, uint32_t s) { (void)u; (void)s; (void)sh2z_arena_alloc(&menu_arena, 256u, 16u); text_dirty = true; return true; }
static void records_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (input.edges & (PAD_B | PAD_START)) (void)request(FZX_SCN_TITLE, RANK_GOTO);
}
static void records_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    dbgio_printf("\x1b[H\x1b[2J\n\n   [PLACEHOLDER RECORDS]\n\n   (records service not linked yet)\n\n   B: BACK\n");
}

static bool results_enter(void *u, uint32_t s) { (void)u; (void)s; (void)sh2z_arena_alloc(&menu_arena, 768u, 16u); text_dirty = true; return true; }
static void results_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (input.edges & PAD_A) { snd_event(3); retry_pending = true; ++fzx_scn_report[7]; (void)request(FZX_SCN_LOAD, RANK_GOTO); }
    else if (input.edges & PAD_B) (void)request(FZX_SCN_TITLE, RANK_GOTO);
}
static void results_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    const fzx_run_result_t *r = &fzx_scn_result;
    static const char *const why[4] = { "-", "FINISH", "RETIRE", "QUIT" };
    dbgio_printf("\x1b[H\x1b[2J\n\n   [PLACEHOLDER RESULTS]\n\n   RUN %lu  %s\n\n", (unsigned long)r->run_id, why[r->reason & 3u]);
    dbgio_printf("   TIME  %lu'%02lu\"%02lu\n", (unsigned long)(r->race_ms / 60000u), (unsigned long)((r->race_ms / 1000u) % 60u),
                 (unsigned long)((r->race_ms / 10u) % 100u));
    for (unsigned i = 0; i < r->laps_done && i < 3u; ++i)
        dbgio_printf("   LAP %u %lu'%02lu\"%02lu\n", i + 1u, (unsigned long)(r->lap_ms[i] / 60000u),
                     (unsigned long)((r->lap_ms[i] / 1000u) % 60u), (unsigned long)((r->lap_ms[i] / 10u) % 100u));
    dbgio_printf("   MAX SPEED %lu KM/H\n\n   A: RETRY   B: TITLE\n", (unsigned long)r->max_speed);
}

/* ---- LOAD ------------------------------------------------------------------ */
#define NBG3_MAP 0x25E7E000u /* VDP2_MAP.md: NBG3 map, 64 x 64 one-word cells */
#define SPIN_ROW 13u
#define SPIN_COL 3u
static volatile uint16_t spin_words[4];
static volatile uint32_t spin_fields, spin_phase;
static bool load_done;

static void spin_hook(void) {
    if (++spin_fields % 6u) return;
    spin_phase = (spin_phase + 1u) & 3u;
    *(volatile uint16_t *)(NBG3_MAP + (SPIN_ROW * 64u + SPIN_COL) * 2u) = spin_words[spin_phase];
}

static bool load_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    load_done = false;
    /* The loading card; the spinner's four glyph words are read back from the map once
     * printed, so the interrupt animates them while the CPU sits in the CD loads. */
    const char *name = "?";
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) if (machines[i].id == fzx_session.machine_id) name = machines[i].name;
    dbgio_printf("\x1b[H\x1b[2J\n\n\n\n\n     MUTE CITY\n\n     TIME ATTACK\n\n     %s\n\n\n\n   |/-\\  LOADING\n", name);
    dbgio_flush();
    for (unsigned k = 0; k < 4u; ++k) spin_words[k] = *(volatile uint16_t *)(NBG3_MAP + (SPIN_ROW * 64u + 3u + k) * 2u);
    dbgio_printf("\x1b[14;5H   ");
    dbgio_flush();
    text_dirty = false;
    return true;
}

static void load_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (load_done || seq.phase != SH2Z_SCENE_RUN) return;
    const uint32_t family = fzx_session.machine_id;
    if (!fzx_session.confirmed || !machine_allowed(family) || !fzx_closeup_has_family(family)) {
        fzx_scn_report[16] = 60u; /* refused: back to select */
        load_done = true;
        (void)request(FZX_SCN_SELECT, RANK_GOTO);
        return;
    }
    const uint32_t f0 = video_fields_get();
    spin_fields = 0;
    fzx_scn_vblank_hook = spin_hook;
    if (retry_pending && resident_key == family + 1u) {
        /* Retry: everything is resident; only race-local state goes back to its start. */
        fzx_drive_reset();
        fzx_closeup_reset();
        retry_entering = race_regs_kept;
        memset((void *)fzx_scn_load_fields, 0, sizeof(fzx_scn_load_fields));
        fzx_scn_report[11] = 2;
    } else {
        fzx_race_load();
        race_regs_kept = false; retry_entering = false;
        resident_key = family + 1u;
        ++fzx_scn_report[8];
        fzx_scn_report[11] = 1;
    }
    fzx_scn_vblank_hook = NULL;
    fzx_scn_report[10] = video_fields_get() - f0;
    fzx_scn_report[23] = fzx_ab_report[0];
    fzx_scn_report[24] = family;
    fzx_scn_report[29] = resident_key;
    retry_pending = false;
    load_done = true;
    (void)request(FZX_SCN_RACE, RANK_GOTO);
}
static void load_draw(void) {}

/* ---- RACE ------------------------------------------------------------------ */
static uint32_t end_reason, end_wait, max_kmh;

static bool race_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    (void)sh2z_arena_alloc(&race_arena, 256u, 16u);
    if (retry_entering) { *vdp2_regs_get() = race_regs; retry_entering = false; }
    fzx_race_begin();
    end_reason = FZX_END_NONE; end_wait = 0; max_kmh = 0;
    ++run_counter;
    fzx_scn_report[12] = run_counter;
    dbgio_printf("\x1b[H\x1b[2J");
    return true;
}

static void race_snapshot(uint32_t reason) {
    const sh2z_vd_body_t *b = fzx_drive_state();
    fzx_run_result_t *r = &fzx_scn_result;
    memset(r, 0, sizeof(*r));
    r->run_id = run_counter; r->reason = (uint8_t)reason;
    r->menu_mode = fzx_session.menu_mode; r->course = fzx_session.course; r->skin = fzx_session.skin;
    r->machine_id = fzx_session.machine_id; r->slider_bits = fzx_session.slider_bits;
    r->laps_done = (uint8_t)(b->laps_done > 0 ? b->laps_done : 0);
    r->race_ms = b->race_time > 0 ? (uint32_t)b->race_time : 0u;
    r->best_lap_ms = 3599999u;
    for (unsigned i = 0; i < FZX_LAPS_MAX && i < (unsigned)r->laps_done; ++i) {
        r->lap_ms[i] = (uint32_t)b->lap_times[i];
        if (r->lap_ms[i] && r->lap_ms[i] < r->best_lap_ms) { r->best_lap_ms = r->lap_ms[i]; r->best_lap_index = (uint8_t)i; }
    }
    r->max_speed = max_kmh;
    fzx_scn_report[13] = reason;
}

static void race_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    const sh2z_vd_body_t *b = fzx_drive_state();
    const uint32_t kmh = (uint32_t)(((int64_t)(b->speed < 0 ? -b->speed : b->speed) * 216) / (65536 * 10));
    if (kmh > max_kmh) max_kmh = kmh;
    fzx_scn_report[28] = max_kmh;
    if (end_reason == FZX_END_NONE) {
        const uint32_t forced = POKED(fzx_scn_force_end);
        if (forced) { end_reason = forced & 3u; POKED(fzx_scn_force_end) = 0; end_wait = 30u; }
        else if (b->flags & SH2Z_VD_FINISHED) { end_reason = FZX_END_FINISH; end_wait = 120u; }
        else if (b->flags & (SH2Z_VD_SPINNING | SH2Z_VD_CRASHED | SH2Z_VD_FALLING)) { end_reason = FZX_END_RETIRE; end_wait = 120u; }
        if (end_reason != FZX_END_NONE) race_snapshot(end_reason);
    } else if (end_wait && --end_wait == 0u) {
        (void)request(FZX_SCN_RESULTS, RANK_END_RACE);
    }
}

static void race_release(void *u, uint32_t s) {
    (void)u; (void)s;
    /* RACE's VDP2 lease back to the baseline (registers; RBG0 off, the back screen black). */
    race_regs = *vdp2_regs_get();
    race_regs_kept = true;
    *vdp2_regs_get() = baseline_regs;
    dbgio_printf("\x1b[H\x1b[2J");
    dbgio_flush();
    vdp2_sync(); vdp2_sync_wait();
}

/* ---- the table ----------------------------------------------------------- */
static bool boot_enter(void *u, uint32_t s) { (void)u; (void)s; return true; }
static void boot_tick(void *u, uint32_t s) { (void)u; (void)s; (void)request(FZX_SCN_TITLE, RANK_GOTO); }
static void common_leave(void *u, uint32_t s) { (void)u; (void)s; leaving = true; }
static void common_release(void *u, uint32_t s) {
    (void)u; (void)s;
    dbgio_printf("\x1b[H\x1b[2J");
    dbgio_flush();
}
static void title_release(void *u, uint32_t s) {
    /* TITLE releases the resident race set (SCENES.md section 7). */
    common_release(u, s);
    resident_key = 0;
}

static const sh2z_scene_t table[FZX_SCN_COUNT] = {
    [FZX_SCN_BOOT]    = { NULL, boot_enter, boot_tick, common_leave, common_release, NULL, 0 },
    [FZX_SCN_TITLE]   = { NULL, title_enter, title_tick, common_leave, title_release, menu_arenas, 1 },
    [FZX_SCN_SELECT]  = { NULL, select_enter, select_tick, common_leave, common_release, menu_arenas, 1 },
    [FZX_SCN_LOAD]    = { NULL, load_enter, load_tick, common_leave, common_release, NULL, 0 },
    [FZX_SCN_RACE]    = { NULL, race_enter, race_tick, common_leave, race_release, race_arenas, 1 },
    [FZX_SCN_RESULTS] = { NULL, results_enter, results_tick, common_leave, common_release, menu_arenas, 1 },
    [FZX_SCN_RECORDS] = { NULL, records_enter, records_tick, common_leave, common_release, menu_arenas, 1 },
};

static void draw_current(void) {
    switch (seq.current) {
    case FZX_SCN_TITLE: title_draw(); break;
    case FZX_SCN_SELECT: select_draw(); break;
    case FZX_SCN_LOAD: load_draw(); break;
    case FZX_SCN_RESULTS: results_draw(); break;
    case FZX_SCN_RECORDS: records_draw(); break;
    default: break;
    }
}

void fzx_scn_main(void) {

    sh2z_arena_init(&menu_arena, menu_heap, sizeof(menu_heap));
    sh2z_arena_init(&race_arena, race_heap, sizeof(race_heap));
    sh2z_scene_input_init(&input, PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT, 20u, 6u);
    session_defaults();
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 0, 0, 0));
    vdp2_sync(); vdp2_sync_wait();
    baseline_regs = *vdp2_regs_get();
    fzx_scn_report[0] = SCN_MAGIC;
    if (!sh2z_scene_init(&seq, table, FZX_SCN_COUNT, FZX_SCN_BOOT, FADE_FRAMES, RETIRE_MIN, RANK_RESET, NULL))
        fault(1, "INIT");
    frame_fields = video_fields_get();
    for (;;) {
        const uint32_t now = video_fields_get();
        uint32_t steps = now - frame_fields;
        frame_fields = now;
        if (steps == 0u) steps = 1u;
        if (steps > 4u) steps = 4u;
        steps_this_frame = steps;
        fzx_scn_report[20] += steps;
        bool connected = false;
        const uint32_t pad = read_pad(&connected);
        fzx_scn_report[31] = pad | (connected ? 0x10000u : 0u);
        /* Soft reset: the chord fires once, then stays latched until all four are released. */
        if ((pad & PAD_CHORD) == PAD_CHORD) {
            if (!chord_latched) {
                chord_latched = 1;
                if (seq.current != FZX_SCN_BOOT && sh2z_scene_request(&seq, FZX_SCN_TITLE, RANK_RESET)) {
                    ++fzx_scn_report[9];
                    ++fzx_scn_report[21];
                    session_defaults();
                    resident_key = 0;
                    retry_pending = false;
                    sh2z_scene_input_consume(&input);
                }
            }
        } else if ((pad & PAD_CHORD) == 0u) chord_latched = 0;
        /* The race reads the pad through the drive's capture override (one reading a frame). */
        fzx_e1_pad_override = 0x10000u | (chord_latched ? 0u : pad);
        /* 60 Hz steps: the first step sees the edges, the rest only the held state. */
        (void)sh2z_scene_input_update(&input, chord_latched ? 0u : pad);
        const uint32_t before = seq.epoch;
        const uint32_t level = sh2z_scene_frame(&seq);
        if (seq.epoch != before) leaving = false;
        for (uint32_t k = 1; k < steps && seq.phase <= SH2Z_SCENE_RUN && seq.current != FZX_SCN_RACE; ++k) {
            (void)sh2z_scene_input_update(&input, chord_latched ? 0u : pad);
            if (seq.pending == SH2Z_SCENE_NONE) table[seq.current].tick(NULL, seq.current);
        }
        if (seq.phase == SH2Z_SCENE_FAULT) fault(2, "ENTER");
        apply_fade(level);
        /* The sprite layer shows only while RACE draws (user_init's priority 6). */
        const bool race_drawing = seq.current == FZX_SCN_RACE && sh2z_scene_drawing(&seq);
        vdp2_sprite_priority_set(0, race_drawing ? 6u : 0u);
        snd_deliver();
        if (sh2z_scene_drawing(&seq)) {
            own_frames = 0;
            if (seq.current == FZX_SCN_RACE) (void)fzx_race_frame();
            else { draw_current(); text_frame(); }
        } else {
            own_frame();
            ++own_frames;
            ++fzx_scn_report[22];
            /* Confirmed retirement: a field has been displayed with the sprite layer
             * hidden (own_frame waited for its VDP2 commit) and VDP1 has no list in
             * flight, so nothing VDP1 reads or shows still references the scene. */
            if (seq.phase == SH2Z_SCENE_RETIRE && own_frames >= 1u && !vdp1_sync_busy()) sh2z_scene_retired(&seq);
        }
        fzx_scn_report[1] = seq.current; fzx_scn_report[2] = seq.phase; fzx_scn_report[3] = level;
        fzx_scn_report[4] = seq.epoch; fzx_scn_report[5] = seq.frames; fzx_scn_report[6] = seq.switches;
        fzx_scn_report[17] = menu_arena.high; fzx_scn_report[18] = race_arena.high;
        fzx_scn_report[19] = video_fields_get(); fzx_scn_report[25] = seq.redirects;
        (void)connected;
    }
}
