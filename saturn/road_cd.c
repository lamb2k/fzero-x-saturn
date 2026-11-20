#include "road_cd.h"
#if ENG_SND
#include "engine/engine_snd.h"
#endif

/* Yaul 0.3.1's CPU transfer uses 16-bit reads at 0x25890000. The reference
 * Yaba build does not implement that alias. The sector-data port used by
 * Cs2ReadLong is 0x25818000; use aligned longword CPU transfers there.
 * Work RAM receives the sector first. No DMA has a cartridge endpoint. */
#define ROAD_CD_DATA ((volatile const uint32_t *)0x25818000u)
#define ROAD_CD_HIRQ ((volatile uint16_t *)0x25890008u)
#define ROAD_CD_TIMEOUT_TICKS 3000000u /* >3 seconds at FRT master/32 */

static bool expired(uint16_t *last,uint32_t *elapsed) {
    uint16_t now=cpu_frt_count_get();
    *elapsed+=(uint16_t)(now-*last);*last=now;
    return *elapsed>ROAD_CD_TIMEOUT_TICKS;
}
#if FZX_CD_STREAM
/* One play command covers STREAM_SECTORS sectors; a read of the next sector in
 * order takes it from the CD block's buffer as it arrives (no new play, no seek).
 * Any other sector starts a new stream. */
#define STREAM_SECTORS 256u
volatile uint32_t fzx_cd_stats[4];
void (*volatile fzx_cd_yield)(void);
uint32_t (*volatile fzx_cd_clock)(void);
static fad_t stream_next,stream_end;
static bool streaming;
void fzx_road_stream_invalidate(void){streaming=false;}
static void yield(void){void (*h)(void)=fzx_cd_yield;if(h){++fzx_cd_stats[3];h();}}
#if (ENG_SCENES && !defined(FZX_P3_BASELINE)) || FZX_CD_TRACE >= 2
/* Seek checks, stale sectors dropped, last requested FAD and last stale FAD. */
volatile uint32_t fzx_cd_seek_stats[4];
/* Yaul has no wrapper for Get Sector Info (54h). Read the FAD of sector zero
 * in partition zero. Sector Number > 0 alone cannot distinguish an in-flight
 * sector from the previous Play Disc command at a discontinuous seek. */
static int stream_head_fad(fad_t *head, uint16_t *last, uint32_t *elapsed) {
    volatile uint16_t *const cr = (volatile uint16_t *)0x25890018u;
    while (!(*ROAD_CD_HIRQ & 1u))
        if (expired(last,elapsed)) return -4;
    *ROAD_CD_HIRQ = (uint16_t)~1u;
    cr[0]=0x5400u; cr[2]=0; cr[4]=0; cr[6]=0;
    while (!(*ROAD_CD_HIRQ & 1u))
        if (expired(last,elapsed)) return -4;
    const uint16_t status=cr[0], low=cr[2];
    (void)cr[4]; (void)cr[6];
    if ((status >> 8) == 0xffu) return -5;
    *head=((fad_t)(status & 0xffu) << 16) | low;
    return 0;
}
#endif
#if FZX_CD_TRACE
/* Diagnostic (FZX_CD_TRACE=1, 2 also asks the block for each sector's FAD): one record per sector read,
 * a ring of 1024: requested FAD | starting << 31, the block's FAD of its first buffered sector (2; else 0),
 * the first two words and words 64 ^ 65. [0] of fzx_cd_trace_n counts records. */
#define CD_TRACE_N 1024u
volatile uint32_t fzx_cd_trace[CD_TRACE_N][5];
volatile uint32_t fzx_cd_trace_n[2];
#endif
#if FZX_CD_STALE_CONTROL
volatile uint32_t fzx_cd_stale_control; /* stream starts that met a stale sector of the previous play */
#endif
static int stream_read(fad_t fad,sector_buffer_t *out) {
    int rc;
    uint16_t last=cpu_frt_count_get();uint32_t elapsed=0;
    const bool starting = !streaming || fad!=stream_next || fad>=stream_end;
    if(starting) {
        streaming=false;
        if((rc=cd_block_cmd_sector_length_set(SECTOR_LENGTH_2048))) return rc;
        if((rc=cd_block_cmd_selector_reset(0,0))) return rc;
        if((rc=cd_block_cmd_cd_dev_connection_set(0))) return rc;
#if FZX_CD_STALE_CONTROL
        /* Control (diagnostic): the race that delivered a wrong first sector. The selector reset freed the
         * partition, so a drive that had paused on a full buffer resumes the previous play and puts its next
         * sector in partition 0 before the new play seeks. Wait up to ~40 ms for such a sector here, so every
         * discontinuous start that follows a running stream meets one. */
        {
            uint16_t l=cpu_frt_count_get();uint32_t e=0;
            while((rc=cd_block_cmd_sector_number_get(0))==0 && e<36000u){const uint16_t now=cpu_frt_count_get();e+=(uint16_t)(now-l);l=now;}
            if(rc>0) ++fzx_cd_stale_control;
        }
#endif
        if((rc=cd_block_cmd_disk_play(0,fad,STREAM_SECTORS))) return rc;
        streaming=true;stream_end=fad+STREAM_SECTORS;++fzx_cd_stats[1];
    }
    for(;;) {
        rc=cd_block_cmd_sector_number_get(0);
        if(rc<0) {streaming=false;return rc;}
        if(rc>0) {
#if ENG_SCENES && !defined(FZX_P3_BASELINE)
            if (starting) {
                fad_t head;
                rc=stream_head_fad(&head,&last,&elapsed);
                if (rc) {streaming=false;return rc;}
                ++fzx_cd_seek_stats[0];
                if (head!=fad) {
                    rc=cd_block_cmd_sector_data_delete(0,0,1);
                    if (rc) {streaming=false;return rc;}
                    ++fzx_cd_seek_stats[1];
                    fzx_cd_seek_stats[2]=fad;
                    fzx_cd_seek_stats[3]=head;
                    yield();
                    if (expired(&last,&elapsed)) {streaming=false;return -6;}
                    continue;
                }
            }
#endif
            break;
        }
        yield();
        if(expired(&last,&elapsed)) {streaming=false;return -2;}
    }
#if FZX_CD_TRACE
    fad_t trace_head=0;
#if FZX_CD_TRACE >= 2
    if((rc=stream_head_fad(&trace_head,&last,&elapsed))) {streaming=false;return rc;}
#endif
#endif
    if((rc=cd_block_cmd_sector_data_get_delete(0,0,1))) {streaming=false;return rc;}
    while(!(*ROAD_CD_HIRQ&0x0002u))
        if(expired(&last,&elapsed)) {streaming=false;return -3;}
    for(unsigned i=0;i<CDFS_SECTOR_SIZE/4u;++i) out->buffer32[i]=*ROAD_CD_DATA;
    rc=cd_block_cmd_data_transfer_end();
    if(rc) {streaming=false;return rc;}
#if FZX_CD_TRACE
    {
        volatile uint32_t *t=fzx_cd_trace[fzx_cd_trace_n[0]++ % CD_TRACE_N];
        /* No per-sector hash: the first trace's 2 KB loop moved the timing and hid the fault. Words 0, 1, 64, 65. */
        t[0]=fad|(starting?0x80000000u:0u); t[1]=trace_head; t[2]=out->buffer32[0]; t[3]=out->buffer32[1];
        t[4]=out->buffer32[64]^out->buffer32[65];
    }
#endif
    stream_next=fad+1u;++fzx_cd_stats[0];
    return 0;
}
#endif
int fzx_road_sector_read(fad_t fad,sector_buffer_t *out) {
    int rc;
#if ENG_SND
    engine_snd_disc_read();
#endif
#if FZX_CD_STREAM
    {
        if(fad<150 || !out || ((uintptr_t)out&3u)) return -1;
        uint32_t (*clock)(void)=fzx_cd_clock;
        const uint32_t t0=clock?clock():0u;
        rc=stream_read(fad,out);
        if(clock) fzx_cd_stats[2]+=clock()-t0;
        yield();
        return rc;
    }
#endif
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
