#include "texture_cd.h"
#include "road_cd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
enum { BYTES=8320 };
static uint8_t pixels[BYTES];
static uint32_t output[BYTES/4+8] __attribute__((aligned(32)));
static uint8_t scratch[8192] __attribute__((aligned(32)));
static int fault;
static unsigned reads;
int cd_block_init(void){return fault==1?-1:0;}
int fzx_road_sector_read(fad_t fad,sector_buffer_t *out) {
    if(fault==2)return -7;
    memset(out,0xee,sizeof(*out));
    if(fad==166){out->buffer8[0]=fault==3?0:1;memcpy(out->buffer8+1,"CD001",5);return 0;}
    assert(fad>=200 && fad<205);++reads;
    if(fault==8 && fad==204)return -8;
    unsigned offset=(fad-200)*2048,count=BYTES-offset;
    if(count>2048)count=2048;
    memcpy(out->buffer8,pixels+offset,count);
    if(fault==7 && fad==204)out->buffer8[127]^=1;
    return 0;
}
void cdfs_config_set(const cdfs_config_t *config){assert(config->sector_count==1);}
void cdfs_filelist_init(cdfs_filelist_t *list,cdfs_filelist_entry_t *entries,uint32_t capacity) {
    assert(capacity==16);list->entries=entries;list->entries_count=0;
}
void cdfs_filelist_root_read(cdfs_filelist_t *list) {
    if(fault==4)return;
    list->entries_count=fault==5?2:1;
    for(unsigned i=0;i<list->entries_count;++i){
        list->entries[i]=(cdfs_filelist_entry_t){.type=CDFS_ENTRY_TYPE_FILE,.size=BYTES-(fault==6?4:0),.starting_fad=200};
        strcpy(list->entries[i].name,"SKYLINE.PAK");
    }
}
int main(void) {
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<BYTES;++i){pixels[i]=(uint8_t)(i*53+i/64);hash=(hash^pixels[i])*16777619u;}
    assert(fzx_texture_cd_scratch_bytes()<=sizeof(scratch));
    for(fault=0;fault<=8;++fault) {
        memset(output,0x69,sizeof(output));reads=0;
        bool ok=fzx_texture_cd_load("SKYLINE.PAK",BYTES,hash,output,scratch,sizeof(scratch));
        assert(ok==(fault==0));
        if(!fault){assert(fzx_texture_cd_status.status==1);assert(reads==5);
            assert(fzx_texture_cd_status.bytes==BYTES);assert(fzx_texture_cd_status.hash==hash);
            assert(memcmp(output,pixels,BYTES)==0);}
        else assert(fzx_texture_cd_status.status!=1);
        for(unsigned i=BYTES/4;i<sizeof(output)/4;++i)assert(output[i]==0x69696969u);
    }
    fault=0;
    assert(!fzx_texture_cd_load("SKYLINE.PAK",BYTES,hash^1,output,scratch,sizeof(scratch)));
    assert(!fzx_texture_cd_load("SKYLINE.PAK",BYTES,hash,output,scratch,16));
    assert(!fzx_texture_cd_load("SKYLINE.PAK",BYTES,hash,output,scratch+1,sizeof(scratch)-1));
    assert(!fzx_texture_cd_load("SKYLINE.PAK",BYTES-1,hash,output,scratch,sizeof(scratch)));
    assert(!fzx_texture_cd_load("SKYLINE.PAK",BYTES,hash,output+1,scratch,sizeof(scratch)));
    puts("texture CD: exact 8320 bytes, 5 sectors, guards, integrity and 13 rejection cases PASS");
}
