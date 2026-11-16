#ifndef TEST_YAUL_H
#define TEST_YAUL_H
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t fad_t,sector_t;
#define CDFS_SECTOR_SIZE 2048u
#define CDFS_ENTRY_TYPE_FILE 1
#define CPU_FRT_CLOCK_DIV_32 1
static inline void cpu_frt_init(int clock){(void)clock;}
typedef union {uint8_t buffer8[2048];uint32_t buffer32[512];} sector_buffer_t;
typedef struct {uint32_t type,size,starting_fad;char name[32];} cdfs_filelist_entry_t;
typedef struct {uint32_t entries_count;cdfs_filelist_entry_t *entries;} cdfs_filelist_t;
typedef struct {void (*sector_read)(sector_t,sector_buffer_t *);sector_buffer_t *sectors;uint32_t sector_count;} cdfs_config_t;
int cd_block_init(void);
void cdfs_config_set(const cdfs_config_t *);
void cdfs_filelist_init(cdfs_filelist_t *,cdfs_filelist_entry_t *,uint32_t);
void cdfs_filelist_root_read(cdfs_filelist_t *);
#endif
