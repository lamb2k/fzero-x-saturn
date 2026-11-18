#ifndef FZX_ROAD_STREAM_COMMANDS_H
#define FZX_ROAD_STREAM_COMMANDS_H
#include <yaul.h>
#include "fzx_road_stream_draw.h"

/* Experimental C3 native packet adapter. Caller owns the user clip command,
 * END/submission and the wait for previous VDP1 completion. Texture bytes
 * include the complete admitted bank; pixel/palette offsets remain relative
 * to its start. Textures and shades must be disjoint aligned VDP1 VRAM spans.
 * Source packets, commands, count and shades must be disjoint writable/input
 * storage as appropriate. No per-packet callbacks or allocations.
 * Count is zero on failure; discard any partial commands/shades then.
 * This adapter validates addresses/layout, not the source pack receipt. */
bool fzx_road_stream_commands(vdp1_cmdt_t *commands,uint32_t capacity,
    const fzx_road_stream_command_t *packets,uint32_t packet_count,
    const void *pixels,uint32_t texture_bytes,void *shades,uint32_t shade_bytes,
    uint32_t *count);
/* Direct-emit target over the same spans and templates as the adapter above;
 * fzx_road_stream_draw_vdp1 then writes the bytes the adapter would write. */
bool fzx_road_stream_emit_setup(fzx_road_stream_emit_t *,vdp1_cmdt_t *commands,uint32_t capacity,
    const void *pixels,uint32_t texture_bytes,void *shades,uint32_t shade_bytes);
#endif
