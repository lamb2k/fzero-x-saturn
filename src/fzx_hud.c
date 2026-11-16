#include "fzx_hud.h"
#include <stddef.h>

static const uint16_t sizes[FZX_HUD_TEXTURES][2] = {
    {24,16}, {72,16}, {8,72}, {12,160}, {20,16}, {8,224}, {16,12}
};

static void sprite(fzx_hud_list_t *out, const fzx_hud_atlas_t *atlas,
                   unsigned texture, unsigned row, unsigned height, int x, int y)
{
    const fzx_hud_texture_t *t = &atlas->textures[texture];
    out->draws[out->count++] = (fzx_hud_draw_t){
        t->offset + row * t->stride * 2u, (int16_t)x, (int16_t)y,
        t->stride, (uint16_t)height, 0
    };
}

bool fzx_hud_build(const fzx_hud_atlas_t *atlas, const fzx_hud_snapshot_t *state,
                   fzx_hud_list_t *output)
{
    if (!atlas || !state || !output || atlas->bytes > 0x6bfe0u ||
        state->speed_kmh > 9999u || state->total_laps < 1u ||
        state->total_laps > 5u || state->lap < 1u) return false;
    uint32_t end = 0;
    for (unsigned i = 0; i < FZX_HUD_TEXTURES; ++i) {
        const fzx_hud_texture_t *t = &atlas->textures[i];
        if (t->width != sizes[i][0] || t->height != sizes[i][1] ||
            t->stride != ((t->width + 7u) & ~7u) || (t->offset & 7u) ||
            t->offset < end || t->offset > atlas->bytes) return false;
        const uint32_t bytes = (uint32_t)t->stride * t->height * 2u;
        if (bytes > atlas->bytes - t->offset) return false;
        end = t->offset + bytes;
    }
    fzx_hud_list_t out = {0};
    sprite(&out, atlas, FZX_HUD_TIME, 0, 16, 198, 38);
    const uint32_t ms = state->time_ms < 3599999u ? state->time_ms : 3599999u;
    const uint32_t minutes = ms / 60000u, seconds = ms / 1000u % 60u;
    const uint32_t hundredths = ms / 10u % 100u;
    const unsigned digits[8] = {minutes/10u, minutes%10u, 10u, seconds/10u,
                                seconds%10u, 11u, hundredths/10u, hundredths%10u};
    for (unsigned i = 0; i < 8; ++i)
        sprite(&out, atlas, FZX_HUD_TIMER, digits[i]*16u, 16, 230+(int)i*8, 38);
    if (!state->finished) {
        const unsigned lap = state->lap < state->total_laps ? state->lap : state->total_laps;
        sprite(&out, atlas, FZX_HUD_LAP, 0, 12, 66, 20);
        sprite(&out, atlas, FZX_HUD_LAP_SYMBOLS, lap*12u, 12, 66, 34);
        sprite(&out, atlas, FZX_HUD_LAP_SYMBOLS, 0, 12, 74, 34);
        sprite(&out, atlas, FZX_HUD_LAP_SYMBOLS, state->total_laps*12u, 12, 82, 34);
    }
    /* Source black backing covers the middle two digit columns. */
    out.draws[out.count++] = (fzx_hud_draw_t){0,238,204,24,16,0x8000};
    uint32_t remainder = state->speed_kmh, divisor = 1000;
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned digit = remainder / divisor;
        if (i == 3 || digit != 0 || state->speed_kmh != remainder)
            sprite(&out, atlas, FZX_HUD_SPEED, digit*16u, 16, 226+(int)i*12, 204);
        remainder %= divisor;
        divisor /= 10u;
    }
    sprite(&out, atlas, FZX_HUD_KMH, 0, 16, 274, 204);
    *output = out;
    return true;
}
