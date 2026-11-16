#ifndef FZX_ROAD_CD_H
#define FZX_ROAD_CD_H
#include <yaul.h>
/* Bounded boot-time PIO read to work RAM, never directly to cartridge RAM. */
int fzx_road_sector_read(fad_t fad,sector_buffer_t *out);
#endif
