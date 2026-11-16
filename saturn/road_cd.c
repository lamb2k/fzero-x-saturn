#include "road_cd.h"

/* Yaul 0.3.1's CPU transfer uses 16-bit reads at 0x25890000. The reference
 * Yaba build does not implement that alias. The sector-data port used by
 * Cs2ReadLong is 0x25818000; use aligned longword CPU transfers there.
 * Work RAM receives the sector first. No DMA has a cartridge endpoint. */
#define ROAD_CD_DATA ((volatile const uint32_t *)0x25818000u)
#define ROAD_CD_HIRQ ((volatile const uint16_t *)0x25890008u)
#define ROAD_CD_TIMEOUT_TICKS 3000000u /* >3 seconds at FRT master/32 */

static bool expired(uint16_t *last,uint32_t *elapsed) {
    uint16_t now=cpu_frt_count_get();
    *elapsed+=(uint16_t)(now-*last);*last=now;
    return *elapsed>ROAD_CD_TIMEOUT_TICKS;
}
int fzx_road_sector_read(fad_t fad,sector_buffer_t *out) {
    int rc;
    uint16_t last=cpu_frt_count_get();uint32_t elapsed=0;
    if(fad<150 || !out || ((uintptr_t)out&3u)) return -1;
    if((rc=cd_block_cmd_sector_length_set(SECTOR_LENGTH_2048))) return rc;
    if((rc=cd_block_cmd_selector_reset(0,0))) return rc;
    if((rc=cd_block_cmd_cd_dev_connection_set(0))) return rc;
    if((rc=cd_block_cmd_disk_play(0,fad,1))) return rc;
    for(;;) {
        rc=cd_block_cmd_sector_number_get(0);
        if(rc<0) return rc;
        if(rc>0) break;
        if(expired(&last,&elapsed)) return -2;
    }
    if((rc=cd_block_cmd_sector_data_get_delete(0,0,1))) return rc;
    while(!(*ROAD_CD_HIRQ&0x0002u))
        if(expired(&last,&elapsed)) return -3;
    for(unsigned i=0;i<CDFS_SECTOR_SIZE/4u;++i) out->buffer32[i]=*ROAD_CD_DATA;
    return cd_block_cmd_data_transfer_end();
}
