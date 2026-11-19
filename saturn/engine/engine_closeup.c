/* Engine E1 close-up A/B, arm A: the player as the 3D model (see engine_closeup.h). */
#include <string.h>
#include <stddef.h>
#include "engine_closeup.h"
#include "fzx_vehicle.h"
#include "machine_rebake_identity.h"
#include "closeup_identity.h"
#include "texture_cd.h"
#include "sh2zam/vehicle_dynamics_experimental.h"

/* engine_drive.c: the player's physics body (E1_CLOSEUP=1 only). */
const sh2z_vd_body_t *fzx_drive_body(void);

volatile uint32_t fzx_ab_report[16];
fzx_vehicle_stats_t fzx_ab_vehicle_stats;

#define ALIGN32(x) (((x) + 31u) & ~31u)
enum {
    TEXEL_BYTES = ALIGN32(FZX_MACHINE_REBAKE_TEXTURE_BYTES),
    SHADE_BYTES = ALIGN32(FZX_MACHINE_REBAKE_PALETTE_BYTES + FZX_MACHINE_REBAKE_GOURAUD_BYTES),
    REGION_BYTES = TEXEL_BYTES + 2u * SHADE_BYTES
};

_Static_assert(FZX_CLOSEUP_META_BYTES == FZX_MACHINE_REBAKE_METADATA_BYTES &&
               FZX_CLOSEUP_TEXEL_BYTES == FZX_MACHINE_REBAKE_TEXTURE_BYTES &&
               FZX_CLOSEUP_PACK_BYTES == FZX_CLOSEUP_META_BYTES + FZX_CLOSEUP_TEXEL_BYTES &&
               (FZX_MACHINE_REBAKE_METADATA_BYTES & 3u) == 0u, "MACHAB.PAK is MACHRLD.PAK then MACHRTX.PAK");
/* The whole CD pack; the metadata stays here, the texel half is copied to VRAM at load. */
static uint32_t meta[FZX_CLOSEUP_PACK_BYTES / 4u] __attribute__((aligned(32)));
static fzx_vehicle_pack_t pack;
static fzx_vehicle_work_t work __attribute__((aligned(32)));
static fzx_vehicle_native_tables_t tables[2] __attribute__((aligned(32))); /* one per VDP1 bank */
static uint8_t *pixels, *shades[2];
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

uint32_t fzx_closeup_load(const vdp1_vram_partitions_t *parts, uint32_t ceiling, void *scratch, uint32_t scratch_bytes) {
    fzx_ab_report[0] = 0;
    if (ceiling < REGION_BYTES) { fzx_ab_report[0] = 5; return 0; }
    const uint32_t floor = (ceiling - REGION_BYTES) & ~31u;
    const uintptr_t base = (uintptr_t)parts->texture_base;
    if (base & 31u) { fzx_ab_report[0] = 5; return 0; }
    pixels = (uint8_t *)(base + floor);
    shades[0] = pixels + TEXEL_BYTES;
    shades[1] = shades[0] + SHADE_BYTES;
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
    memset(shades[0], 0, 2u * SHADE_BYTES);
    memset(&work, 0, sizeof(work));
    memset(tables, 0, sizeof(tables));
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
    fzx_ab_report[10] = (uint32_t)((uintptr_t)shades[0] & 0x7ffffu);
    fzx_ab_report[11] = (uint32_t)((uintptr_t)shades[1] & 0x7ffffu);
    fzx_ab_report[12] = (uint32_t)(((uintptr_t)shades[1] + SHADE_BYTES) & 0x7ffffu);
    fzx_ab_report[0] = 1;
    return floor;
}

unsigned fzx_closeup_draw(vdp1_cmdt_t *commands, unsigned capacity, const fzx_view_t *view,
                          const fzx_engine_cam_t *cam, uint32_t bank) {
    if (fzx_ab_report[0] != 1 || bank > 1u) return 0;
    const uint16_t t0 = cpu_frt_count_get();
    const sh2z_vd_body_t *b = fzx_drive_body();
    /* Source reflection: (world light - camera forward) * 16383, rounded; 0.57735
     * is the source's outside light component. Camera forward is row 2, Q30. */
    for (unsigned i = 0; i < 3u; ++i) {
        state.model_pos[i] = fixed_to_float(b->pos[i], 16);
        for (unsigned j = 0; j < 3u; ++j) state.model_basis[i][j] = fixed_to_float(b->body[i][j], 30);
        camera.eye[i] = fixed_to_float(cam->eye[i], 16);
        const int64_t r = ((int64_t)619924842 - (int64_t)view->rotation[2][i]) * 16383; /* 0.57735 Q30 */
        state.reflection[i] = (short)((r + (r >= 0 ? (1 << 29) : -(1 << 29))) / (1 << 30));
    }
    work.tables = &tables[bank];
    const fzx_vehicle_target_t target = {
        .commands = (uint32_t (*)[8])(void *)commands, .capacity = capacity, .pixels = pixels, .shades = shades[bank],
        .palette_bytes = FZX_MACHINE_REBAKE_PALETTE_BYTES, .gouraud_bytes = FZX_MACHINE_REBAKE_GOURAUD_BYTES,
        .texture_ctrl = texture_ctrl, .texture_pmod = texture_pmod, .solid_ctrl = solid_ctrl, .solid_pmod = solid_pmod};
    uint32_t drawn = 0;
    const bool ok = fzx_vehicle_draw_native(&pack, view, &camera, &state, &work, &target, &drawn, &fzx_ab_vehicle_stats);
    if (!ok) { ++fzx_ab_report[5]; fzx_ab_report[6] = fzx_ab_vehicle_stats.fault; drawn = 0; }
    /* Palettes change only with the body colour (once here): both banks get them. */
    for (unsigned i = 0; i < pack.palettes; ++i)
        if (work.palette_dirty[i]) {
            memcpy(shades[0] + i * 32u, work.palette[i], 32u);
            memcpy(shades[1] + i * 32u, work.palette[i], 32u);
            ++fzx_ab_report[15];
        }
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
