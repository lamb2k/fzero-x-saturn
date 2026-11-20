#ifndef FZX_ROAD_CD_H
#define FZX_ROAD_CD_H
#include <yaul.h>
/* Bounded boot-time PIO read to work RAM, never directly to cartridge RAM. */
int fzx_road_sector_read(fad_t fad,sector_buffer_t *out);
#if FZX_CD_STREAM
/* Scenes image: sequential sectors stream from one play command (no seek per sector).
 * [0] sectors, [1] play commands, [2] fields spent inside reads, [3] yields. */
extern volatile uint32_t fzx_cd_stats[4];
/* Called while a read waits and after each sector: the loading screen's turn. */
extern void (*volatile fzx_cd_yield)(void);
/* Field clock for the read-time counter (the engine's VBlank count). */
extern uint32_t (*volatile fzx_cd_clock)(void);
/* The CD block was re-initialised or the caller abandoned a stream. */
void fzx_road_stream_invalidate(void);
#endif
#endif
