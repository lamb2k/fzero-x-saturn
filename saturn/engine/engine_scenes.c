/* Engine scenes (ENG_SCENES=1): the boot-to-reset loop. See engine_scenes.h and
 * SCENES.md. The sequencing (fades, ranked requests, confirmed retirement, arenas,
 * press edges) is SH2ZAM scene-1; this file holds the game's scene table.
 *
 * TITLE, SELECT, RESULTS and RECORDS are PLACEHOLDER screens drawn as text in the
 * original font on NBG3 (the manager's layer) until Codex's C8 screens bind. LOAD
 * shows a text loading card; RACE is the E1 drive with the 3D player (engine_main.c).
 *
 * Video: the manager owns the VDP1 command list outside RACE (prelude, a black
 * full-screen polygon, END each frame), NBG3 and colour offset A (every fade). RACE owns
 * everything else while it runs; its release returns VDP2 to the boot baseline. */
#include <string.h>
#include <yaul.h>
#include "road_cd.h"
#include "texture_cd.h"
#include "engine_scenes.h"
#include "engine_closeup.h"
#ifndef FZX_P3_BASELINE
#include "title_identity.h"
#include "engine_load_cards.h"
#include "select_art.h"
#include "machine_tuning.h"
#endif
#include "engine_records.h"
#include "engine_cup.h"
#include "engine_field.h"
#ifndef ENG_COURSE_SELECT
#define ENG_COURSE_SELECT 0
#endif
#ifndef ENG_COURSE_INDEX
#define ENG_COURSE_INDEX 0
#endif
#if ENG_COURSE_SELECT
#include "engine_courses.h"
#endif
#ifndef ENG_ROSTER
#define ENG_ROSTER 0
#endif
#if ENG_ROSTER
#include "engine_roster.h"
#include "rosterz_identity.h"
#include "roster_identity.h"
#ifndef FZX_STOCK_RIVAL_SKIN
#define FZX_STOCK_RIVAL_SKIN '1' /* earlier generated roster headers */
#endif
/* The roster LOAD phase's field: the stock families of a full grid (time attack has no
 * rivals; this measures the E2 field's load). [0] fields, [1] raw FNV of the inflated
 * check set (family 0 near), [2] its expected FNV, [3] check result (1 equal). */
volatile uint32_t fzx_scn_roster[8];
static uint8_t roster_check[60u * 1024u] __attribute__((aligned(4)));
static void nibble_mirror(uint8_t *row, uint32_t bytes) {
    for (uint32_t i = 0, j = bytes - 1u; i < j; ++i, --j) {
        const uint8_t a = row[i], b = row[j];
        row[i] = (uint8_t)((b << 4) | (b >> 4)); row[j] = (uint8_t)((a << 4) | (a >> 4));
    }
    if (bytes & 1u) row[bytes / 2u] = (uint8_t)((row[bytes / 2u] << 4) | (row[bytes / 2u] >> 4));
}
/* Inflates every cell of a loaded set back into the RSP1 1.1 layout and returns its FNV. */
static uint32_t roster_inflate_fnv(uint32_t family, unsigned prof) {
    const sh2z_rspr_set_t *set = fzx_roster_set(family, prof);
    if (!set || set->bytes > sizeof(roster_check)) return 0;
    const uint8_t *src = (const uint8_t *)set;
    uint32_t header = 0;
    {   /* The header region (header, cell table, CLUTs) as stored; the rest zero, then the cells. */
        static const char names[2][4] = { { 'n', 'e', 'a', 'r' }, { 'f', 'a', 'r', 0 } };
        const char key[8] = { 'c', (char)('0' + family / 10u), (char)('0' + family % 10u), 's', FZX_STOCK_RIVAL_SKIN, 0, 0, 0 };
        for (unsigned i = 0; i < FZX_RZ_SETS; ++i)
            if (!memcmp(fzx_rz_sets[i].key, key, 8) && !memcmp(fzx_rz_sets[i].prof, names[prof & 1u], 4)) header = fzx_rz_sets[i].header_bytes;
    }
    for (uint32_t i = 0; i < set->bytes; ++i) roster_check[i] = i < header ? src[i] : 0u;
    const uint32_t cells = (uint32_t)set->yaw_count * set->pitch_count;
    const sh2z_rspr_cell_t *ct = sh2z_rspr_cells(set);
    for (uint32_t c = 0; c < cells; ++c) {
        uint32_t from; bool flip;
        if (!fzx_roster_cell_source(family, prof, c, &from, &flip)) return 0;
        uint8_t *dst = roster_check + ct[c].pixel_offset;
        if (!fzx_roster_cell_decode(family, prof, from, (volatile uint16_t *)(void *)dst)) return 0;
        if (flip) for (uint32_t y = 0; y < ct[c].height; ++y) nibble_mirror(dst + y * (ct[c].width / 2u), ct[c].width / 2u);
    }
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < set->bytes; ++i) h = (h ^ roster_check[i]) * 16777619u;
    return h;
}
#endif
#include "engine_drive.h"
#include "sh2zam/scene_experimental.h"
#include "sh2zam/vehicle_dynamics_experimental.h"
#if ENG_SND
#include "engine_snd.h"
/* Music fades (frames): before LOAD's first disc read, and out of a race into RESULTS. */
#define SND_LOAD_FADE 30u
#define SND_RACE_FADE 60u
#endif

#ifndef FZX_GUEST_MASK
#define FZX_GUEST_MASK 0ull
#endif
#ifndef FZX_P3_BASELINE
#if (FZX_GUEST_MASK & ~SELECT_GUEST_MASK)
#error "Rebake SELECT.PAK with the enabled guest portraits"
#endif
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
#ifndef FZX_P3_BASELINE
#define PAD_X 0x0040u
#define PAD_Z 0x0010u
#define PAD_L 0x0008u
#define PAD_R 0x0080u
#endif
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
#if ENG_SCN_STALL_CONTROL
volatile uint32_t fzx_scn_stall_ctl; /* engine gate control: see the main loop */
#endif
volatile uint32_t fzx_scn_probe;      /* captures: 0x100 | machine id, tried in SELECT */
/* SCENES.md 11.3 (captures): nonzero classifies a Time Attack race at the GP trigger, as GP
 * does. [0] the tick that set the finish, [1] the finish timer, [2] the trigger frame,
 * [3] result hash, [4] attempt hash, [5] player place, [6] rows, [7] projected rows. */
volatile uint32_t fzx_scn_gp_probe;
volatile uint32_t fzx_scn_cls[8];
static fzx_classification_t scn_cls;
const fzx_classification_t *fzx_cup_classification(void) { return &scn_cls; }
static uint8_t load_course, course_cursor, select_mode, select_difficulty;
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
#ifndef FZX_P3_BASELINE
static bool machine_allowed(uint32_t id) {
    if (id == 33u || id == 34u) return ((FZX_GUEST_MASK >> id) & 1ull) != 0ull;
    return id < 30u;
#else
typedef struct { uint16_t id; const char *name; } machine_t;
/* C-069 test entrants, then the guests (each behind its FZX_GUEST_MASK bit). */
static const machine_t machines[] = { { 0, "BLUE FALCON" }, { 1, "GOLDEN FOX" }, { 3, "FIRE STINGRAY" },
                                      { 33, "GUEST 33" }, { 34, "GUEST 34" } };
#define MACHINE_COUNT (sizeof(machines) / sizeof(machines[0]))

static bool machine_allowed(uint32_t id) {
    if (id >= 33u && id <= 34u) return ((FZX_GUEST_MASK >> id) & 1ull) != 0ull;
    for (unsigned i = 0; i < 3u; ++i) if (machines[i].id == id) return true;
    return false;
#endif
}
#ifndef FZX_P3_BASELINE
static const char *machine_name(unsigned id) {
    static const char *const names[] = {
        "BLUE FALCON","GOLDEN FOX","WILD GOOSE","FIRE STINGRAY","WHITE CAT","RED GAZELLE","GREAT STAR",
        "IRON TIGER","DEEP CLAW","TWIN NORITTA","SUPER PIRANHA","MIGHTY HURRICANE","LITTLE WYVERN",
        "SPACE ANGLER","GREEN PANTHER","BLACK BULL","WILD BOAR","ASTRO ROBIN","KING METEOR","QUEEN METEOR",
        "WONDER WASP","HYPER SPEEDER","DEATH ANCHOR","CRAZY BEAR","NIGHT THUNDER","BIG FANG",
        "MIGHTY TYPHOON","MAD WOLF","SONIC PHANTOM","BLOOD HAWK","SUPER FALCON","SUPER STINGRAY",
        "SUPER CAT","DR. ROBOTNIK","METAL SONIC"
    };
    return id < sizeof(names)/sizeof(names[0]) && (id < 33 || machine_allowed(id)) ? names[id] : "MACHINE";
}
static float select_balance;
#endif

static void session_defaults(void) {
    memset(&fzx_session, 0, sizeof(fzx_session));
    fzx_session.version = 1; fzx_session.menu_mode = 1; /* Time Attack */
    fzx_session.machine_id = 0; fzx_session.skin = 1; fzx_session.player_render = 1; /* 3D */
    fzx_session.slider_bits = 0x3F000000u; /* 0.5 */
#if ENG_COURSE_INDEX
    fzx_session.course = ENG_COURSE_INDEX; /* the course this image carries (ENG_COURSE) */
#endif
}

/* The catalog bit a session's mode needs (SCENES.md 11.1): GP 0, otherwise Time Attack. */
static uint8_t mode_avail(unsigned mode) { return mode == 0u ? FZX_AVAIL_GP : FZX_AVAIL_TA; }
static bool course_ok(uint32_t course, unsigned mode) {
    const fzx_course_row_t *row = course < 256u ? fzx_course_find((uint8_t)course) : NULL;
    return row && (row->avail & mode_avail(mode)) != 0;
}

/* SELECT's only write path (SCENES.md section 3). */
static bool session_edit(uint32_t machine_id, uint32_t course) {
    if (!machine_allowed(machine_id) || !course_ok(course, select_mode)) return false;
    fzx_session.menu_mode = select_mode;
    fzx_session.difficulty = select_difficulty;
    fzx_session.cup = FZX_CUP_JACK;
    fzx_session.machine_id = (uint16_t)machine_id;
    fzx_session.course = (uint8_t)course;
#ifndef FZX_P3_BASELINE
    fzx_session.skin = machine_id >= 33u ? 0u : 1u;
    memcpy(&fzx_session.slider_bits, &select_balance, sizeof(select_balance));
#endif
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

/* RESULTS over the retained race backdrop: the backdrop layers (RBG0, back screen,
 * sprite) take colour offset B, dimmed by BACKDROP_DIM steps on top of the fade. */
#define BACKDROP_DIM 12u
static bool backdrop;
static unsigned backdrop_count; /* the race's last list length (END included) */
static void apply_fade(uint32_t level) {
    vdp2_ioregs_t *r = vdp2_regs_get();
    const uint16_t v = (uint16_t)((0u - level * 8u) & 0x1FFu); /* -8 per step: 32 = -256 (black) */
    if (backdrop && seq.current == FZX_SCN_RESULTS) {
        const uint32_t d = level > BACKDROP_DIM ? level : BACKDROP_DIM;
        const uint16_t b = (uint16_t)((0u - d * 8u) & 0x1FFu);
        r->clofen = 0x007Fu;
        r->clofsl = 0x0070u; /* RBG0, back screen, sprite: offset B */
        r->coar = v; r->coag = v; r->coab = v;
        r->cobr = b; r->cobg = b; r->cobb = b;
        return;
    }
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

/* The manager's VDP1 lists for frames RACE doesn't draw (SCENES.md section 8):
 * - menu_list: system clip, local origin, black erase, the scene's span (up to
 *   FZX_SCN_MENU_SPAN commands), END;
 * - bare_list: system clip, local origin, black erase, END (no art): the retirement
 *   frames, so a list that replaces a scene never references any scene's VRAM. */
enum { SPAN_FIRST = 3u };
static vdp1_cmdt_t menu_cmds[SPAN_FIRST + FZX_SCN_MENU_SPAN + 1u];
static vdp1_cmdt_t bare_cmds[SPAN_FIRST + 1u];
static vdp1_cmdt_list_t menu_list, bare_list;
/* Menu hook counters: [0] span refusals, [1] preload deferrals (lease still referenced),
 * [2] preload steps, [3] preload cancels, [4] preloads completed, [5] longest preload (frames). */
volatile uint32_t fzx_scn_ctl[8];
static unsigned span_count;
static bool span_open;
static fzx_scn_menu_hooks_t hooks[FZX_SCN_COUNT];
/* The lease: the scene whose commands the last displayed menu list may reference
 * (FZX_SCN_COUNT = none). Set by a drawing frame with a span; cleared by a bare frame
 * that has been drawn and shown (the retirement confirmation). */
static uint32_t lease_owner = FZX_SCN_COUNT;
static uint32_t preloading = FZX_SCN_COUNT; /* the scene whose preload is in progress */

void fzx_scn_menu_hooks_set(uint32_t scene, const fzx_scn_menu_hooks_t *h) {
    if (scene < FZX_SCN_COUNT && h) hooks[scene] = *h;
}
bool fzx_scn_span_append(const vdp1_cmdt_t *cmds, unsigned n) {
    if (!span_open || !cmds || n > FZX_SCN_MENU_SPAN - span_count) { ++fzx_scn_ctl[0]; return false; }
    memcpy(&menu_cmds[SPAN_FIRST + span_count], cmds, n * sizeof(vdp1_cmdt_t));
    span_count += n;
    return true;
}
bool fzx_scn_lease_free(void) { return lease_owner == FZX_SCN_COUNT; }
static void menu_list_init(void) {
    const int16_vec2_t clip = INT16_VEC2_INITIALIZER(319, 223);
    const int16_vec2_t origin = INT16_VEC2_INITIALIZER(0, 0);
    memset(menu_cmds, 0, sizeof(menu_cmds));
    vdp1_cmdt_system_clip_coord_set(&menu_cmds[0]);
    vdp1_cmdt_vtx_system_clip_coord_set(&menu_cmds[0], clip);
    vdp1_cmdt_local_coord_set(&menu_cmds[1]);
    vdp1_cmdt_vtx_local_coord_set(&menu_cmds[1], origin);
    {   /* Full-screen black polygon: the frame's erase. */
        const vdp1_cmdt_draw_mode_t mode = {.pre_clipping_disable = 1};
        vdp1_cmdt_polygon_set(&menu_cmds[2]);
        vdp1_cmdt_draw_mode_set(&menu_cmds[2], mode);
        vdp1_cmdt_color_set(&menu_cmds[2], RGB1555(1, 0, 0, 0));
        menu_cmds[2].cmd_xa = 0; menu_cmds[2].cmd_ya = 0; menu_cmds[2].cmd_xb = 319; menu_cmds[2].cmd_yb = 0;
        menu_cmds[2].cmd_xc = 319; menu_cmds[2].cmd_yc = 223; menu_cmds[2].cmd_xd = 0; menu_cmds[2].cmd_yd = 223;
    }
    vdp1_cmdt_end_set(&menu_cmds[3]);
    menu_list.cmdts = menu_cmds; menu_list.count = 4;
    memcpy(bare_cmds, menu_cmds, sizeof(bare_cmds));
    bare_list.cmdts = bare_cmds; bare_list.count = SPAN_FIRST + 1u;
}

/* The drawing frame's span: the scene's draw hook, bounded; END after it. */
static void span_build(uint32_t scene) {
    span_count = 0;
    span_open = true;
    const fzx_scn_menu_hooks_t *h = scene < FZX_SCN_COUNT ? &hooks[scene] : NULL;
    if (h && h->draw) {
        const unsigned n = h->draw(&menu_cmds[SPAN_FIRST + span_count], FZX_SCN_MENU_SPAN - span_count);
        if (n > FZX_SCN_MENU_SPAN - span_count) ++fzx_scn_ctl[0]; /* refused: the frame draws none */
        else span_count += n;
    }
    if (POKED(fzx_scn_probe) & 0x400u) { /* control: an append past the bound is refused */
        (void)fzx_scn_span_append(menu_cmds, FZX_SCN_MENU_SPAN + 1u);
        POKED(fzx_scn_probe) = 0;
    }
    span_open = false;
    vdp1_cmdt_end_set(&menu_cmds[SPAN_FIRST + span_count]);
    menu_list.count = (uint16_t)(SPAN_FIRST + span_count + 1u);
    if (span_count) lease_owner = scene;
}
/* A frame of the manager's (menus, loading, transitions): its VDP1 list (menu scenes
 * add their commands between the prelude and END) and the VDP2 commit.
 * Each list waits for the previous VDP1 frame to complete (vdp1_sync_wait), as the
 * race's serial frames do: a put while Yaul's previous sync is still pending stalls in
 * vdp1_sync_cmdt_put (2026-11-19, Ymir; the cause of the earlier menu-list hang). */
static void list_frame(const vdp1_cmdt_list_t *list) {
    vdp2_sync_wait();
    vdp1_sync_put_wait();
    vdp1_sync_wait();
    vdp1_sync_cmdt_list_put(list, 0);
    vdp1_sync_render();
    vdp1_sync();
    vdp2_sync();
}

/* A frame without scene art (loading card, transitions, retirement). */
static void own_frame(void) { list_frame(&bare_list); }

/* A menu scene's drawing frame: its span, then the list. NBG3 text goes to VRAM only on
 * the frames that printed. Over a retained race backdrop the list is the race's last one
 * (without its END), then the scene's span, then END. */
static vdp1_cmdt_list_t backdrop_list;
static void text_frame(bool printed) {
    if (printed) dbgio_flush();
    span_build(seq.current);
    if (backdrop && seq.current == FZX_SCN_RESULTS) {
        unsigned count, capacity;
        vdp1_cmdt_t *race = fzx_race_list(&count, &capacity);
        const unsigned first = backdrop_count - 1u;
        if (first + span_count + 1u <= capacity) {
            memcpy(&race[first], &menu_cmds[SPAN_FIRST], span_count * sizeof(vdp1_cmdt_t));
            vdp1_cmdt_end_set(&race[first + span_count]);
            backdrop_list.cmdts = race;
            backdrop_list.count = (uint16_t)(first + span_count + 1u);
            list_frame(&backdrop_list);
            return;
        }
        ++fzx_scn_ctl[0]; /* the span does not fit after the backdrop: refused */
    }
    list_frame(&menu_list);
}

static bool request(uint32_t target, uint32_t rank) {
    const bool ok = sh2z_scene_request(&seq, target, rank);
    if (ok) { sh2z_scene_input_consume(&input); ++fzx_scn_report[30]; }
    return ok;
}

/* ---- placeholder screens -------------------------------------------------- */
static uint32_t cursor;

#ifndef FZX_P3_BASELINE
/* Retain the checked title atlas at the top of the 4 MiB cartridge. TITLE borrows
 * the VDP1 texture partition only after the preceding scene has retired its list. */
extern unsigned char sat_stub_data_end[];
extern volatile uint32_t fzx_e1_driver;
static uintptr_t title_cart, title_vram;
static uint32_t title_at, title_ticks, demo_index, saved_driver;
static bool title_connected, demo;
static fzx_session_t demo_return_session;

/* The two menu packs share a cartridge reservation and use the VDP1
 * texture partition in disjoint scene leases. SELECT never reads the CD. */
uintptr_t fzx_title_cart_limit(void) { return title_cart ? title_cart - SELECT_ART_BYTES : 0; }

static bool title_cache(void) {
    dram_cart_init();
    if (dram_cart_id_get() != DRAM_CART_ID_4MIB) return false;
    title_cart = (uintptr_t)dram_cart_area_get() + dram_cart_size_get() - FZX_TITLE_BYTES;
    uintptr_t scratch = ((uintptr_t)sat_stub_data_end + 31u) & ~(uintptr_t)31u;
    return fzx_texture_cd_load("TITLE.PAK", FZX_TITLE_BYTES, FZX_TITLE_HASH,
                               (void *)title_cart, (void *)scratch, 4096u)
        && fzx_texture_cd_load("SELECT.PAK", SELECT_ART_BYTES, SELECT_ART_HASH,
                               (void *)fzx_title_cart_limit(), (void *)scratch, 4096u);
}
static void title_cancel(void) { title_at = 0; }

static bool art_preload(uint32_t budget, uintptr_t source, unsigned total) {
    vdp1_vram_partitions_t part;
    vdp1_vram_partitions_get(&part);
    if (total > part.texture_size) fault(61, "MENU VRAM");
    title_vram = (uintptr_t)part.texture_base;
    const uint16_t start = cpu_frt_count_get();
    do {
        unsigned bytes = total - title_at;
        if (bytes > 512u) bytes = 512u;
        volatile uint16_t *dst = (volatile uint16_t *)(title_vram + title_at);
        const uint16_t *src = (const uint16_t *)(source + title_at);
        for (unsigned i = 0; i < bytes/2u; ++i) dst[i] = src[i];
        title_at += bytes;
        if (title_at == total) { title_at = 0; return true; }
    } while ((uint16_t)(cpu_frt_count_get() - start) < budget/2u);
    return false;
}

static bool title_preload(uint32_t budget) { return art_preload(budget, title_cart, FZX_TITLE_BYTES); }
static bool select_preload(uint32_t budget) { return art_preload(budget, fzx_title_cart_limit(), SELECT_ART_BYTES); }

static unsigned select_span(vdp1_cmdt_t *span, unsigned capacity) {
    if (!capacity || cursor >= 35u) return 0;
    const unsigned w = select_art[cursor].width, h = select_art[cursor].height;
    if (!w || !h) return 0;
    const bool portrait = cursor >= 33u;
    const uintptr_t palette = title_vram + select_art[cursor].offset;
    const vdp1_cmdt_draw_mode_t mode = { .end_code_disable = 1,
        .color_mode = portrait ? VDP1_CMDT_CM_RGB_32768 : VDP1_CMDT_CM_CLUT_16 };
    memset(span, 0, sizeof(*span));
    vdp1_cmdt_distorted_sprite_set(span);
    vdp1_cmdt_draw_mode_set(span, mode);
    vdp1_cmdt_char_size_set(span, w, h);
    vdp1_cmdt_char_base_set(span, (vdp1_vram_t)(palette + (portrait ? 0u : 32u)));
    if (!portrait) span->cmd_colr = (uint16_t)((palette & 0x7ffffu) >> 3);
    const int width = w * 3 / 2, height = h * 3 / 2;
    span->cmd_xa = span->cmd_xd = 255 - width / 2;
    span->cmd_xb = span->cmd_xc = span->cmd_xa + width - 1;
    span->cmd_ya = span->cmd_yb = 50 - height / 2;
    span->cmd_yc = span->cmd_yd = span->cmd_ya + height - 1;
    return 1;
}

static const fzx_scn_menu_hooks_t select_hooks = {select_preload, title_cancel, select_span};

static unsigned title_span(vdp1_cmdt_t *span, unsigned capacity) {
    if (capacity < FZX_TITLE_IMAGE_COUNT) return 0;
    const vdp1_cmdt_draw_mode_t mode = { .end_code_disable=1, .color_mode=VDP1_CMDT_CM_RGB_32768 };
    unsigned n = 0;
    for (unsigned i = 0; i < FZX_TITLE_IMAGE_COUNT; ++i) {
        if (i == 2u && (!title_connected || title_ticks < 95u)) continue;
        if (i == 4u && title_connected) continue;
        vdp1_cmdt_t *cmd = &span[n++];
        memset(cmd, 0, sizeof(*cmd));
        vdp1_cmdt_normal_sprite_set(cmd);
        vdp1_cmdt_draw_mode_set(cmd, mode);
        vdp1_cmdt_char_base_set(cmd, (vdp1_vram_t)(title_vram + fzx_title_images[i].offset));
        vdp1_cmdt_char_size_set(cmd, fzx_title_images[i].width, fzx_title_images[i].height);
        cmd->cmd_xa = fzx_title_images[i].left;
        cmd->cmd_ya = fzx_title_images[i].top - 8;
    }
    return n;
}
static const fzx_scn_menu_hooks_t title_hooks = {title_preload, title_cancel, title_span};

static void demo_stop(void) {
    if (!demo) return;
    fzx_e1_driver = saved_driver;
    fzx_session = demo_return_session;
    demo = false;
}
static bool title_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    demo_stop();
    title_ticks = 0;
#else
static bool title_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    (void)sh2z_arena_alloc(&menu_arena, 512u, 16u);
#endif
    text_dirty = true;
    return true;
}
static void title_tick(void *u, uint32_t s) {
    (void)u; (void)s;
#ifndef FZX_P3_BASELINE
    if (!title_connected) return;
    ++title_ticks;
    if (title_ticks >= 80u && (input.edges & (PAD_A | PAD_C | PAD_START))) {
        snd_event(1); (void)request(FZX_SCN_SELECT, RANK_GOTO);
    } else if (input.edges & PAD_B) (void)request(FZX_SCN_RECORDS, RANK_GOTO);
    else if (title_ticks >= 600u) {
        /* Retail cues its demo from the soundtrack and rotates GP/VS cameras.
         * The engine uses a timed one-player demonstration on available courses. */
        static const uint8_t order[] = {3u, 1u, 2u, 4u, 0u, 5u};
        for (unsigned i = 0; i < sizeof(order); ++i) {
            const unsigned course = order[demo_index++ % sizeof(order)];
            const fzx_course_row_t *row = fzx_course_find(course);
            if (!row || !(row->avail & FZX_AVAIL_TA)) continue;
            if (!request(FZX_SCN_LOAD, RANK_GOTO)) break;
            demo_return_session = fzx_session;
            saved_driver = fzx_e1_driver;
            session_defaults();
            fzx_session.course = (uint8_t)course;
            fzx_session.confirmed = 1;
            fzx_e1_driver = 1;
            retry_pending = false;
            demo = true;
            break;
        }
    }
#else
    if (input.edges & PAD_START) { snd_event(1); (void)request(FZX_SCN_SELECT, RANK_GOTO); }
    else if (input.edges & PAD_B) (void)request(FZX_SCN_RECORDS, RANK_GOTO);
#endif
}
static void title_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
#ifndef FZX_P3_BASELINE
    dbgio_printf("\x1b[H\x1b[2J");
#else
    dbgio_printf("\x1b[H\x1b[2J\n\n\n     F-ZERO X  SATURN ENGINE\n\n     [PLACEHOLDER TITLE]\n\n\n"
                 "     PRESS START\n\n     B: RECORDS\n\n\n\n     ENGINE SCENES LOOP - TEST BUILD\n");
#endif
}

static const char *course_name(uint8_t course) {
    static const char *const names[] = {
        "MUTE CITY", "SILENCE", "SAND OCEAN", "DEVIL'S FOREST", "BIG BLUE", "PORT TOWN"
    };
    const fzx_course_row_t *row = fzx_course_find(course);
    return row && row->display_key < sizeof(names) / sizeof(names[0]) ? names[row->display_key] : "UNKNOWN";
}

static bool jack_ready(void) {
    return E1_FIELD == 29 && fzx_cup_gp_ready(FZX_CUP_JACK);
}

static bool select_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    (void)sh2z_arena_alloc(&menu_arena, 1024u, 16u);
#ifndef FZX_P3_BASELINE
    cursor = machine_allowed(fzx_session.machine_id) ? fzx_session.machine_id : 0;
    memcpy(&select_balance, &fzx_session.slider_bits, sizeof(select_balance));
#else
    cursor = 0;
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) if (machines[i].id == fzx_session.machine_id) cursor = i;
#endif
    select_mode = jack_ready() ? fzx_session.menu_mode : 1u;
    select_difficulty = fzx_session.difficulty;
    course_cursor = course_ok(fzx_session.course, select_mode) ? fzx_session.course : fzx_course_step(fzx_session.course, 1, mode_avail(select_mode));
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
        const bool sel = session_edit(id, course_cursor);
        fzx_session = keep;
        fzx_scn_report[14] = 0x100u | (sel ? 1u : 0u) | (id << 16);
        fzx_scn_report[15] = (machine_allowed(id) && fzx_closeup_has_family(id)) ? 1u : 0u;
        POKED(fzx_scn_probe) = 0;
    } else if (probe & 0x8000u) {
        /* Capture check: can course `probe & 0xFF` be confirmed in this mode? */
        const uint32_t id = probe & 0xFFu;
        const fzx_session_t keep = fzx_session;
        const bool sel = session_edit(fzx_session.machine_id, id);
        fzx_session = keep;
        fzx_scn_report[14] = 0x8000u | (sel ? 1u : 0u) | (id << 16);
        POKED(fzx_scn_probe) = 0;
    }
    if ((input.edges & PAD_C) && jack_ready()) {
        select_mode ^= 1u;
        course_cursor = select_mode ? fzx_course_step(FZX_COURSE_NONE, 1, FZX_AVAIL_TA)
                                              : fzx_cup_def(FZX_CUP_JACK)->course[0];
        text_dirty = true;
    }
    if (!select_mode && (input.repeat & (PAD_LEFT | PAD_RIGHT))) {
        select_difficulty = (select_difficulty + ((input.repeat & PAD_LEFT) ? 3u : 1u)) % 4u;
        text_dirty = true;
    }
    if (select_mode && (input.repeat & (PAD_LEFT | PAD_RIGHT))) {
        const uint8_t c = fzx_course_step(course_cursor, (input.repeat & PAD_LEFT) ? -1 : 1, mode_avail(select_mode));
        if (c != FZX_COURSE_NONE && c != course_cursor) { course_cursor = c; text_dirty = true; }
    }
    if (probe & 0x200u) {
        /* Script hook (claude/courses): course `probe & 0xFF` under the cursor, if the mode lists it. */
        const uint8_t c = (uint8_t)(probe & 0xFFu);
        if (course_ok(c, select_mode)) course_cursor = c;
        text_dirty = true;
        POKED(fzx_scn_probe) = 0;
    }
#ifndef FZX_P3_BASELINE
    if (input.repeat & (PAD_UP | PAD_DOWN)) {
        do {
            cursor = (cursor + ((input.repeat & PAD_DOWN) ? 1u : 34u)) % 35u;
        } while (!machine_allowed(cursor));
        text_dirty = true;
    }
    float previous = select_balance;
    if (input.edges & PAD_X) select_balance = 0;
    else if (input.edges & PAD_Z) select_balance = 1;
    else if (input.held & PAD_L) select_balance -= 1.f/128.f;
    else if (input.held & PAD_R) select_balance += 1.f/128.f;
    if (select_balance < 0) select_balance = 0;
    if (select_balance > 1) select_balance = 1;
    if (previous != select_balance) text_dirty = true;
    if (input.edges & PAD_A) {
        if (session_edit(cursor, course_cursor)) { snd_event(2); retry_pending = false; (void)request(FZX_SCN_LOAD, RANK_GOTO); }
#else
    uint32_t n = 0, visible[MACHINE_COUNT];
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) if (machine_allowed(machines[i].id)) visible[n++] = i;
    unsigned at = 0;
    for (unsigned i = 0; i < n; ++i) if (visible[i] == cursor) at = i;
    if (input.repeat & PAD_DOWN) { at = (at + 1u) % n; text_dirty = true; }
    if (input.repeat & PAD_UP) { at = (at + n - 1u) % n; text_dirty = true; }
    cursor = visible[at];
    if (input.edges & PAD_A) {
        if (session_edit(machines[cursor].id, course_cursor)) { snd_event(2); retry_pending = false; (void)request(FZX_SCN_LOAD, RANK_GOTO); }
#endif
    } else if (input.edges & PAD_B) (void)request(FZX_SCN_TITLE, RANK_GOTO);
}
static void select_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    static const char *const levels[] = { "NOVICE", "STANDARD", "EXPERT", "MASTER" };
    dbgio_printf("\x1b[H\x1b[2J\n\n   RACE SELECT\n\n");
    if (select_mode)
        dbgio_printf("   TIME ATTACK\n   COURSE  < %s >\n", course_name(course_cursor));
    else
        dbgio_printf("   GRAND PRIX   JACK CUP\n   CLASS   < %s >\n", levels[select_difficulty]);
    if (jack_ready()) dbgio_printf("   C: CHANGE MODE\n");
#ifndef FZX_P3_BASELINE
    fzx_machine_grade_t grade = fzx_machine_grade(cursor);
    dbgio_printf("\n   MACHINE %02u   UP/DOWN\n   %s\n", cursor+1u, machine_name(cursor));
    dbgio_printf("\n   BODY %c   BOOST %c   GRIP %c\n   WEIGHT %u KG\n",
                 grade.body, grade.boost, grade.grip, grade.weight);
    dbgio_printf("\n   ENGINE BALANCE   %u%%\n   ACCEL < L / R > TOP SPEED\n   X: MIN    Z: MAX\n",
                 (unsigned)(select_balance * 100.f + 0.5f));
#else
    dbgio_printf("\n   MACHINE (3D PLAYER)\n\n");
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) {
        if (!machine_allowed(machines[i].id)) continue;
        dbgio_printf("   %s %-14s %s\n", i == cursor ? ">" : " ", machines[i].name,
                     fzx_closeup_has_family(machines[i].id) ? "" : "(NO 3D PACK)");
    }
#endif
    dbgio_printf("\n\n   A: RACE   B: BACK\n");
}

#ifndef FZX_P3_BASELINE
/* RECORDS owns only its cursor. The backing tables are shared, read-only here. */
typedef struct { uint8_t course, kind; } records_browser_t;
static records_browser_t *browser;

static bool records_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    browser = sh2z_arena_alloc(&menu_arena, sizeof(*browser), 16u);
    if (!browser) return false;
    const fzx_cup_def_t *jack = fzx_cup_def(FZX_CUP_JACK);
    browser->course = 0;
    for (unsigned i = 0; i < jack->rounds; ++i)
        if (jack->course[i] == fzx_session.course) browser->course = i;
    browser->kind = FZX_REC_TA;
    text_dirty = true;
    return true;
}

static void records_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (input.edges & (PAD_B | PAD_START)) {
        (void)request(FZX_SCN_TITLE, RANK_GOTO);
        return;
    }
    unsigned rounds = fzx_cup_def(FZX_CUP_JACK)->rounds;
    if (input.repeat & (PAD_LEFT | PAD_RIGHT)) {
        unsigned step = (input.repeat & PAD_LEFT) ? rounds - 1u : 1u;
        browser->course = (browser->course + step) % rounds;
        text_dirty = true;
    }
    if (input.edges & PAD_C) {
        browser->kind = browser->kind == FZX_REC_TA ? FZX_REC_DEATH_RACE : FZX_REC_TA;
        text_dirty = true;
    }
#else
static bool records_enter(void *u, uint32_t s) { (void)u; (void)s; (void)sh2z_arena_alloc(&menu_arena, 256u, 16u); text_dirty = true; return true; }
static void records_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (input.edges & (PAD_B | PAD_START)) (void)request(FZX_SCN_TITLE, RANK_GOTO);
#endif
}
#ifndef FZX_P3_BASELINE

static void records_line(const fzx_rec_time_t *entry) {
    if (entry->ms >= FZX_REC_MAX_TIMER) {
        dbgio_printf("  --\n");
        return;
    }
    unsigned ms = entry->ms;
    dbgio_printf(" %u:%02u.%03u  %.3s  %s\n", ms / 60000u, ms / 1000u % 60u,
                 ms % 1000u, entry->name, machine_name(entry->machine_id));
}

static void records_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    const unsigned course = fzx_cup_def(FZX_CUP_JACK)->course[browser->course];
    dbgio_printf("\x1b[H\x1b[2J\n  RECORDS    %s\n\n  < %s >\n\n",
                 browser->kind == FZX_REC_TA ? "TIME ATTACK" : "DEATH RACE", course_name(course));
    if (browser->kind == FZX_REC_TA) {
        const fzx_rec_course_t *row = fzx_records_course(course);
        for (unsigned i = 0; i < FZX_REC_TOP; ++i) {
            dbgio_printf(" %u", i + 1u);
            records_line(&row->top[i]);
        }
        dbgio_printf("\n BEST LAP\n ");
        records_line(&row->best_lap);
        if (row->max_speed)
            dbgio_printf(" MAX %lu KM/H   %s\n", (unsigned long)row->max_speed,
                         machine_name(row->max_speed_machine_id));
        else dbgio_printf(" MAX SPEED  --\n");
    } else {
        dbgio_printf(" BEST TIME\n ");
        records_line(&fzx_records_get()->death_race[course]);
    }
    dbgio_printf("\n LEFT/RIGHT COURSE   C MODE   B BACK\n");
#else
static void records_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    dbgio_printf("\x1b[H\x1b[2J\n\n   [PLACEHOLDER RECORDS]\n\n   (records service not linked yet)\n\n   B: BACK\n");
#endif
}

/* The source name keyboard is ten columns wide. Blanks in its final row
 * are holes, > inserts a space, and < accepts the name. */
static const char name_keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ'!.,0123456789       >-<";
enum { NAME_COLUMNS = 10, NAME_ROWS = 5, NAME_END = NAME_COLUMNS * NAME_ROWS - 1 };
_Static_assert(sizeof(name_keys) == NAME_END + 2, "name keyboard layout");

typedef struct {
    fzx_rank_t rank;
    fzx_rec_result_t save;
    unsigned key, length;
    char name[4];
    bool editing, standings;
    unsigned first_row;
} results_ui_t;
static results_ui_t *results;

static void name_move(int dx, int dy) {
    int x = (int)(results->key % NAME_COLUMNS);
    int y = (int)(results->key / NAME_COLUMNS);
    do {
        x = (x + dx + NAME_COLUMNS) % NAME_COLUMNS;
        y = (y + dy + NAME_ROWS) % NAME_ROWS;
        results->key = (unsigned)(y * NAME_COLUMNS + x);
    } while (name_keys[results->key] == ' ');
}

static void name_tick(void) {
    if (!input.repeat && !input.edges) return;
    text_dirty = true;
    if (results->length < 3u) {
        if (input.repeat & PAD_LEFT) name_move(-1, 0);
        if (input.repeat & PAD_RIGHT) name_move(1, 0);
        if (input.repeat & PAD_UP) name_move(0, -1);
        if (input.repeat & PAD_DOWN) name_move(0, 1);
    }
    const char key = name_keys[results->key];
    if (input.edges & (PAD_A | PAD_START)) {
        if (key == '<') {
            results->save = fzx_records_commit(&fzx_scn_result, results->name);
            results->editing = false;
            sh2z_scene_input_consume(&input);
        } else if (input.edges & PAD_A) {
            results->name[results->length++] = key == '>' ? ' ' : key;
            if (results->length == 3u) results->key = NAME_END;
        } else {
            results->key = NAME_END;
        }
    } else if (input.edges & PAD_B) {
        results->key = 0;
        if (results->length) {
            char removed = results->name[--results->length];
            results->name[results->length] = 0;
            results->key = (unsigned)(strchr(name_keys, removed == ' ' ? '>' : removed) - name_keys);
        }
    }
}

static void results_release(void *u, uint32_t s) {
    (void)u; (void)s;
    /* The retained backdrop goes back with RESULTS: VDP2 to the baseline. */
    if (backdrop) { backdrop = false; *vdp2_regs_get() = baseline_regs; }
    dbgio_printf("\x1b[H\x1b[2J");
    dbgio_flush();
}
static const uint8_t place_points[FZX_ENTRANTS_MAX] = {
    100,93,87,81,76,71,66,62,58,54,50,47,44,41,38,35,33,31,29,27,25,23,22,21,20,19,18,17,16,15
};
static void gp_score(const fzx_classification_t *race, int32_t total[FZX_ENTRANTS_MAX]) {
    for (unsigned p = 0; p < race->count; ++p)
        if (race->row[p].scoring) total[race->row[p].entrant] += place_points[p];
}

static bool gp_ahead(const fzx_cup_t *cup, unsigned a, unsigned b) {
    if (cup->total[a] != cup->total[b]) return cup->total[a] > cup->total[b];
    if (!a || !b) return a == 0;
    return cup->last_place[a] < cup->last_place[b];
}

static void gp_draw(const fzx_cup_t *cup) {
#ifdef FZX_P3_BASELINE
    static const char *const names[] = {
        "BLUE FALCON","GOLDEN FOX","WILD GOOSE","FIRE STINGRAY","WHITE CAT","RED GAZELLE","GREAT STAR",
        "IRON TIGER","DEEP CLAW","TWIN NORITTA","SUPER PIRANHA","MIGHTY HURRICANE","LITTLE WYVERN",
        "SPACE ANGLER","GREEN PANTHER","BLACK BULL","WILD BOAR","ASTRO ROBIN","KING METEOR","QUEEN METEOR",
        "WONDER WASP","HYPER SPEEDER","DEATH ANCHOR","CRAZY BEAR","NIGHT THUNDER","BIG FANG",
        "MIGHTY TYPHOON","MAD WOLF","SONIC PHANTOM","BLOOD HAWK","SUPER FALCON","SUPER STINGRAY",
        "SUPER CAT","ROBOTNIK","METAL SONIC"
    };
#endif
    const fzx_classification_t *race = &cup->last;
    dbgio_printf("\x1b[H\x1b[2J\n JACK CUP  %u/6   %s\n", cup->round + 1u, course_name(race->course));
    if (race->outcome != FZX_OUT_FINISH) {
        dbgio_printf("\n %s\n", race->outcome == FZX_OUT_QUIT ? "QUIT" : "RETIRED");
        if (cup->state == FZX_CUP_RETIRED)
            dbgio_printf(" SPARES %d    A: RETRY    B: TITLE\n", (int)cup->live.balance);
        else dbgio_printf(" CUP OVER    A: TITLE\n");
        return;
    }
    uint8_t order[FZX_ENTRANTS_MAX];
    for (unsigned i = 0; i < race->count; ++i) {
        unsigned j = i;
        uint8_t id = race->row[i].entrant;
        while (results->standings && j && gp_ahead(cup, id, order[j-1])) {
            order[j] = order[j-1];
            --j;
        }
        order[j] = id;
    }
    dbgio_printf("\n %s\n\n", !results->standings ? "RACE RESULTS" :
                 cup->state == FZX_CUP_FINAL ? "FINAL STANDINGS" : "CUP STANDINGS");
    for (unsigned p = results->first_row; p < race->count && p < results->first_row + 10u; ++p) {
        unsigned id = order[p], machine = 255u;
        for (unsigned i = 0; i < cup->entrants.count; ++i)
            if (cup->entrants.e[i].entrant == id) { machine = cup->entrants.e[i].machine_id; break; }
#ifndef FZX_P3_BASELINE
        const char *name = machine_name(machine);
#else
        const char *name = machine < sizeof(names)/sizeof(names[0]) ? names[machine] : "MACHINE";
#endif
        dbgio_printf(" %2u %c %-16s ", p+1u, id == 0 ? '>' : ' ', name);
        if (results->standings) dbgio_printf("%3ld PTS\n", (long)cup->total[id]);
        else if (!race->row[p].scoring) dbgio_printf("RET\n");
        else dbgio_printf("+%u%s\n", (unsigned)place_points[p], race->row[p].status == FZX_CL_PROJECTED ? " EST" : "");
    }
    dbgio_printf("\n UP/DOWN: PAGE    A: %s\n", !results->standings ? "STANDINGS" :
                 cup->state == FZX_CUP_FINAL ? "TITLE" : "NEXT COURSE");
}

static bool results_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    results = sh2z_arena_alloc(&menu_arena, sizeof(*results), 16u);
    if (!results) return false;
    memset(results, 0, sizeof(*results));
    if (fzx_cup()->state == FZX_CUP_RACING) {
        if (!fzx_cup_accept(&scn_cls)) return false;
        text_dirty = true;
        return true;
    }
    fzx_records_rank(&fzx_scn_result, &results->rank);
    results->editing = results->rank.place != 0;
    if (!results->editing && (results->rank.best_lap || results->rank.max_speed))
        results->save = fzx_records_commit(&fzx_scn_result, NULL);
    text_dirty = true;
    return true;
}
static void results_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (results->editing) { name_tick(); return; }
    if ((input.edges & PAD_C) && (results->save == FZX_REC_FULL || results->save == FZX_REC_FAILED)) {
        results->save = fzx_records_flush();
        text_dirty = true;
        return;
    }
    if (POKED(fzx_scn_probe) & 0x200u) { /* captures: commit the same run again */
        { const fzx_rec_result_t again = fzx_records_commit(&fzx_scn_result, results->name); fzx_scn_report[14] = 0x200u | (uint32_t)again; }
        POKED(fzx_scn_probe) = 0;
    }
    const fzx_cup_t *cup = fzx_cup();
    if (cup->state != FZX_CUP_IDLE) {
        /* GP: RESULTS writes intents only; the manager resolves LOAD's next course. */
        const uint32_t id = fzx_scn_result.run_id;
        if (cup->state == FZX_CUP_READY) {
            retry_pending = true;
            (void)request(FZX_SCN_LOAD, RANK_GOTO);
            return;
        }
        if ((input.edges & PAD_DOWN) && results->first_row + 10u < cup->last.count) {
            results->first_row += 10u;
            text_dirty = true;
        } else if ((input.edges & PAD_UP) && results->first_row >= 10u) {
            results->first_row -= 10u;
            text_dirty = true;
        }
        if ((input.edges & PAD_A) && cup->last.outcome == FZX_OUT_FINISH && !results->standings) {
            results->standings = true;
            results->first_row = 0;
            text_dirty = true;
            sh2z_scene_input_consume(&input);
        } else if (input.edges & PAD_A) {
            if (cup->state == FZX_CUP_SCORED && fzx_cup_intent(FZX_CUP_NEXT, id)) {
                retry_pending = false;
                (void)request(FZX_SCN_LOAD, RANK_GOTO);
            } else if (cup->state == FZX_CUP_RETIRED && fzx_cup_intent(FZX_CUP_RETRY, id)) {
                retry_pending = true;
                (void)request(FZX_SCN_LOAD, RANK_GOTO);
            } else if (fzx_cup_intent(FZX_CUP_END, id)) (void)request(FZX_SCN_TITLE, RANK_GOTO);
        } else if ((input.edges & PAD_B) && fzx_cup_intent(FZX_CUP_END, id))
            (void)request(FZX_SCN_TITLE, RANK_GOTO);
        return;
    }
    if (input.edges & PAD_A) { snd_event(3); retry_pending = true; ++fzx_scn_report[7]; (void)request(FZX_SCN_LOAD, RANK_GOTO); }
    else if (input.edges & PAD_B) (void)request(FZX_SCN_TITLE, RANK_GOTO);
}
static void results_draw(void) {
    if (!text_dirty) return;
    text_dirty = false;
    if (fzx_cup()->state != FZX_CUP_IDLE) { gp_draw(fzx_cup()); return; }
    const fzx_run_result_t *r = &fzx_scn_result;
    static const char *const why[4] = { "-", "FINISH", "RETIRE", "QUIT" };
#ifndef FZX_P3_BASELINE
    dbgio_printf("\x1b[H\x1b[2J\n\n   RACE RESULTS\n   %s\n   RUN %lu  %s\n\n",
                 machine_name(r->machine_id), (unsigned long)r->run_id, why[r->reason & 3u]);
#else
    dbgio_printf("\x1b[H\x1b[2J\n\n   RACE RESULTS\n\n   RUN %lu  %s\n\n", (unsigned long)r->run_id, why[r->reason & 3u]);
#endif
    dbgio_printf("   TIME  %lu'%02lu\"%02lu\n", (unsigned long)(r->race_ms / 60000u), (unsigned long)((r->race_ms / 1000u) % 60u),
                 (unsigned long)((r->race_ms / 10u) % 100u));
    for (unsigned i = 0; i < r->laps_done && i < 3u; ++i)
        dbgio_printf("   LAP %u %lu'%02lu\"%02lu\n", i + 1u, (unsigned long)(r->lap_ms[i] / 60000u),
                     (unsigned long)((r->lap_ms[i] / 1000u) % 60u), (unsigned long)((r->lap_ms[i] / 10u) % 100u));
    dbgio_printf("   MAX SPEED %lu KM/H\n", (unsigned long)r->max_speed);
    if (results->editing) {
        dbgio_printf("\n   PLACE %u   NAME [%s]\n\n", (unsigned)results->rank.place, results->name);
        for (unsigned row = 0; row < NAME_ROWS; ++row) {
            char line[NAME_COLUMNS * 3 + 1];
            for (unsigned col = 0; col < NAME_COLUMNS; ++col) {
                unsigned k = row * NAME_COLUMNS + col;
                line[col * 3] = k == results->key ? '[' : ' ';
                line[col * 3 + 1] = name_keys[k];
                line[col * 3 + 2] = k == results->key ? ']' : ' ';
            }
            line[NAME_COLUMNS * 3] = 0;
            dbgio_printf("   %s\n", line);
        }
        dbgio_printf("\n   A SELECT  B DELETE  START END\n   > SPACE   < ACCEPT\n");
        return;
    }
    static const char *const rec[5] = { "SAVED", "SAVED (NEW)", "NOT SAVED: FULL", "NOT SAVED: FAILED", "ALREADY" };
    const fzx_rec_course_t *c = fzx_records_course(r->course);
    dbgio_printf("\n   RECORD: PLACE %u%s%s  %s\n", (unsigned)results->rank.place, results->rank.best_lap ? " LAP" : "",
                 results->rank.max_speed ? " SPEED" : "", (results->rank.place || results->rank.best_lap || results->rank.max_speed) ? rec[results->save] : "NO CHANGE");
    if (c) for (unsigned i = 0; i < FZX_REC_TOP; ++i) {
        const uint32_t ms = c->top[i].ms;
        if (ms >= FZX_REC_MAX_TIMER) dbgio_printf("   %u --------\n", i + 1u);
        else dbgio_printf("   %u %lu'%02lu\"%02lu %s\n", i + 1u, (unsigned long)(ms / 60000u),
                          (unsigned long)((ms / 1000u) % 60u), (unsigned long)((ms / 10u) % 100u), c->top[i].name);
    }
    if (results->save == FZX_REC_FULL || results->save == FZX_REC_FAILED) dbgio_printf("   C: RETRY SAVE\n");
    if (scn_cls.valid)
        dbgio_printf("   CLASS %u/%u  %08lX\n", (unsigned)scn_cls.player_place, (unsigned)scn_cls.count,
                     (unsigned long)scn_cls.result_hash);
    dbgio_printf("\n   A: RETRY   B: TITLE\n");
}

/* ---- LOAD ------------------------------------------------------------------ */
#define NBG3_MAP 0x25E7E000u /* VDP2_MAP.md: NBG3 map, 64 x 64 one-word cells */
volatile uint32_t fzx_scn_load_cd[8][3];
static uint32_t turn_field, turn_gap_max;
static void *load_cancel[5]; /* GCC __builtin_setjmp buffer (no libc setjmp here) */
static bool load_running;
/* Progress bar: row 15, columns 3..34, from CD sectors read against the cold load's
 * sector total of the previous cold load (first load: the sector count so far). */
#define BAR_ROW 15u
#define BAR_COLS 32u
static uint32_t bar_total = 674u, bar_drawn; /* sectors of a cold load (measured 2026-11-19) */
static uint16_t bar_on, bar_off;

static uint32_t bar_base;
static void bar_draw(void) {
    uint32_t n = ((fzx_cd_stats[0] - bar_base) * BAR_COLS) / bar_total;
    if (n > BAR_COLS) n = BAR_COLS;
    volatile uint16_t *row = (volatile uint16_t *)(NBG3_MAP + (BAR_ROW * 64u + 3u) * 2u);
    while (bar_drawn < n) row[bar_drawn++] = bar_on;
}

void fzx_scn_stamp(unsigned i) {
    if (i >= 8u) return;
    fzx_scn_load_fields[i] = video_fields_get();
    fzx_scn_load_cd[i][0] = fzx_cd_stats[0];
    fzx_scn_load_cd[i][1] = fzx_cd_stats[2];
    fzx_scn_load_cd[i][2] = turn_gap_max;
}

/* The loading screen's turn, called from inside the CD reads (and while they wait):
 * once per field at most it draws the progress bar, reads the pad and, on the soft
 * reset chord, abandons the load. */
static void load_turn(void) {
    const uint32_t now = video_fields_get();
    if (now == turn_field) return;
    const uint32_t gap = now - turn_field;
    if (gap > turn_gap_max) turn_gap_max = gap;
    turn_field = now;
    bool connected;
    const uint32_t pad = read_pad(&connected);
    fzx_scn_report[31] = pad | (connected ? 0x10000u : 0u);
    if ((pad & PAD_CHORD) == PAD_CHORD && load_running) {
        load_running = false;
        __builtin_longjmp(load_cancel, 1);
    }
}
#define SPIN_ROW 13u
#define SPIN_COL 3u
static volatile uint16_t spin_words[4];
static volatile uint32_t spin_fields, spin_phase;
static bool load_done, load_ran, menu_frame_prev;
static uint32_t menu_scene_prev, menu_phase_prev;
volatile uint32_t fzx_scn_gap_at[8]; /* manager frames over two fields: scene << 16 | phase << 8 | fields */

static volatile uint32_t card_updates;
static void bar_draw(void);
/* VBlank-out: the loading card changes every 2 fields (spinner, progress bar) whatever
 * the CPU is doing in the load. */
static void spin_hook(void) {
    if (++spin_fields % 2u) return;
    spin_phase = (spin_phase + 1u) & 3u;
    *(volatile uint16_t *)(NBG3_MAP + (SPIN_ROW * 64u + SPIN_COL) * 2u) = spin_words[spin_phase];
    bar_draw();
    ++card_updates;
}

#ifndef FZX_P3_BASELINE
/* Nameplates use the original proportional Font 3, baked as 8x8 cells.
 * The manager's NBG3 map displays them while VDP1 is being reclaimed by RACE.
 * B1 0x7b000..0x7bbff and palette 62 are free in the race map. */
static uint16_t card_saved_colors[16];
static uint16_t card_saved_track_color;
static unsigned card_used;
/* The course-menu Jack preview is solid RGB(192,64,64). Its baked 128x64
 * one-bit still expands into B1 0x7a000..0x7afff, just below the nameplate.
 * Each 8x8 character has 8 source bytes and 16 destination words. */
static void card_track(unsigned course) {
    static const uint16_t four_pixels[16] = {
        0,1,0x10,0x11,0x100,0x101,0x110,0x111,
        0x1000,0x1001,0x1010,0x1011,0x1100,0x1101,0x1110,0x1111
    };
    volatile uint16_t *color = (volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0, 0, 977);
    card_saved_track_color = *color;
    *color = RGB1555(1, 24, 8, 8).raw;
    volatile uint16_t *tiles = (volatile uint16_t *)VDP2_VRAM_ADDR(3, 0x1a000);
    for (unsigned i = 0; i < sizeof(card_tracks[0]); ++i) {
        const uint8_t row = card_tracks[course][i];
        tiles[2u*i] = four_pixels[row >> 4];
        tiles[2u*i+1u] = four_pixels[row & 15u];
    }
    volatile uint16_t *map = (volatile uint16_t *)NBG3_MAP;
    for (unsigned i = 0; i < 128u; ++i)
        map[(5u+i/16u)*64u + 22u+i%16u] = (uint16_t)(0xd000u | (0xd00u+i));
}
static void card_show(unsigned course) {
    if (course >= sizeof(cards)/sizeof(cards[0])) return;
    const unsigned cols = cards[course].cols;
    volatile uint16_t *tiles = (volatile uint16_t *)VDP2_VRAM_ADDR(3, 0x1b000);
    volatile uint16_t *palette = (volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0, 0, 992);
    for (unsigned i = 0; i < 16u; ++i) {
        card_saved_colors[i] = palette[i];
        palette[i] = card_colors[i];
    }
    card_used = 32u * cols;
    for (unsigned i = 0; i < card_used; ++i) tiles[i] = card_cells[cards[course].at+i];
    volatile uint16_t *map = (volatile uint16_t *)NBG3_MAP;
    for (unsigned i = 0; i < 2u*cols; ++i)
        map[(3u+i/cols)*64u + (40u-cols)/2u + i%cols] = (uint16_t)(0xe000u | (0xd80u+i));
    card_track(course);
}
static void card_release(void) {
    if (!card_used) return;
    volatile uint16_t *palette = (volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0, 0, 992);
    for (unsigned i = 0; i < 16u; ++i) palette[i] = card_saved_colors[i];
    *(volatile uint16_t *)VDP2_CRAM_MODE_1_OFFSET(0, 0, 977) = card_saved_track_color;
    volatile uint16_t *tiles = (volatile uint16_t *)VDP2_VRAM_ADDR(3, 0x1a000);
    for (unsigned i = 0; i < 2048u + card_used; ++i) tiles[i] = 0;
    card_used = 0;
}

#endif
static bool load_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    load_done = false;
#if ENG_SND
    engine_snd_leave(SND_LOAD_FADE); /* the music fades; load_tick waits for a quiet drive */
#endif
    /* The loading card; the spinner's four glyph words are read back from the map once
     * printed, so the interrupt animates them while the CPU sits in the CD loads. */
#ifndef FZX_P3_BASELINE
    const uint8_t course = fzx_session.menu_mode == 0u && fzx_cup()->state != FZX_CUP_IDLE
                           ? fzx_cup_next_course() : fzx_session.course;
    dbgio_printf("\x1b[H\x1b[2J\x1b[8;5H%s\x1b[10;5H%s\x1b[14;4H|/-\\  LOADING",
                 demo ? "DEMO" : fzx_session.menu_mode ? "TIME ATTACK" : "JACK CUP",
                 machine_name(fzx_session.machine_id));
#else
    const char *name = "?";
    for (unsigned i = 0; i < MACHINE_COUNT; ++i) if (machines[i].id == fzx_session.machine_id) name = machines[i].name;
#if ENG_COURSE_SELECT
    const fzx_course_info_t *ci = fzx_course_info_row(
        fzx_session.menu_mode == 0u && fzx_cup()->state != FZX_CUP_IDLE ? fzx_cup_next_course() : fzx_session.course);
    dbgio_printf("\x1b[H\x1b[2J\n\n\n\n\n     %s\n\n     TIME ATTACK\n\n     %s\n\n\n\n   |/-\\  LOADING\n",
                 ci ? ci->key : "?", name);
#else
    dbgio_printf("\x1b[H\x1b[2J\n\n\n\n\n     MUTE CITY\n\n     TIME ATTACK\n\n     %s\n\n\n\n   |/-\\  LOADING\n", name);
#endif
#endif
    dbgio_flush();
    for (unsigned k = 0; k < 4u; ++k) spin_words[k] = *(volatile uint16_t *)(NBG3_MAP + (SPIN_ROW * 64u + 3u + k) * 2u);
    dbgio_printf("\x1b[14;5H   ");
    dbgio_printf("\x1b[16;4H                                ");
    dbgio_flush();
    /* The bar's glyphs: the card's full-block-like '=' and the space, read back. */
    dbgio_printf("\x1b[16;4H=");
    dbgio_flush();
    bar_on = *(volatile uint16_t *)(NBG3_MAP + (BAR_ROW * 64u + 3u) * 2u);
    bar_off = *(volatile uint16_t *)(NBG3_MAP + (BAR_ROW * 64u + 4u) * 2u);
    *(volatile uint16_t *)(NBG3_MAP + (BAR_ROW * 64u + 3u) * 2u) = bar_off;
#ifndef FZX_P3_BASELINE
    card_show(course);
#endif
    text_dirty = false;
    return true;
}

static void load_tick(void *u, uint32_t s) __attribute__((noinline));
static void load_tick(void *u, uint32_t s) {
    (void)u; (void)s;
    if (load_done || seq.phase != SH2Z_SCENE_RUN) return;
#if ENG_SND
    if (!engine_snd_quiet()) return; /* no disc read until the music has faded and the drive paused */
#endif
    const uint32_t family = fzx_session.machine_id;
    load_course = fzx_session.course;
    if (fzx_session.menu_mode == 0u && fzx_session.confirmed) {
        /* GP: the manager owns the course (SCENES.md 11.5); a confirmed GP session with no cup begins one. */
        if (fzx_cup()->state == FZX_CUP_IDLE) {
            fzx_entrants_t e;
            if (
#if E1_FIELD
                fzx_field_entrants(&e, (uint8_t)fzx_session.machine_id, fzx_session.skin) &&
#else
                false &&
#endif
                fzx_cup_gp_ready(fzx_session.cup)) (void)fzx_cup_begin(fzx_session.cup, fzx_session.difficulty, &e);
        }
        load_course = fzx_cup_next_course();
    }
#if ENG_COURSE_SELECT
    if (course_ok(load_course, fzx_session.menu_mode) && !fzx_course_use(load_course)) { /* no packs for it on this disc: back to select */
        fzx_scn_report[16] = 61u;
        load_done = true;
#ifndef FZX_P3_BASELINE
        (void)request(demo ? FZX_SCN_TITLE : FZX_SCN_SELECT, RANK_GOTO);
#else
        (void)request(FZX_SCN_SELECT, RANK_GOTO);
#endif
        return;
    }
#endif
    if (!fzx_session.confirmed || !machine_allowed(family) || !fzx_closeup_has_family(family) || !course_ok(load_course, fzx_session.menu_mode)) {
        fzx_scn_report[16] = 60u; /* refused: back to select */
        load_done = true;
#ifndef FZX_P3_BASELINE
        (void)request(demo ? FZX_SCN_TITLE : FZX_SCN_SELECT, RANK_GOTO);
#else
        (void)request(FZX_SCN_SELECT, RANK_GOTO);
#endif
        return;
    }
    const uint32_t f0 = video_fields_get();
    load_ran = true;
    spin_fields = 0;
    fzx_scn_vblank_hook = spin_hook;
#if ENG_COURSE_SELECT
    const uint32_t key = ((uint32_t)load_course << 16) + family + 1u; /* course and family */
#else
    const uint32_t key = family + 1u;
#endif
    if (retry_pending && resident_key == key) {
        /* Retry: everything is resident; only race-local state goes back to its start. */
        fzx_drive_reset();
        fzx_closeup_reset();
        retry_entering = race_regs_kept;
        memset((void *)fzx_scn_load_fields, 0, sizeof(fzx_scn_load_fields));
        fzx_scn_report[11] = 2;
    } else {
        /* Cold load: the loading screen takes a turn inside every CD read (load_turn). */
        const uint32_t s0 = fzx_cd_stats[0];
        turn_field = video_fields_get(); turn_gap_max = 0; bar_drawn = 0; bar_base = s0;
        fzx_cd_clock = video_fields_get;
        fzx_cd_yield = load_turn;
        load_running = true;
        if (__builtin_setjmp(load_cancel)) {
            /* Soft reset mid-load: drop everything the load made, back to TITLE. */
            fzx_cd_yield = NULL;
            fzx_scn_vblank_hook = NULL;
            fzx_texture_cd_reset();
            *vdp2_regs_get() = baseline_regs;
            resident_key = 0; race_regs_kept = false; retry_entering = false;
            ++fzx_scn_report[9]; ++fzx_scn_report[21];
            chord_latched = 1;
#ifndef FZX_P3_BASELINE
            demo_stop();
#endif
            session_defaults();
            sh2z_scene_input_consume(&input);
            load_done = true;
            fzx_scn_report[10] = video_fields_get() - f0;
            fzx_scn_report[11] = 3; /* cancelled */
            /* As a finished load does (below): settle VDP1 and VDP2 and put the first list after
             * the idle load inside this frame, so the LEAVE frame that follows does not take the
             * three-field first show (2026-11-20, Ymir gap dump: LOAD LEAVE, 3 fields, at cancel 30). */
            vdp1_sync_wait();
            vdp2_sync_wait();
            own_frame();
            vdp1_sync_wait();
            vdp2_sync_wait();
            (void)sh2z_scene_request(&seq, FZX_SCN_TITLE, RANK_RESET);
            return;
        }
        fzx_race_load();
#if ENG_ROSTER
        {   /* The roster: fzx_race_load has loaded the field's families selectively
             * (fzx_drive_roster_load, fzx_scn_roster[0] its fields). Check: the first
             * loaded family's near set, inflated back to RSP1 1.1, against its raw FNV. */
            uint32_t fam = 0;
            while (fam < 30u && !fzx_roster_set(fam, FZX_ROSTER_NEAR)) ++fam;
            const char key[5] = { 'c', (char)('0' + fam / 10u), (char)('0' + fam % 10u), 's', FZX_STOCK_RIVAL_SKIN };
            fzx_scn_roster[1] = fam < 30u ? roster_inflate_fnv(fam, FZX_ROSTER_NEAR) : 0u;
            fzx_scn_roster[2] = 0;
            for (unsigned i = 0; i < FZX_RZ_SETS; ++i)
                if (!memcmp(fzx_rz_sets[i].key, key, 5) && !memcmp(fzx_rz_sets[i].prof, "near", 4))
                    fzx_scn_roster[2] = fzx_rz_sets[i].raw_fnv;
            fzx_scn_roster[3] = fzx_scn_roster[1] && fzx_scn_roster[1] == fzx_scn_roster[2];
            fzx_scn_roster[4] = fam;
            fzx_scn_stamp(7);
        }
#endif
        fzx_race_render_callback(false); /* until RACE enters */
        load_running = false;
        fzx_cd_yield = NULL;
        bar_total = fzx_cd_stats[0] - s0;
        race_regs_kept = false; retry_entering = false;
        resident_key = key;
        ++fzx_scn_report[8];
        fzx_scn_report[11] = 1;
    }
    /* The card's VBlank animation stays on until LOAD leaves (its first frames after the
     * load can wait on VDP1, see load_leave). */
    fzx_scn_report[10] = video_fields_get() - f0;
    fzx_scn_report[23] = fzx_ab_report[0];
    fzx_scn_report[24] = family;
    fzx_scn_report[29] = resident_key;
    retry_pending = false;
    load_done = true;
    /* Settle VDP1 and VDP2 inside the load's own frame, and put the first list after the
     * load here too: the first put after an idle VDP1 is shown three fields later, which
     * the next manager frame would otherwise take (2026-11-19, Ymir probe on the first LOAD
     * LEAVE frames). The load's frame then puts no list of its own (see fzx_scn_main), and
     * the manager frames after it start at a frame change, on the two-field cadence. */
    vdp1_sync_wait();
    vdp2_sync_wait();
    own_frame();
    vdp1_sync_wait();
    vdp2_sync_wait();
    (void)request(FZX_SCN_RACE, RANK_GOTO);
}
static void load_draw(void) {}
/* The VBlank spinner stays on until LOAD leaves, so the card moves from the first field
 * of the load to the fade out. */
#ifndef FZX_P3_BASELINE
static void load_leave(void *u, uint32_t s) { (void)u; (void)s; leaving = true; fzx_scn_vblank_hook = NULL; card_release(); }
#else
static void load_leave(void *u, uint32_t s) { (void)u; (void)s; leaving = true; fzx_scn_vblank_hook = NULL; }
#endif

/* ---- RACE ------------------------------------------------------------------ */
static uint32_t end_reason, end_wait, max_kmh;
static bool cls_armed, cls_done;
static uint32_t race_run_id;

/* SCENES.md 11.3: the classification at the official trigger, from the drive's finish timer
 * (one menu update per simulation tick). */
static fzx_cl_key_t cls_key(uint8_t outcome) {
    fzx_cl_key_t k;
    memset(&k, 0, sizeof(k));
    const fzx_cup_t *cup = fzx_cup();
    k.run_id = race_run_id; k.cup = cup->state ? cup->cup : 0u; k.round = cup->state ? cup->round : 0u;
    k.course = load_course; k.outcome = outcome; k.player_kos = cup->live.kos;
    return k;
}
static void race_finish_hook(uint32_t timer, uint32_t tick) {
    if (timer == 1u) fzx_scn_cls[0] = tick;
    if (cls_done || timer < FZX_FINISH_TRIGGER_TICKS) return;
    fzx_cl_in_t in[FZX_ENTRANTS_MAX];
    fzx_cl_key_t k = cls_key(FZX_OUT_FINISH);
#if E1_FIELD
    const unsigned n = fzx_field_classify_in(in, fzx_drive_state()->race_time, &k);
#else
    const unsigned n = 0u; /* no rival field: nothing to classify */
#endif
    cls_done = true;
    if (!n || !fzx_classify(&scn_cls, &k, in, n)) { fzx_scn_cls[3] = 0xBADu; return; }
    unsigned projected = 0;
    for (unsigned p = 0; p < scn_cls.count; ++p) projected += scn_cls.row[p].status == FZX_CL_PROJECTED;
    fzx_scn_cls[1] = timer; fzx_scn_cls[2] = (uint32_t)k.frame; fzx_scn_cls[3] = scn_cls.result_hash;
    fzx_scn_cls[4] = scn_cls.attempt_hash; fzx_scn_cls[5] = scn_cls.player_place; fzx_scn_cls[6] = scn_cls.count;
    fzx_scn_cls[7] = projected;
    if (fzx_cup()->state == FZX_CUP_RACING) end_wait = 1u; /* GP: RESULTS at the trigger */
}
extern void (*volatile fzx_drive_finish_hook)(uint32_t timer, uint32_t tick);

static bool race_enter(void *u, uint32_t s) {
    (void)u; (void)s;
    (void)sh2z_arena_alloc(&race_arena, 256u, 16u);
    if (retry_entering) { *vdp2_regs_get() = race_regs; retry_entering = false; }
    fzx_race_render_callback(true);
#if ENG_SND
    engine_snd_start(seq.epoch); /* engine voices, the race's sound epoch, the course music (no waits) */
#endif
    fzx_race_begin();
    end_reason = FZX_END_NONE; end_wait = 0; max_kmh = 0;
    ++run_counter;
    fzx_scn_report[12] = (fzx_records_boot_id() << 16) | (run_counter & 0xFFFFu);
    race_run_id = fzx_scn_report[12];
    memset(&scn_cls, 0, sizeof(scn_cls));
    for (unsigned i = 0; i < 8u; ++i) fzx_scn_cls[i] = 0;
    const bool gp = fzx_cup()->state == FZX_CUP_READY && fzx_cup_race_start(race_run_id);
    cls_armed = gp || POKED(fzx_scn_gp_probe) != 0u; cls_done = false;
    fzx_drive_finish_hook = cls_armed ? race_finish_hook : NULL;
    dbgio_printf("\x1b[H\x1b[2J");
    return true;
}

static void race_snapshot(uint32_t reason) {
    const sh2z_vd_body_t *b = fzx_drive_state();
    fzx_run_result_t *r = &fzx_scn_result;
    memset(r, 0, sizeof(*r));
    r->run_id = (fzx_records_boot_id() << 16) | (run_counter & 0xFFFFu); r->reason = (uint8_t)reason;
    r->menu_mode = fzx_session.menu_mode; r->course = load_course; r->skin = fzx_session.skin;
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

/* The pause menu (SCENES.md 11.4). START pauses a race in progress: the drive holds at its tick
 * (frames keep presenting, physics and rivals frozen, no catch-up after). RETRY produces the
 * PAUSE_RETRY outcome: in GP the manager applies it (fzx_cup_accept checks the lock before any
 * change; a refusal keeps the race paused); in Time Attack it restarts the run. QUIT ends the
 * race as the forced QUIT does. fzx_scn_pause: [0] pauses, [1] retries produced, [2] retries
 * refused (locked), [3] resumes, [4] quits, [5] the menu cursor. */
volatile uint32_t fzx_scn_pause[8];
extern volatile uint32_t fzx_e0_hold_tick;
#define HOLD (*(volatile uint32_t *)((uintptr_t)&fzx_e0_hold_tick | CPU_CACHE_THROUGH))
static bool paused;
static unsigned pause_at;
static void pause_draw(bool locked) {
    static const char *const item[3] = { "CONTINUE", "RETRY", "QUIT" };
    dbgio_printf("\x1b[H\x1b[2J\n\n\n\n\n\n\n\n             PAUSE\n\n");
    for (unsigned i = 0; i < 3u; ++i)
        dbgio_printf("           %s %s%s\n", i == pause_at ? ">" : " ", item[i], i == 1u && locked ? " (NO SPARES)" : "");
    dbgio_flush();
    fzx_scn_pause[5] = pause_at;
}
static void pause_end(void) {
    paused = false; HOLD = 0u;
    dbgio_printf("\x1b[H\x1b[2J");
    dbgio_flush();
}
static void race_quit(void);
static void race_pause_retry(void);
static void pause_tick(void) {
    const bool gp = fzx_cup()->state == FZX_CUP_RACING;
    const bool locked = gp && fzx_cup_live()->retry_locked;
    if (input.repeat & (PAD_UP | PAD_DOWN)) {
        pause_at = (pause_at + ((input.repeat & PAD_UP) ? 2u : 1u)) % 3u;
        pause_draw(locked);
    }
    if (!(input.edges & (PAD_A | PAD_START))) return;
    sh2z_scene_input_consume(&input);
    if (pause_at == 0u || (input.edges & PAD_START && !(input.edges & PAD_A))) { ++fzx_scn_pause[3]; pause_end(); }
    else if (pause_at == 1u) race_pause_retry();
    else { ++fzx_scn_pause[4]; pause_end(); race_quit(); }
}

static void race_tick(void *u, uint32_t s) {
    (void)u; (void)s;
#ifndef FZX_P3_BASELINE
    if (demo) {
        const uint32_t ticks = fzx_drive_tick();
        if ((input.edges & (PAD_A | PAD_C | PAD_START)) || ticks >= 1800u ||
            (fzx_drive_state()->flags & (SH2Z_VD_FINISHED | SH2Z_VD_CRASHED | SH2Z_VD_FALLING)))
            (void)request(FZX_SCN_TITLE, RANK_GOTO);
        return;
    }
#endif
    if (paused) { pause_tick(); return; }
    if (end_reason == FZX_END_NONE && (input.edges & PAD_START) && fzx_drive_tick() > 0u) {
        paused = true; pause_at = 0u; HOLD = fzx_drive_tick();
        ++fzx_scn_pause[0];
        sh2z_scene_input_consume(&input);
        pause_draw(fzx_cup()->state == FZX_CUP_RACING && fzx_cup_live()->retry_locked);
        return;
    }
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
        /* GP: a retire or quit is outcome-only, written at the event (SCENES.md 11.3). */
        if (end_reason != FZX_END_NONE && end_reason != FZX_END_FINISH && fzx_cup()->state == FZX_CUP_RACING) {
            const fzx_cl_key_t k = cls_key(end_reason == FZX_END_RETIRE ? FZX_OUT_RETIRE : FZX_OUT_QUIT);
            (void)fzx_cl_outcome_only(&scn_cls, &k);
            cls_done = true;
        }
    } else if (end_reason == FZX_END_FINISH && cls_armed && !cls_done) {
        /* The rivals race on until the classification trigger. */
    } else if (end_wait && --end_wait == 0u) {
        (void)request(FZX_SCN_RESULTS, RANK_END_RACE);
    }
}

static void race_quit(void) {
    end_reason = FZX_END_QUIT; end_wait = 30u;
    race_snapshot(end_reason);
    if (fzx_cup()->state == FZX_CUP_RACING) {
        const fzx_cl_key_t k = cls_key(FZX_OUT_QUIT);
        (void)fzx_cl_outcome_only(&scn_cls, &k);
        cls_done = true;
    }
}
static void race_pause_retry(void) {
    if (fzx_cup()->state == FZX_CUP_RACING) {
        fzx_classification_t c;
        const fzx_cl_key_t k = cls_key(FZX_OUT_PAUSE_RETRY);
        if (!fzx_cl_outcome_only(&c, &k) || !fzx_cup_accept(&c)) {
            ++fzx_scn_pause[2]; /* locked (no spares at race start): no debit, no transition */
            pause_draw(true);
            return;
        }
        scn_cls = c; cls_done = true;
    }
    ++fzx_scn_pause[1];
    end_reason = FZX_END_PAUSE_RETRY;
    race_snapshot(end_reason);
    pause_end();
    retry_pending = true; /* same course: the resident set is reused */
    (void)request(FZX_SCN_LOAD, RANK_GOTO);
}

static void race_release(void *u, uint32_t s) {
    (void)u; (void)s;
    paused = false; HOLD = 0u;
    fzx_race_render_callback(false);
    race_regs = *vdp2_regs_get();
    race_regs_kept = true;
    if (seq.next == FZX_SCN_RESULTS) {
        /* The backdrop is handed to RESULTS as a retained lease: RBG0, the back screen and
         * the race's last VDP1 list (its textures and the bank it read stay as they are). */
        unsigned capacity;
        (void)fzx_race_list(&backdrop_count, &capacity);
        backdrop = backdrop_count >= 3u;
    } else {
        /* RACE's VDP2 lease back to the baseline (registers; RBG0 off, the back screen black). */
        *vdp2_regs_get() = baseline_regs;
    }
    dbgio_printf("\x1b[H\x1b[2J");
    dbgio_flush();
    vdp2_sync(); vdp2_sync_wait();
}

/* ---- the table ----------------------------------------------------------- */
static bool boot_enter(void *u, uint32_t s) {
    (void)u; (void)s;
#ifndef FZX_P3_BASELINE
    if (!title_cache()) fault(62, "TITLE PACK");
    fzx_scn_menu_hooks_set(FZX_SCN_TITLE, &title_hooks);
    fzx_scn_menu_hooks_set(FZX_SCN_SELECT, &select_hooks);
#endif
    fzx_cup_score_hook(gp_score);
    (void)fzx_records_load(); /* SCENES.md section 5: the saved tables and a new boot id */
    return true;
}
static void boot_tick(void *u, uint32_t s) { (void)u; (void)s; (void)request(FZX_SCN_TITLE, RANK_GOTO); }
static void common_leave(void *u, uint32_t s) { (void)u; (void)s; leaving = true; }
#if ENG_SND
static void race_leave(void *u, uint32_t s) { common_leave(u, s); engine_snd_leave(SND_RACE_FADE); }
#else
#define race_leave common_leave
#endif

/* ---- menu preload dispatch (SCENES.md section 2; C-070) ----------------------------- */
#define PRELOAD_BUDGET_FRT 7000u /* about 7.8 ms a frame (FRT at master clock / 32) */
static uint32_t preload_frames;
static void preload_cancel(void) {
    if (preloading < FZX_SCN_COUNT && hooks[preloading].preload_cancel) hooks[preloading].preload_cancel();
    if (preloading < FZX_SCN_COUNT) ++fzx_scn_ctl[3];
    preloading = FZX_SCN_COUNT;
}
/* scene-1 calls this once per frame in PRELOAD, after the previous scene's release. */
static bool menu_preload(void *u, uint32_t scene) {
    (void)u;
    if (preloading != scene) { preload_cancel(); preloading = scene; preload_frames = 0; }
    /* Never write VRAM a displayed list may still read: defer until the lease is free. */
    if (!fzx_scn_lease_free()) { ++fzx_scn_ctl[1]; return false; }
    const fzx_scn_menu_hooks_t *h = &hooks[scene];
    if (!h->preload_step) { preloading = FZX_SCN_COUNT; return true; }
    ++fzx_scn_ctl[2];
    if (++preload_frames > fzx_scn_ctl[5]) fzx_scn_ctl[5] = preload_frames;
    if (h->preload_step(PRELOAD_BUDGET_FRT)) { preloading = FZX_SCN_COUNT; ++fzx_scn_ctl[4]; return true; }
    return false;
}

/* Capture-only test hooks (probe 0x1000): a one-command span and a 30-frame preload, to
 * exercise the span, the lease and the stepped preload before the C8 screens bind. */
static uint32_t test_steps;
static unsigned test_draw(vdp1_cmdt_t *span, unsigned capacity) {
    if (!capacity) return 0;
    span[0] = menu_cmds[2];
    return 1;
}
static bool test_preload(uint32_t budget) { (void)budget; return ++test_steps >= 30u; }
static void test_cancel(void) { test_steps = 0; }
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
    [FZX_SCN_TITLE]   = { menu_preload, title_enter, title_tick, common_leave, title_release, menu_arenas, 1 },
    [FZX_SCN_SELECT]  = { menu_preload, select_enter, select_tick, common_leave, common_release, menu_arenas, 1 },
    [FZX_SCN_LOAD]    = { NULL, load_enter, load_tick, load_leave, common_release, NULL, 0 },
    [FZX_SCN_RACE]    = { NULL, race_enter, race_tick, race_leave, race_release, race_arenas, 1 },
    [FZX_SCN_RESULTS] = { menu_preload, results_enter, results_tick, common_leave, results_release, menu_arenas, 1 },
    [FZX_SCN_RECORDS] = { menu_preload, records_enter, records_tick, common_leave, common_release, menu_arenas, 1 },
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

    menu_list_init();
    /* Yaul leaves the end-of-draw interrupt masked; unmask it as the race load does
     * (which later chains its stamp ahead of Yaul's handler). */
    {
        const uint32_t mask = cpu_intc_mask_get();
        cpu_intc_mask_set(15);
        scu_ic_mask_chg(SCU_IC_MASK_ALL & ~SCU_IC_MASK_SPRITE_END, SCU_IC_MASK_NONE);
        cpu_intc_mask_set(mask);
    }
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
#ifndef FZX_P3_BASELINE
    /* Queue the manager's black VDP1 list after the CD reads, so its first
     * visible frame starts with the same active display pipeline as later ones.
     * Then wait for it, as a load's end does (load_tick): the first put after the idle
     * boot is shown two or three fields later, by where in a field the CD reads end, and
     * the first manager frame would otherwise take that wait (2026-11-20, Ymir gap dump:
     * BOOT phase 0, 3 fields, on images whose layout moved). */
    own_frame();
    vdp1_sync_wait();
    vdp2_sync_wait();
#endif
    frame_fields = video_fields_get();
    for (;;) {
        const uint32_t now = video_fields_get();
        uint32_t steps = now - frame_fields;
        /* Manager frame cadence outside RACE (fzx_scn_ctl[6] worst gap, [7] gaps over two
         * fields): the gap measured here belongs to the previous iteration, counted only when
         * that iteration was a manager frame that ran no blocking load. */
        if (menu_frame_prev && steps > fzx_scn_ctl[6]) fzx_scn_ctl[6] = steps;
        if (menu_frame_prev && steps > 2u) { ++fzx_scn_ctl[7]; fzx_scn_gap_at[fzx_scn_ctl[7] & 7u] = (menu_scene_prev << 16) | menu_phase_prev << 8 | steps; }
        frame_fields = now;
        if (steps == 0u) steps = 1u;
        if (steps > 4u) steps = 4u;
        steps_this_frame = steps;
        fzx_scn_report[20] += steps;
        bool connected = false;
        const uint32_t pad = read_pad(&connected);
#ifndef FZX_P3_BASELINE
        title_connected = connected;
#endif
        fzx_scn_report[31] = pad | (connected ? 0x10000u : 0u);
        {   /* Captures: the menu hook controls. */
            const uint32_t probe = POKED(fzx_scn_probe);
            if (probe & 0x1000u) {
                static const fzx_scn_menu_hooks_t t = { test_preload, test_cancel, test_draw };
                fzx_scn_menu_hooks_set(FZX_SCN_TITLE, &t);
                fzx_scn_menu_hooks_set(FZX_SCN_SELECT, &t);
                POKED(fzx_scn_probe) = 0;
            } else if (probe & 0x2000u) {
                (void)fzx_records_rewrite();
                POKED(fzx_scn_probe) = 0;
            } else if (probe & 0x800u) {
                /* Control: a preload that starts while the current scene's lease is still
                 * referenced must defer. */
                const uint32_t before = fzx_scn_ctl[1];
                (void)menu_preload(NULL, FZX_SCN_SELECT);
                fzx_scn_report[14] = 0x800u | (fzx_scn_ctl[1] - before);
                preload_cancel();
                POKED(fzx_scn_probe) = 0;
            }
        }
        /* Soft reset: the chord fires once, then stays latched until all four are released. */
        if ((pad & PAD_CHORD) == PAD_CHORD) {
            if (!chord_latched) {
                chord_latched = 1;
                if (seq.current != FZX_SCN_BOOT && sh2z_scene_request(&seq, FZX_SCN_TITLE, RANK_RESET)) {
                    preload_cancel(); /* a preload in progress is abandoned */
                    ++fzx_scn_report[9];
                    ++fzx_scn_report[21];
#ifndef FZX_P3_BASELINE
                    demo_stop();
#endif
                    session_defaults();
                    fzx_cup_reset();
                    fzx_drive_finish_hook = NULL;
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
        /* Extra 60 Hz steps for the menus (LOAD starts its load from the frame's own tick, so
         * the frame before it has reported RUN). */
        for (uint32_t k = 1; k < steps && seq.phase <= SH2Z_SCENE_RUN && seq.current != FZX_SCN_RACE &&
                             seq.current != FZX_SCN_LOAD; ++k) {
            (void)sh2z_scene_input_update(&input, chord_latched ? 0u : pad);
            if (seq.pending == SH2Z_SCENE_NONE) table[seq.current].tick(NULL, seq.current);
        }
        if (seq.phase == SH2Z_SCENE_FAULT) fault(2, "ENTER");
        apply_fade(level);
        snd_deliver();
#if ENG_SND
        if (seq.current != FZX_SCN_RACE) engine_snd_idle(); /* the race frame services the CD itself */
#endif
        if (load_ran) {
            /* The frame that ran a blocking load has put its list inside the load (load_tick);
             * the card stays on screen, its spinner is VBlank-driven. */
        } else if (sh2z_scene_drawing(&seq)) {
            own_frames = 0;
            if (seq.current == FZX_SCN_RACE) (void)fzx_race_frame();
            else { const bool printed = text_dirty; draw_current(); text_frame(printed); }
        } else {
            own_frame();
            ++own_frames;
            ++fzx_scn_report[22];
            /* Confirmed retirement: the manager's own list has replaced the scene's last
             * one. own_frame waits for the previous VDP1 frame (vdp1_sync_wait) before it
             * puts its list, so at the second own frame the first manager list has been
             * drawn and shown: nothing VDP1 reads or shows still references the scene. */
            if (seq.phase == SH2Z_SCENE_RETIRE && own_frames >= 2u) {
                lease_owner = FZX_SCN_COUNT; /* the bare list has replaced every span */
                sh2z_scene_retired(&seq);
            }
        }
#if ENG_SCN_STALL_CONTROL
        /* Engine gate control (ctl-menus): a write to fzx_scn_stall_ctl holds this menu frame for 3 fields. */
        if (POKED(fzx_scn_stall_ctl) && seq.current != FZX_SCN_RACE && !load_ran) {
            POKED(fzx_scn_stall_ctl) = 0;
            const uint32_t f0 = video_fields_get();
            while (video_fields_get() - f0 < 3u) { }
        }
#endif
        menu_scene_prev = seq.current; menu_phase_prev = seq.phase;
        menu_frame_prev = seq.current != FZX_SCN_RACE && !load_ran && !(seq.phase <= SH2Z_SCENE_RUN && seq.current == FZX_SCN_RACE);
        load_ran = false;
        fzx_scn_report[1] = seq.current; fzx_scn_report[2] = seq.phase; fzx_scn_report[3] = level;
        fzx_scn_report[4] = seq.epoch; fzx_scn_report[5] = seq.frames; fzx_scn_report[6] = seq.switches;
        fzx_scn_report[17] = menu_arena.high; fzx_scn_report[18] = race_arena.high;
        fzx_scn_report[19] = video_fields_get(); fzx_scn_report[25] = seq.redirects;
        (void)connected;
    }
}
