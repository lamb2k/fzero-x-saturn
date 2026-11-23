/* Engine E1 close-up A/B, arm A: the player as the 3D model (see engine_closeup.h). */
#include <string.h>
#include <stddef.h>
#include "engine_closeup.h"
#include "fzx_vehicle.h"
#include "texture_cd.h"
#ifndef ENG_SCENES
#define ENG_SCENES 0
#endif
#if ENG_SCENES
/* Scenes image: the player's 3D pack per machine family (MACH3D<ff>.PAK, tools/mach3d_packs.py;
 * mach3d_identity.h is indexed by family, pack_bytes 0 = no pack). Work RAM and the VDP1
 * region take the largest pack the image may load; the loaded pack's sizes are in `loaded`. */
#include "mach3d_identity.h"
#ifndef FZX_P3_BASELINE
#if FZX_GUEST_MASK
#include "guest_machine_identity.h"
#endif
#define PACK_MAX 48128u
#define TEXEL_SRC 23264u
#else
#define PACK_MAX 40960u
#define TEXEL_SRC 20480u
#endif
#define SHADE_SRC 2048u
#define PALETTE_NOW loaded.palette_bytes
#define GOURAUD_NOW loaded.gouraud_bytes
#else
#include "machine_rebake_identity.h"
#include "closeup_identity.h"
#define PACK_MAX FZX_CLOSEUP_PACK_BYTES
#define TEXEL_SRC FZX_MACHINE_REBAKE_TEXTURE_BYTES
#define SHADE_SRC (FZX_MACHINE_REBAKE_PALETTE_BYTES + FZX_MACHINE_REBAKE_GOURAUD_BYTES)
#define PALETTE_NOW FZX_MACHINE_REBAKE_PALETTE_BYTES
#define GOURAUD_NOW FZX_MACHINE_REBAKE_GOURAUD_BYTES
#endif
#include "sh2zam/vehicle_dynamics_experimental.h"

/* engine_drive.c: the player's physics body (E1_CLOSEUP=1 only). */
const sh2z_vd_body_t *fzx_drive_body(void);

volatile uint32_t fzx_ab_report[16];
/* LOD for fzx_closeup_draw_pose (the source's numbering, 1..5): set by the caller per car. */
volatile uint32_t fzx_closeup_lod = 1;
fzx_vehicle_stats_t fzx_ab_vehicle_stats;

#define ALIGN32(x) (((x) + 31u) & ~31u)
/* 3D car slots: shade tables (palettes + Gouraud words) and template tables per slot
 * and per VDP1 bank; the texels are shared. FZX_CLOSEUP_CARS 1 is arm A's layout. */
#ifndef FZX_CLOSEUP_CARS
#define FZX_CLOSEUP_CARS 1
#endif
enum {
    TEXEL_BYTES = ALIGN32(TEXEL_SRC),
    SHADE_BYTES = ALIGN32(SHADE_SRC),
    REGION_BYTES = TEXEL_BYTES + 2u * FZX_CLOSEUP_CARS * SHADE_BYTES
};

#if !ENG_SCENES
_Static_assert(FZX_CLOSEUP_META_BYTES == FZX_MACHINE_REBAKE_METADATA_BYTES &&
               FZX_CLOSEUP_TEXEL_BYTES == FZX_MACHINE_REBAKE_TEXTURE_BYTES &&
               FZX_CLOSEUP_PACK_BYTES == FZX_CLOSEUP_META_BYTES + FZX_CLOSEUP_TEXEL_BYTES &&
               (FZX_MACHINE_REBAKE_METADATA_BYTES & 3u) == 0u, "MACHAB.PAK is MACHRLD.PAK then MACHRTX.PAK");
#endif
/* The whole CD pack; the metadata stays here, the texel half is copied to VRAM at load. */
static uint32_t meta[PACK_MAX / 4u] __attribute__((aligned(32)));
static fzx_vehicle_pack_t pack;
static fzx_vehicle_work_t work __attribute__((aligned(32)));
#if defined(ENG_FX) && ENG_FX
/* Race effects: the body colour changes every frame while the player flashes or flickers. Its
 * palettes go to the player's own table only (both banks, as each is next built); the rivals'
 * tables keep the load's palettes. gen 1 = the load's palettes (base_pal), 0 = not yet written. */
static uint16_t base_pal[FZX_VEHICLE_PALETTES][16];
static uint32_t pal_gen, table_gen[2][FZX_CLOSEUP_CARS];
static bool draw_player;
#endif
#if ENG_LOW_COLD
/* Also the host of the low work RAM block's load image (engine_low.x .bsshost): written only after
 * engine_low_init() has copied the image down and zeroed it, first thing in main. */
__attribute__((section(".bss.lowhost.closeup_tables")))
#endif
static fzx_vehicle_native_tables_t tables[2][FZX_CLOSEUP_CARS] __attribute__((aligned(32))); /* per bank, per slot */
static uint8_t *pixels, *shades[2][FZX_CLOSEUP_CARS];
static uint16_t texture_ctrl, texture_pmod, solid_ctrl, solid_pmod;
static RefMachine state;
static RefCamera camera;

/* Q(shift) fixed point to binary32 by integer operations, rounded to nearest even
 * (equal to (float)v / 2^shift; no software floating point per frame). */
static float fixed_to_float(int32_t v, unsigned shift) {
    union { uint32_t u; float f; } r;
    if (!v) { r.u = 0; return r.f; }
    const uint32_t sign = v < 0 ? 0x80000000u : 0u;
    uint32_t a = v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
    int e = 31;
    while (!(a & 0x80000000u)) { a <<= 1; --e; }
    /* a: 1.31 normalised; keep 24 bits. */
    uint32_t m = a >> 8;
    const uint32_t rest = a & 0xffu;
    if (rest > 0x80u || (rest == 0x80u && (m & 1u))) ++m;
    if (m == 0x01000000u) { m >>= 1; ++e; }
    r.u = sign | ((uint32_t)(e - (int)shift + 127) << 23) | (m & 0x007fffffu);
    return r.f;
}

#if ENG_BAL_PREWARM
/* Prewarm (claude/balance): every bank and slot's native tables and templates at load, for the
 * player's LOD (slot 0) and the field's 3D rival LOD (fzx_field_k bits 8..15; the other slots),
 * so the first race frame of each bank does not build them (8 to 9 ms each in Ymir). */
static void prewarm(void);
#define PREWARM() prewarm()
#else
#define PREWARM() ((void)0)
#endif
#if ENG_SCENES
/* The pack loaded: file, sizes, checks and body colour. */
typedef struct {
    const char *file;
    uint32_t pack_bytes, pack_hash, meta_bytes, meta_hash, texel_bytes, palette_bytes, gouraud_bytes;
    uint8_t body[3];
} pack_id_t;
static pack_id_t loaded;
static uint32_t load_pack(const pack_id_t *id, const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch,
                          uint32_t scratch_bytes) {
    fzx_ab_report[0] = 0;
    if (id->pack_bytes > sizeof(meta) || id->meta_bytes + id->texel_bytes != id->pack_bytes || (id->meta_bytes & 3u) ||
        id->texel_bytes > TEXEL_BYTES || id->palette_bytes + id->gouraud_bytes > SHADE_BYTES) { fzx_ab_report[0] = 3; return 0; }
    loaded = *id;
    if (ceiling < REGION_BYTES) { fzx_ab_report[0] = 5; return 0; }
    const uint32_t floor = (ceiling - REGION_BYTES) & ~31u;
    const uintptr_t base = (uintptr_t)parts->texture_base;
    if (base & 31u) { fzx_ab_report[0] = 5; return 0; }
    pixels = (uint8_t *)(base + floor);
    for (unsigned b = 0; b < 2u; ++b)
        for (unsigned s = 0; s < FZX_CLOSEUP_CARS; ++s) shades[b][s] = pixels + TEXEL_BYTES + (b * FZX_CLOSEUP_CARS + s) * SHADE_BYTES;
    if (!fzx_texture_cd_load(id->file, id->pack_bytes, id->pack_hash, meta, scratch, scratch_bytes)) { fzx_ab_report[0] = 2; return 0; }
    if (!fzx_vehicle_pack_open(&pack, meta, id->meta_bytes, id->texel_bytes, id->meta_hash, false)) { fzx_ab_report[0] = 3; return 0; }
    {
        const uint32_t *src = meta + id->meta_bytes / 4u;
        volatile uint32_t *dst = (volatile uint32_t *)(void *)pixels;
        for (uint32_t i = 0; i < id->texel_bytes / 4u; ++i) dst[i] = src[i];
        for (uint32_t i = 0; i < id->texel_bytes / 4u; ++i)
            if (dst[i] != src[i]) { fzx_ab_report[0] = 4; return 0; }
    }
    memset(shades[0][0], 0, 2u * FZX_CLOSEUP_CARS * SHADE_BYTES);
    memset(&work, 0, sizeof(work));
    memset(tables, 0, sizeof(tables));
#if defined(ENG_FX) && ENG_FX
    pal_gen = 0; memset(table_gen, 0, sizeof(table_gen));
#endif
    {
        vdp1_cmdt_t t, s;
        memset(&t, 0, sizeof(t)); memset(&s, 0, sizeof(s));
        const vdp1_cmdt_draw_mode_t texture = {.end_code_disable = 1, .trans_pixel_disable = 1, .user_clipping_mode = 2,
                                               .color_mode = VDP1_CMDT_CM_CLUT_16, .cc_mode = VDP1_CMDT_CC_GOURAUD};
        const vdp1_cmdt_draw_mode_t solid = {.end_code_disable = 1, .trans_pixel_disable = 1, .user_clipping_mode = 2,
                                             .color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_REPLACE};
        vdp1_cmdt_distorted_sprite_set(&t); vdp1_cmdt_draw_mode_set(&t, texture);
        vdp1_cmdt_polygon_set(&s); vdp1_cmdt_draw_mode_set(&s, solid);
        texture_ctrl = t.cmd_ctrl; texture_pmod = t.cmd_pmod; solid_ctrl = s.cmd_ctrl; solid_pmod = s.cmd_pmod;
    }
    memset(&state, 0, sizeof(state));
    memset(&camera, 0, sizeof(camera));
    for (unsigned i = 0; i < 3u; ++i) {
        state.model_scale[i] = fixed_to_float(13421773, 27); /* 0x3DCCCCCD, binary32 0.1 */
        state.ambient[i] = 100u; state.light_rgb[i] = 255u; state.light_dir[i] = 69;
        state.fog[i] = pack.data[51u + i];
    }
    state.body[0] = id->body[0]; state.body[1] = id->body[1]; state.body[2] = id->body[2];
    state.source_lod = 1; state.normal_body_visible = 1;
    fzx_ab_report[9] = (uint32_t)((uintptr_t)pixels & 0x7ffffu);
    fzx_ab_report[10] = (uint32_t)((uintptr_t)shades[0][0] & 0x7ffffu);
    fzx_ab_report[11] = (uint32_t)((uintptr_t)shades[1][0] & 0x7ffffu);
    fzx_ab_report[12] = (uint32_t)(((uintptr_t)shades[1][FZX_CLOSEUP_CARS - 1u] + SHADE_BYTES) & 0x7ffffu);
    PREWARM();
    fzx_ab_report[0] = 1;
    return floor;
}
#ifndef FZX_P3_BASELINE
static const fzx_mach3d_identity_t *family_pack(uint32_t family) {
#if FZX_GUEST_MASK
    if ((family == 33u || family == 34u) && ((FZX_GUEST_MASK >> family) & 1ull))
        return &guest_machine[family - 33u];
#endif
    return family < FZX_MACH3D_FAMILIES ? &fzx_mach3d_identity[family] : NULL;
}
bool fzx_closeup_has_family(uint32_t family) {
    const fzx_mach3d_identity_t *pack = family_pack(family);
    return pack && pack->present && pack->pack_bytes;
#else
bool fzx_closeup_has_family(uint32_t family) {
    return family < FZX_MACH3D_FAMILIES && fzx_mach3d_identity[family].present && fzx_mach3d_identity[family].pack_bytes;
#endif
}
uint32_t fzx_closeup_load_family(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes,
                                 uint32_t family) {
    static char file[13] = "MACH3D00.PAK";
    if (!fzx_closeup_has_family(family)) { fzx_ab_report[0] = 6; return 0; } /* no pack for this family */
#ifndef FZX_P3_BASELINE
    const fzx_mach3d_identity_t *e = family_pack(family);
#else
    const fzx_mach3d_identity_t *e = &fzx_mach3d_identity[family];
#endif
    file[6] = (char)('0' + family / 10u); file[7] = (char)('0' + family % 10u);
    const pack_id_t id = { file, e->pack_bytes, e->pack_fnv, e->metadata_bytes, e->metadata_fnv, e->texel_bytes,
                           e->palette_bytes, e->gouraud_bytes, { e->body[0], e->body[1], e->body[2] } };
    return load_pack(&id, parts, ceiling, scratch, scratch_bytes);
}
/* A retry: per-race draw state back to its start; the pack stays. */
void fzx_closeup_reset(void) {
    if (fzx_ab_report[0] != 1) return;
    memset(shades[0][0], 0, 2u * FZX_CLOSEUP_CARS * SHADE_BYTES);
    memset(&work, 0, sizeof(work));
    memset(tables, 0, sizeof(tables));
#if defined(ENG_FX) && ENG_FX
    pal_gen = 0; memset(table_gen, 0, sizeof(table_gen));
#endif
    PREWARM();
    for (unsigned i = 1; i < 16u; ++i) if (i < 9u || i > 12u) fzx_ab_report[i] = 0;
}
#else
uint32_t fzx_closeup_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes) {
    fzx_ab_report[0] = 0;
    if (ceiling < REGION_BYTES) { fzx_ab_report[0] = 5; return 0; }
    const uint32_t floor = (ceiling - REGION_BYTES) & ~31u;
    const uintptr_t base = (uintptr_t)parts->texture_base;
    if (base & 31u) { fzx_ab_report[0] = 5; return 0; }
    pixels = (uint8_t *)(base + floor);
    for (unsigned b = 0; b < 2u; ++b)
        for (unsigned s = 0; s < FZX_CLOSEUP_CARS; ++s) shades[b][s] = pixels + TEXEL_BYTES + (b * FZX_CLOSEUP_CARS + s) * SHADE_BYTES;
    /* One CD file (MACHAB.PAK: metadata, then texels) to work RAM; the texels then to VRAM. */
    if (!fzx_texture_cd_load("MACHAB.PAK", FZX_CLOSEUP_PACK_BYTES, FZX_CLOSEUP_PACK_HASH, meta,
                             scratch, scratch_bytes)) { fzx_ab_report[0] = 2; return 0; }
    if (!fzx_vehicle_pack_open(&pack, meta, FZX_MACHINE_REBAKE_METADATA_BYTES, FZX_MACHINE_REBAKE_TEXTURE_BYTES,
                               FZX_MACHINE_REBAKE_METADATA_HASH, false)) { fzx_ab_report[0] = 3; return 0; }
    {
        const uint32_t *src = meta + FZX_MACHINE_REBAKE_METADATA_BYTES / 4u;
        volatile uint32_t *dst = (volatile uint32_t *)(void *)pixels;
        for (uint32_t i = 0; i < FZX_MACHINE_REBAKE_TEXTURE_BYTES / 4u; ++i) dst[i] = src[i];
        for (uint32_t i = 0; i < FZX_MACHINE_REBAKE_TEXTURE_BYTES / 4u; ++i)
            if (dst[i] != src[i]) { fzx_ab_report[0] = 4; return 0; }
    }
    memset(shades[0][0], 0, 2u * FZX_CLOSEUP_CARS * SHADE_BYTES);
    memset(&work, 0, sizeof(work));
    memset(tables, 0, sizeof(tables));
#if defined(ENG_FX) && ENG_FX
    pal_gen = 0; memset(table_gen, 0, sizeof(table_gen));
#endif
    {
        vdp1_cmdt_t t, s;
        memset(&t, 0, sizeof(t)); memset(&s, 0, sizeof(s));
        const vdp1_cmdt_draw_mode_t texture = {.end_code_disable = 1, .trans_pixel_disable = 1, .user_clipping_mode = 2,
                                               .color_mode = VDP1_CMDT_CM_CLUT_16, .cc_mode = VDP1_CMDT_CC_GOURAUD};
        const vdp1_cmdt_draw_mode_t solid = {.end_code_disable = 1, .trans_pixel_disable = 1, .user_clipping_mode = 2,
                                             .color_mode = VDP1_CMDT_CM_RGB_32768, .cc_mode = VDP1_CMDT_CC_REPLACE};
        vdp1_cmdt_distorted_sprite_set(&t); vdp1_cmdt_draw_mode_set(&t, texture);
        vdp1_cmdt_polygon_set(&s); vdp1_cmdt_draw_mode_set(&s, solid);
        texture_ctrl = t.cmd_ctrl; texture_pmod = t.cmd_pmod; solid_ctrl = s.cmd_ctrl; solid_pmod = s.cmd_pmod;
    }
    /* The fixed part of the draw state: stock machine scale 0.1, LOD 1 (the player at
     * chase distance), the body colour of the sprite sets' skin, the source's outside
     * lighting and the pack's outside fog (its bytes 51..53, as the legacy draw). */
    memset(&state, 0, sizeof(state));
    memset(&camera, 0, sizeof(camera));
    for (unsigned i = 0; i < 3u; ++i) {
        state.model_scale[i] = fixed_to_float(13421773, 27); /* 0x3DCCCCCD, binary32 0.1 */
        state.ambient[i] = 100u; state.light_rgb[i] = 255u; state.light_dir[i] = 69;
        state.fog[i] = pack.data[51u + i];
    }
    state.body[0] = FZX_CLOSEUP_BODY_R; state.body[1] = FZX_CLOSEUP_BODY_G; state.body[2] = FZX_CLOSEUP_BODY_B;
    state.source_lod = 1; state.normal_body_visible = 1;
    fzx_ab_report[9] = (uint32_t)((uintptr_t)pixels & 0x7ffffu);
    fzx_ab_report[10] = (uint32_t)((uintptr_t)shades[0][0] & 0x7ffffu);
    fzx_ab_report[11] = (uint32_t)((uintptr_t)shades[1][0] & 0x7ffffu);
    fzx_ab_report[12] = (uint32_t)(((uintptr_t)shades[1][FZX_CLOSEUP_CARS - 1u] + SHADE_BYTES) & 0x7ffffu);
    PREWARM();
    fzx_ab_report[0] = 1;
    return floor;
}
#endif

/* The draw for a pose already in `state` (model_pos, model_basis), into a slot's tables. */
static unsigned draw_state(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                           const fzx_engine_cam_t *cam, uint32_t bank, uint32_t slot, uint16_t t0) {
    /* Source reflection: (world light - camera forward) * 16383, rounded; 0.57735
     * is the source's outside light component. Camera forward is row 2, Q30. */
    for (unsigned i = 0; i < 3u; ++i) {
        camera.eye[i] = fixed_to_float(cam->eye[i], 16);
        const int64_t r = ((int64_t)619924842 - (int64_t)view->rotation[2][i]) * 16383; /* 0.57735 Q30 */
        state.reflection[i] = (short)((r + (r >= 0 ? (1 << 29) : -(1 << 29))) / (1 << 30));
    }
    work.tables = &tables[bank][slot];
    const fzx_vehicle_target_t target = {
        .commands = (uint32_t (*)[8])(void *)commands, .capacity = capacity, .pixels = pixels, .shades = shades[bank][slot],
        .palette_bytes = PALETTE_NOW, .gouraud_bytes = GOURAUD_NOW,
        .texture_ctrl = texture_ctrl, .texture_pmod = texture_pmod, .solid_ctrl = solid_ctrl, .solid_pmod = solid_pmod};
    uint32_t drawn = 0;
    const bool ok = fzx_vehicle_draw_native(&pack, view, &camera, &state, &work, &target, &drawn, &fzx_ab_vehicle_stats);
    if (!ok) { ++fzx_ab_report[5]; fzx_ab_report[6] = fzx_ab_vehicle_stats.fault; drawn = 0; }
#if defined(ENG_FX) && ENG_FX
    {
        bool dirty = false;
        for (unsigned i = 0; i < pack.palettes; ++i) dirty |= work.palette_dirty[i] != 0;
        if (dirty) {
            ++fzx_ab_report[15];
            if (!pal_gen) memcpy(base_pal, work.palette, sizeof(base_pal[0]) * pack.palettes); /* the load's */
            ++pal_gen;
        }
        /* The commands use this table from the next draw of the bank: refresh it now if stale. */
        const uint32_t want = draw_player ? pal_gen : 1u;
        if (bank < 2u && slot < FZX_CLOSEUP_CARS && table_gen[bank][slot] != want) {
            const uint16_t (*src)[16] = draw_player ? (const uint16_t (*)[16])work.palette : (const uint16_t (*)[16])base_pal;
            for (unsigned i = 0; i < pack.palettes; ++i) memcpy(shades[bank][slot] + i * 32u, src[i], 32u);
            table_gen[bank][slot] = want;
        }
    }
#else
    /* Palettes change only with the body colour (once here): every table gets them. */
    for (unsigned i = 0; i < pack.palettes; ++i)
        if (work.palette_dirty[i]) {
            for (unsigned b = 0; b < 2u; ++b)
                for (unsigned s = 0; s < FZX_CLOSEUP_CARS; ++s) memcpy(shades[b][s] + i * 32u, work.palette[i], 32u);
            ++fzx_ab_report[15];
        }
#endif
    fzx_ab_report[1] = drawn;
    if (drawn > fzx_ab_report[2]) fzx_ab_report[2] = drawn;
    fzx_ab_report[3] += drawn;
    if (drawn) ++fzx_ab_report[4];
    fzx_ab_report[7] = fzx_ab_vehicle_stats.culled;
    fzx_ab_report[8] += fzx_ab_vehicle_stats.clipped;
    const uint32_t t = (uint16_t)(cpu_frt_count_get() - t0);
    fzx_ab_report[13] = t;
    if (t > fzx_ab_report[14]) fzx_ab_report[14] = t;
    return drawn;
}

unsigned fzx_closeup_draw(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                          const fzx_engine_cam_t *cam, uint32_t bank) {
    if (fzx_ab_report[0] != 1 || bank > 1u) return 0;
    const uint16_t t0 = cpu_frt_count_get();
    const sh2z_vd_body_t *b = fzx_drive_body();
    for (unsigned i = 0; i < 3u; ++i) {
        state.model_pos[i] = fixed_to_float(b->pos[i], 16);
        for (unsigned j = 0; j < 3u; ++j) state.model_basis[i][j] = fixed_to_float(b->body[i][j], 30);
    }
#if defined(ENG_FX) && ENG_FX
    draw_player = true;
    const unsigned n = draw_state(commands, capacity, view, cam, bank, 0, t0);
    draw_player = false;
    return n;
#else
    return draw_state(commands, capacity, view, cam, bank, 0, t0);
#endif
}

#if defined(ENG_FX) && ENG_FX
/* Race effects (engine_fx.c): the body colour the next draw shades with (the white flash and the
 * low-energy flicker); the palettes follow it as on a change of machine. */
void fzx_closeup_body_get(uint8_t rgb[3]) { rgb[0] = state.body[0]; rgb[1] = state.body[1]; rgb[2] = state.body[2]; }
void fzx_closeup_body_set(const uint8_t rgb[3]) { state.body[0] = rgb[0]; state.body[1] = rgb[1]; state.body[2] = rgb[2]; }
#endif

unsigned fzx_closeup_draw_pose(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                               const fzx_engine_cam_t *cam, uint32_t bank, uint32_t slot,
                               const int32_t pos[3], const int16_t basis[3][3]) {
    if (fzx_ab_report[0] != 1 || bank > 1u || slot >= FZX_CLOSEUP_CARS) return 0;
    const uint16_t t0 = cpu_frt_count_get();
    for (unsigned i = 0; i < 3u; ++i) {
        state.model_pos[i] = fixed_to_float(pos[i], 16);
        for (unsigned j = 0; j < 3u; ++j) state.model_basis[i][j] = fixed_to_float(basis[i][j], 14);
    }
    /* The source's LOD numbers: 1..3 draw the pack's LOD 1, 4 its LOD 4, 5 its LOD 5. */
    state.source_lod = (int)(fzx_closeup_lod >= 1u && fzx_closeup_lod <= 5u ? fzx_closeup_lod : 1u);
    const unsigned n = draw_state(commands, capacity, view, cam, bank, slot, t0);
    state.source_lod = 1;
    return n;
}

#if ENG_BAL_PREWARM
static void prewarm(void) {
    static vdp1_cmdt_t scratch_cmd[1];
#if defined(E1_FIELD) && E1_FIELD
    extern volatile uint32_t fzx_field_k;
    const uint32_t k_lod = (fzx_field_k >> 8) & 0xffu;
    const uint32_t rival = k_lod >= 1u && k_lod <= 5u ? k_lod : 1u; /* as fzx_closeup_draw_pose */
#else
    const uint32_t rival = 1u;
#endif
    for (unsigned b = 0; b < 2u; ++b)
        for (unsigned s = 0; s < FZX_CLOSEUP_CARS; ++s) {
            work.tables = &tables[b][s];
            const fzx_vehicle_target_t target = {
                .commands = (uint32_t (*)[8])(void *)scratch_cmd, .capacity = 1u, .pixels = pixels, .shades = shades[b][s],
                .palette_bytes = PALETTE_NOW, .gouraud_bytes = GOURAUD_NOW,
                .texture_ctrl = texture_ctrl, .texture_pmod = texture_pmod, .solid_ctrl = solid_ctrl, .solid_pmod = solid_pmod};
            if (!fzx_vehicle_native_prewarm(&pack, s ? (int)rival : 1, &work, &target)) ++fzx_ab_report[5];
        }
}
#endif
