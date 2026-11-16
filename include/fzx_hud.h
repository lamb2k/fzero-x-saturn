/* Native single-player HUD drawing, adapted from pinned CC0 hud.c.
 * This consumes a presentation snapshot; it never updates race state. */
#ifndef FZX_NATIVE_HUD_H
#define FZX_NATIVE_HUD_H
#include <stdbool.h>
#include <stdint.h>

enum { FZX_HUD_TIME, FZX_HUD_ENERGY, FZX_HUD_LAP_SYMBOLS, FZX_HUD_SPEED,
       FZX_HUD_KMH, FZX_HUD_TIMER, FZX_HUD_LAP, FZX_HUD_TEXTURES,
       FZX_HUD_MAX_DRAWS = 64 };
typedef struct {
    uint32_t offset; /* bytes in VDP1 atlas, always aligned to eight */
    uint16_t width, stride, height;
} fzx_hud_texture_t;
typedef struct {
    uint32_t bytes;
    fzx_hud_texture_t textures[FZX_HUD_TEXTURES];
} fzx_hud_atlas_t;
typedef struct {
    uint32_t time_ms, speed_kmh;
    uint16_t lap, total_laps;
    bool finished;
    uint16_t energy_width; /* source-rounded energy/maxEnergy * 68, range 0..68 */
    bool can_boost;
} fzx_hud_snapshot_t;
typedef struct {
    uint32_t offset;
    int16_t x, y;
    uint16_t width, height, color; /* color!=0: solid rectangle */
} fzx_hud_draw_t;
typedef struct {
    uint32_t count;
    fzx_hud_draw_t draws[FZX_HUD_MAX_DRAWS];
} fzx_hud_list_t;

/* Timer, speed, energy and lap counter. Flash timers, countdown,
 * interval/results and mode-dependent HUD selection remain source-integration work.
 * Coordinates retain the source 320x240 layout; these components fit 320x224.
 * Invalid atlas/state is rejected without changing the caller's output. */
bool fzx_hud_build(const fzx_hud_atlas_t *atlas, const fzx_hud_snapshot_t *state,
                   fzx_hud_list_t *output);
#endif
