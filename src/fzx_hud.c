#include "fzx_hud.h"
#include <stddef.h>

static const uint16_t sizes[FZX_HUD_TEXTURES][2] = {
    {24,16}, {72,16}, {8,72}, {12,160}, {20,16}, {8,224}, {16,12}
};

void fzx_hud_clock_reset(fzx_hud_clock_t *clock)
{
    if (clock) *clock = (fzx_hud_clock_t){1, 0, 0, 5, true};
}

bool fzx_hud_clock_step(fzx_hud_clock_t *clock, const fzx_hud_clock_input_t *input)
{
    if (!clock || !input || input->lap > 4 ||
        clock->lap < 1u || clock->lap > 4u || clock->flash_ticks > 180u ||
        input->race_time_ms > 0x7ffffffau) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (input->lap_times_ms[i] > 0x7ffffffau) return false;
    fzx_hud_clock_t out = *clock;
    if ((int32_t)out.lap < input->lap) {
        out.lap = (uint32_t)input->lap;
        out.flash_ticks = 180;
    }
    if (out.flash_ticks == 180) {
        const int32_t slot = (input->lap+1)%3;
        if (slot < 0) return false; /* invalid source indexing, never wrap it */
        out.previous_lap_ms = input->lap_times_ms[slot];
    }
    if (out.flash_ticks > 0) {
        if (!input->paused) --out.flash_ticks;
        out.visible = out.flash_ticks%20u >= 5u;
        out.time_ms = out.previous_lap_ms + 5u;
        /* A 1->0 transition stays in this branch and is still blank. */
    } else {
        out.visible = true;
        out.time_ms = input->race_time_ms + 5u;
    }
    *clock = out;
    return true;
}

static void sprite(fzx_hud_list_t *out, const fzx_hud_atlas_t *atlas,
                   unsigned texture, unsigned row, unsigned height, int x, int y)
{
    const fzx_hud_texture_t *t = &atlas->textures[texture];
    out->draws[out->count++] = (fzx_hud_draw_t){
        t->offset + row * t->stride * 2u, (int16_t)x, (int16_t)y,
        t->stride, (uint16_t)height, 0
    };
}

static void fill(fzx_hud_list_t *out, int x, int y, unsigned width, uint16_t color)
{
    out->draws[out->count++] = (fzx_hud_draw_t){
        0, (int16_t)x, (int16_t)y, (uint16_t)width, 1, color
    };
}

#define RGB(r,g,b) (0x8000u | ((r)>>3) | (((g)>>3)<<5) | (((b)>>3)<<10))
static void energy(fzx_hud_list_t *out, const fzx_hud_snapshot_t *state)
{
    /* hud.c's RGBA5551 fill words. Row 3 alternates two red levels: do not
     * flatten it to a gradient. Draw its even color then overwrite odd pixels.
     * Five rows + 34 odd pixels is the bounded worst case (59 total HUD draws). */
    static const uint16_t colors[2][5] = {
        {RGB(155,0,0), RGB(220,0,0), RGB(255,0,0), RGB(220,0,0), RGB(155,0,0)},
        {RGB(0,255,0), RGB(0,255,20), RGB(0,195,35), RGB(0,160,50), RGB(0,135,70)}
    };
    if (state->finished || state->energy_width == 0) return;
    for (unsigned y = 0; y < 5; ++y)
        fill(out, 222, 28+(int)y, state->energy_width, colors[state->can_boost][y]);
    if (!state->can_boost)
        for (unsigned x = 1; x < state->energy_width; x += 2)
            fill(out, 222+(int)x, 31, 1, RGB(230,0,0));
}
#undef RGB

bool fzx_hud_build(const fzx_hud_atlas_t *atlas, const fzx_hud_snapshot_t *state,
                   fzx_hud_list_t *output)
{
    if (!atlas || !state || !output || atlas->bytes > 0x6bfe0u ||
        state->speed_kmh > 9999u || state->total_laps < 1u ||
        state->total_laps > 5u || state->lap < 1u || state->energy_width > 68u) return false;
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
    if (!state->hide_timer)
        for (unsigned i = 0; i < 8; ++i)
            sprite(&out, atlas, FZX_HUD_TIMER, digits[i]*16u, 16, 230+(int)i*8, 38);
    if (!state->finished) {
        sprite(&out, atlas, FZX_HUD_ENERGY, 0, 16, 220, 20);
        const unsigned lap = state->lap < state->total_laps ? state->lap : state->total_laps;
        sprite(&out, atlas, FZX_HUD_LAP, 0, 12, 66, 20);
        sprite(&out, atlas, FZX_HUD_LAP_SYMBOLS, lap*12u, 12, 66, 34);
        sprite(&out, atlas, FZX_HUD_LAP_SYMBOLS, 0, 12, 74, 34);
        sprite(&out, atlas, FZX_HUD_LAP_SYMBOLS, state->total_laps*12u, 12, 82, 34);
    }
    energy(&out, state);
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
