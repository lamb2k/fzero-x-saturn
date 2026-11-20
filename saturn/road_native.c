#include <yaul.h>
#include <string.h>
#include "road_native.h"
#include "road_cd.h"
#include "road_identity.h"
#include "course_select.h"
#if FZX_LOAD_VERIFIED
#include "engine_load_fast.h"
#endif

_Static_assert(!FZX_ROAD_PACK_FIXTURE,"A source-road race requires the local source pack");
_Static_assert(FZX_ROAD_PACK_BYTES<=FZX_ROAD_MAX_BYTES,"road pack exceeds cartridge");
_Static_assert((FZX_ROAD_PACK_BYTES&3u)==0,"road pack must be word aligned");
fzx_road_pack_t fzx_native_road;
volatile fzx_road_load_status_t fzx_road_load_status;
volatile uint32_t fzx_road_next_frame, fzx_road_frame;
volatile uint32_t fzx_road_plan_status, fzx_road_plan_sequence;
RefRoadPlan fzx_source_road_plan;
_Static_assert(sizeof(RefRoadPlan)==796,"source road packet ABI");
#if ENG_COURSE_SELECT
#define road_fog (fzx_cid->road_fog)
#else
static const unsigned char road_fog[3]=FZX_ROAD_COURSE_FOG;
#endif
static uint32_t planned_frame;
static bool road_tick_seen,road_plan_seen;
static sector_buffer_t sector __aligned(32);
#if FZX_SOURCE_MACHINE_MENU
enum { ROOT_ENTRIES=256 };
#elif FZX_SOURCE_COUNTDOWN
enum { ROOT_ENTRIES=32 };
#elif ENG_COURSE_SELECT
enum { ROOT_ENTRIES=64 }; /* every bundled course's packs */
#else
enum { ROOT_ENTRIES=16 };
#endif
#if FZX_SIMPIPE_DBUF
#include "simpipe.h"
_Static_assert(ROOT_ENTRIES*sizeof(cdfs_filelist_entry_t)==FZX_SIMPIPE_ROAD_LIST_BYTES,"road file list reservation");
#define entries ((cdfs_filelist_entry_t *)(uintptr_t)FZX_SIMPIPE_ROAD_LIST)
static void road_list_guards(void){
    volatile uint32_t *g=(volatile uint32_t *)(uintptr_t)FZX_SIMPIPE_TOP_RESERVE_BASE;
    volatile uint32_t *h=(volatile uint32_t *)(uintptr_t)(FZX_SIMPIPE_ROAD_LIST+FZX_SIMPIPE_ROAD_LIST_BYTES);
    for(unsigned i=0;i<8u;++i){g[i]=FZX_SIMPIPE_GUARD;h[i]=FZX_SIMPIPE_GUARD;}
}
#else
static cdfs_filelist_entry_t entries[ROOT_ENTRIES];
#endif
void fzx_road_note_tick(void) {
    fzx_road_frame=fzx_road_next_frame++;
    road_tick_seen=true;
}
void fzx_road_restart(void) {road_plan_seen=false;road_tick_seen=false;}
#ifndef FZX_ENGINE /* the engine line fills this state itself (saturn/engine) */
bool fzx_road_publish(void) {
    if(!road_tick_seen) return true;
    if(road_plan_seen && planned_frame==fzx_road_frame) return fzx_road_plan_status==1;
    ref_road_plan(&fzx_source_road_plan,fzx_road_frame,road_fog);
    const unsigned int chunks=fzx_native_road.count[FZX_ROAD_CHUNK];
    if(fzx_source_road_plan.faults || fzx_source_road_plan.outer>1 ||
       fzx_source_road_plan.visible_chunks>chunks || fzx_source_road_plan.count>REF_ROAD_GROUPS) {
        fzx_road_plan_status=2;return false;
    }
    for(unsigned int i=0;i<fzx_source_road_plan.count;++i) {
        const RefRoadGroup *g=&fzx_source_road_plan.groups[i];
        if(g->start>=chunks || g->end>=chunks || (g->direction!=1 && g->direction!=-1)) {
            fzx_road_plan_status=3;return false;
        }
    }
    planned_frame=fzx_road_frame;road_plan_seen=true;
    ++fzx_road_plan_sequence;fzx_road_plan_status=1;
    return true;
}
#endif
static void read_volume_sector(sector_t lba,sector_buffer_t *out) {
    int rc=fzx_road_sector_read((fad_t)(lba+150u),out);
    if(rc) {
        fzx_road_load_status.io_result=rc;
        memset(out,0,sizeof(*out));
    }
}

static bool cart_probe(volatile uint32_t *cart) {
    uint32_t saved[4]; bool ok=true;
    for(unsigned i=0;i<4;++i) saved[i]=cart[i*0x40000u];
    for(unsigned i=0;i<4;++i) cart[i*0x40000u]=0xa19c52e7u+i;
    for(unsigned i=0;i<4;++i) if(cart[i*0x40000u]!=0xa19c52e7u+i) ok=false;
    for(unsigned i=0;i<4;++i) cart[i*0x40000u]=saved[i];
    return ok;
}

bool fzx_road_load(const void *course,uint32_t course_bytes) {
    cdfs_filelist_t list;
    const cdfs_filelist_entry_t *file=NULL;
    fzx_road_load_status=(fzx_road_load_status_t){0};
    memset(&fzx_native_road,0,sizeof(fzx_native_road));
    cpu_frt_init(CPU_FRT_CLOCK_DIV_32);
    dram_cart_init();
    fzx_road_load_status.cart_id=(uint32_t)dram_cart_id_get();
    volatile uint32_t *cart=dram_cart_area_get();
    if(fzx_road_load_status.cart_id!=DRAM_CART_ID_4MIB || dram_cart_size_get()!=FZX_ROAD_MAX_BYTES ||
       !cart || !cart_probe(cart)) {fzx_road_load_status.status=2;return false;}
    fzx_road_load_status.status=10;
    if(cd_block_init()!=0) {fzx_road_load_status.status=3;return false;}
#if FZX_CD_STREAM
    fzx_road_stream_invalidate();
#endif
    /* CDFS's default callback discards read errors and then asserts on a bad
     * volume header. Probe it first so boot can report a plain load failure. */
    fzx_road_load_status.io_result=fzx_road_sector_read(166,&sector);
    fzx_road_load_status.volume_word=fzx_road_u32(sector.buffer8);
    if(fzx_road_load_status.io_result) {fzx_road_load_status.status=3;return false;}
    if(sector.buffer8[0]!=1 || memcmp(sector.buffer8+1,"CD001",5)) {
        fzx_road_load_status.status=7;return false;
    }
    fzx_road_load_status.status=13;
    const cdfs_config_t config={.sector_read=read_volume_sector,.sectors=&sector,.sector_count=1};
    cdfs_config_set(&config);
#if FZX_SIMPIPE_DBUF
    road_list_guards();
#endif
    cdfs_filelist_init(&list,entries,ROOT_ENTRIES);
    cdfs_filelist_root_read(&list);
    if(fzx_road_load_status.io_result) {fzx_road_load_status.status=3;return false;}
    fzx_road_load_status.status=11;
    for(uint32_t i=0;i<list.entries_count;++i) {
        if(entries[i].type==CDFS_ENTRY_TYPE_FILE && !strcmp(entries[i].name,CNAME(FZX_CF_ROAD,"ROAD.PAK"))) {
            if(file) {fzx_road_load_status.status=4;return false;}
            file=&entries[i];
        }
    }
    if(!file) {fzx_road_load_status.status=4;return false;}
    if(file->size!=CSEL(road_bytes,FZX_ROAD_PACK_BYTES)) {fzx_road_load_status.status=5;return false;}
#if FZX_LOAD_VERIFIED
    uint32_t road_hash=2166136261u;
#endif
    for(uint32_t at=0;at<file->size;at+=CDFS_SECTOR_SIZE) {
        fzx_road_load_status.io_result=fzx_road_sector_read(file->starting_fad+at/CDFS_SECTOR_SIZE,&sector);
        if(fzx_road_load_status.io_result) {
            fzx_road_load_status.status=3;return false;
        }
        uint32_t count=file->size-at;
        if(count>CDFS_SECTOR_SIZE) count=CDFS_SECTOR_SIZE;
        /* CPU stores only. The cartridge is never an SCU/SH-2 DMA endpoint. */
        volatile uint32_t *dest=cart+at/4u;
        for(uint32_t i=0;i<count/4u;++i) dest[i]=sector.buffer32[i];
#if FZX_LOAD_VERIFIED
        /* The pack's own FNV (bytes 20..23 as zero) as the sectors arrive: the open
         * below does not read the pack a second time for it. */
        for(uint32_t i=0;i<count;++i) road_hash=(road_hash^((at+i>=20u && at+i<24u)?0u:sector.buffer8[i]))*16777619u;
#endif
        ++fzx_road_load_status.sectors;
    }
    fzx_road_load_status.status=12;
#if FZX_LOAD_VERIFIED
    if(road_hash!=CSEL(road_hash,FZX_ROAD_PACK_HASH) ||
       !fzx_road_pack_open_verified(&fzx_native_road,(const void *)cart,file->size,CSEL(road_hash,FZX_ROAD_PACK_HASH),
                                    fzx_road_hash(course,course_bytes),false)) {
#else
    if(!fzx_road_pack_open(&fzx_native_road,(const void *)cart,file->size,CSEL(road_hash,FZX_ROAD_PACK_HASH),
                           fzx_road_hash(course,course_bytes),false)) {
#endif
        fzx_road_load_status.status=6;return false;
    }
    fzx_road_load_status.bytes=fzx_native_road.bytes;
    fzx_road_load_status.hash=fzx_native_road.hash;
    fzx_road_load_status.vertices=fzx_native_road.count[FZX_ROAD_VERTEX];
    fzx_road_load_status.triangles=fzx_native_road.count[FZX_ROAD_TRIANGLE];
    fzx_road_load_status.chunks=fzx_native_road.count[FZX_ROAD_CHUNK];
    fzx_road_load_status.status=1;
    return true;
}
