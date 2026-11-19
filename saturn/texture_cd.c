#include "texture_cd.h"
#include "road_cd.h"
#include <string.h>
#if FZX_SOURCE_MACHINE_MENU
enum { ROOT_ENTRIES=256 };
#elif FZX_SOURCE_COUNTDOWN
enum { ROOT_ENTRIES=32 };
#elif defined(FZX_TEXTURE_CD_ROOT_ENTRIES)
/* Engine E1 close-up arm A: one more pack than the 16-entry default holds. */
enum { ROOT_ENTRIES=FZX_TEXTURE_CD_ROOT_ENTRIES };
#else
enum { ROOT_ENTRIES=16 };
#endif

typedef struct {
    uint32_t guard_before[8];
    sector_buffer_t sector;
    cdfs_filelist_entry_t entries[ROOT_ENTRIES];
    uint32_t guard_after[8];
} scratch_t;
_Static_assert(sizeof(scratch_t)+32u<=FZX_TEXTURE_CD_SCRATCH_RESERVE,
               "CD scratch crosses the input-log guard; update the reserved gap");
static scratch_t *workspace;
volatile fzx_texture_cd_status_t fzx_texture_cd_status;
enum { GUARD=0x39a5c27e };
uint32_t fzx_texture_cd_scratch_bytes(void){return sizeof(scratch_t);}
static void volume_read(sector_t lba,sector_buffer_t *out) {
    const int rc=fzx_road_sector_read((fad_t)(lba+150u),out);
    if(rc){fzx_texture_cd_status.io_result=rc;memset(out,0,sizeof(*out));}
}
static bool fail(uint32_t status){fzx_texture_cd_status.status=status;return false;}
static uint32_t hash_bytes(uint32_t hash,const volatile uint8_t *data,uint32_t bytes) {
    while(bytes--)hash=(hash^*data++)*16777619u;
    return hash;
}
#if FZX_CD_STREAM
static cdfs_filelist_entry_t kept[ROOT_ENTRIES];
static uint32_t kept_count;
static bool kept_ok;
void fzx_texture_cd_reset(void){kept_ok=false;fzx_road_stream_invalidate();}
bool fzx_texture_cd_load(const char *name,uint32_t bytes,uint32_t expected,
                         void *vram,void *scratch,uint32_t scratch_bytes) {
    fzx_texture_cd_status=(fzx_texture_cd_status_t){0};
    if(!name || !*name || !bytes || (bytes&3u) || !vram || ((uintptr_t)vram&7u) ||
       !scratch || ((uintptr_t)scratch&31u) || scratch_bytes<sizeof(scratch_t))return fail(2);
    workspace=scratch;memset(workspace,0,sizeof(*workspace));
    for(unsigned i=0;i<8;++i)workspace->guard_before[i]=workspace->guard_after[i]=GUARD;
    fzx_texture_cd_status.scratch_start=(uint32_t)(uintptr_t)scratch;
    fzx_texture_cd_status.scratch_end=(uint32_t)(uintptr_t)scratch+sizeof(*workspace);
    fzx_texture_cd_status.status=10;
    /* Scenes image: the CD block is initialised and the root directory read once;
     * every later load looks its file up in the kept list. */
    cdfs_filelist_entry_t *const entries=kept;
    if(!kept_ok) {
        cpu_frt_init(CPU_FRT_CLOCK_DIV_32);
        if(cd_block_init())return fail(3);
        fzx_road_stream_invalidate();
    volume_read(16,&workspace->sector);
    if(fzx_texture_cd_status.io_result)return fail(3);
    if(workspace->sector.buffer8[0]!=1 || memcmp(workspace->sector.buffer8+1,"CD001",5))return fail(4);
    const cdfs_config_t config={.sector_read=volume_read,.sectors=&workspace->sector,.sector_count=1};
    cdfs_config_set(&config);
    cdfs_filelist_t list;
    cdfs_filelist_init(&list,entries,ROOT_ENTRIES);
    cdfs_filelist_root_read(&list);
    if(fzx_texture_cd_status.io_result)return fail(3);
    if(list.entries_count>ROOT_ENTRIES)return fail(5);
    kept_count=list.entries_count;kept_ok=true;
    }
    const uint32_t entries_count=kept_count;
    const cdfs_filelist_entry_t *file=NULL;
    for(uint32_t i=0;i<entries_count;++i) {
        const cdfs_filelist_entry_t *entry=&entries[i];
        if(entry->type==CDFS_ENTRY_TYPE_FILE && !strcmp(entry->name,name)) {
            if(file)return fail(5);
            file=entry;
        }
    }
    if(!file)return fail(5);
    if(file->size!=bytes)return fail(6);
    uint32_t hash=2166136261u;
    for(uint32_t at=0;at<bytes;at+=CDFS_SECTOR_SIZE) {
        const int rc=fzx_road_sector_read(file->starting_fad+at/CDFS_SECTOR_SIZE,&workspace->sector);
        if(rc){fzx_texture_cd_status.io_result=rc;return fail(3);}
        uint32_t count=bytes-at;if(count>CDFS_SECTOR_SIZE)count=CDFS_SECTOR_SIZE;
        hash=hash_bytes(hash,workspace->sector.buffer8,count);
        volatile uint32_t *dest=(volatile uint32_t *)vram+at/4u;
        for(uint32_t i=0;i<count/4u;++i)dest[i]=workspace->sector.buffer32[i];
        for(uint32_t i=0;i<count/4u;++i)if(dest[i]!=workspace->sector.buffer32[i])return fail(7);
        ++fzx_texture_cd_status.sectors;fzx_texture_cd_status.bytes+=count;
    }
    fzx_texture_cd_status.hash=hash;
    if(hash!=expected)return fail(8);
    for(unsigned i=0;i<8;++i)
        if(workspace->guard_before[i]!=GUARD || workspace->guard_after[i]!=GUARD)return fail(9);
    fzx_texture_cd_status.status=1;return true;
}
/* Scenes image: bytes [offset, offset + bytes) of a file into dest (CPU 32-bit stores;
 * offset, bytes and dest 4-byte aligned), FNV-1a of the bytes checked against expected as
 * they arrive. fzx_texture_cd_fault (captures) flips one byte of the first sector read, so
 * the check must refuse it. */
volatile uint32_t fzx_texture_cd_fault;
bool fzx_texture_cd_load_range(const char *name,uint32_t file_bytes,uint32_t offset,uint32_t bytes,uint32_t expected,
                               void *dest,void *scratch,uint32_t scratch_bytes) {
    if(!kept_ok) {
        /* The first use initialises the CD block and reads the directory (as any load). */
        static uint32_t probe[2] __attribute__((aligned(8)));
        (void)fzx_texture_cd_load(name,0xFFFFFFFCu,0,probe,scratch,scratch_bytes);
        if(!kept_ok)return false;
    }
    fzx_texture_cd_status=(fzx_texture_cd_status_t){0};
    if(!name||!bytes||(offset&3u)||(bytes&3u)||((uintptr_t)dest&3u)||!scratch||((uintptr_t)scratch&31u)||
       scratch_bytes<sizeof(scratch_t))return fail(2);
    workspace=scratch;
    const cdfs_filelist_entry_t *file=NULL;
    for(uint32_t i=0;i<kept_count;++i)
        if(kept[i].type==CDFS_ENTRY_TYPE_FILE && !strcmp(kept[i].name,name))file=&kept[i];
    if(!file)return fail(5);
    if(file->size!=file_bytes||offset>file_bytes||bytes>file_bytes-offset)return fail(6);
    uint32_t hash=2166136261u,done=0;
    volatile uint32_t *out=(volatile uint32_t *)dest;
    for(uint32_t sector=offset/CDFS_SECTOR_SIZE;done<bytes;++sector) {
        const int rc=fzx_road_sector_read(file->starting_fad+sector,&workspace->sector);
        if(rc){fzx_texture_cd_status.io_result=rc;return fail(3);}
        volatile uint32_t *const fault=(volatile uint32_t *)((uintptr_t)&fzx_texture_cd_fault|CPU_CACHE_THROUGH);
        if(*fault){workspace->sector.buffer8[(offset%CDFS_SECTOR_SIZE)]^=0x5Au;*fault=0;}
        uint32_t from=sector==offset/CDFS_SECTOR_SIZE?offset%CDFS_SECTOR_SIZE:0u;
        uint32_t n=CDFS_SECTOR_SIZE-from;if(n>bytes-done)n=bytes-done;
        hash=hash_bytes(hash,workspace->sector.buffer8+from,n);
        for(uint32_t k=0;k<n/4u;++k)out[(done/4u)+k]=workspace->sector.buffer32[(from/4u)+k];
        done+=n;++fzx_texture_cd_status.sectors;fzx_texture_cd_status.bytes+=n;
    }
    fzx_texture_cd_status.hash=hash;
    if(hash!=expected)return fail(8);
    fzx_texture_cd_status.status=1;return true;
}
#else
bool fzx_texture_cd_load(const char *name,uint32_t bytes,uint32_t expected,
                         void *vram,void *scratch,uint32_t scratch_bytes) {
    fzx_texture_cd_status=(fzx_texture_cd_status_t){0};
    if(!name || !*name || !bytes || (bytes&3u) || !vram || ((uintptr_t)vram&7u) ||
       !scratch || ((uintptr_t)scratch&31u) || scratch_bytes<sizeof(scratch_t))return fail(2);
    workspace=scratch;memset(workspace,0,sizeof(*workspace));
    for(unsigned i=0;i<8;++i)workspace->guard_before[i]=workspace->guard_after[i]=GUARD;
    fzx_texture_cd_status.scratch_start=(uint32_t)(uintptr_t)scratch;
    fzx_texture_cd_status.scratch_end=(uint32_t)(uintptr_t)scratch+sizeof(*workspace);
    fzx_texture_cd_status.status=10;
    cpu_frt_init(CPU_FRT_CLOCK_DIV_32);
    if(cd_block_init())return fail(3);
    volume_read(16,&workspace->sector);
    if(fzx_texture_cd_status.io_result)return fail(3);
    if(workspace->sector.buffer8[0]!=1 || memcmp(workspace->sector.buffer8+1,"CD001",5))return fail(4);
    const cdfs_config_t config={.sector_read=volume_read,.sectors=&workspace->sector,.sector_count=1};
    cdfs_config_set(&config);
    cdfs_filelist_t list;
    cdfs_filelist_init(&list,workspace->entries,ROOT_ENTRIES);
    cdfs_filelist_root_read(&list);
    if(fzx_texture_cd_status.io_result)return fail(3);
    if(list.entries_count>ROOT_ENTRIES)return fail(5);
    const cdfs_filelist_entry_t *file=NULL;
    for(uint32_t i=0;i<list.entries_count;++i) {
        const cdfs_filelist_entry_t *entry=&workspace->entries[i];
        if(entry->type==CDFS_ENTRY_TYPE_FILE && !strcmp(entry->name,name)) {
            if(file)return fail(5);
            file=entry;
        }
    }
    if(!file)return fail(5);
    if(file->size!=bytes)return fail(6);
    uint32_t hash=2166136261u;
    for(uint32_t at=0;at<bytes;at+=CDFS_SECTOR_SIZE) {
        const int rc=fzx_road_sector_read(file->starting_fad+at/CDFS_SECTOR_SIZE,&workspace->sector);
        if(rc){fzx_texture_cd_status.io_result=rc;return fail(3);}
        uint32_t count=bytes-at;if(count>CDFS_SECTOR_SIZE)count=CDFS_SECTOR_SIZE;
        hash=hash_bytes(hash,workspace->sector.buffer8,count);
        volatile uint32_t *dest=(volatile uint32_t *)vram+at/4u;
        for(uint32_t i=0;i<count/4u;++i)dest[i]=workspace->sector.buffer32[i];
        for(uint32_t i=0;i<count/4u;++i)if(dest[i]!=workspace->sector.buffer32[i])return fail(7);
        ++fzx_texture_cd_status.sectors;fzx_texture_cd_status.bytes+=count;
    }
    fzx_texture_cd_status.hash=hash;
    if(hash!=expected)return fail(8);
    for(unsigned i=0;i<8;++i)
        if(workspace->guard_before[i]!=GUARD || workspace->guard_after[i]!=GUARD)return fail(9);
    fzx_texture_cd_status.status=1;return true;
}
#endif
