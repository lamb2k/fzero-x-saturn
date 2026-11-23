/* HOST stub of the backup library: files in memory, enough for engine_records.c. */
#ifndef FZX_HOST_BUP_STUB_H
#define FZX_HOST_BUP_STUB_H
#include <stdint.h>
typedef int bup_status_t;
enum { BUP_OK = 0, BUP_NOT_ENOUGH_MEMORY = 1, BUP_FAILED = 2 };
enum { BUP_DEVICE_INTERNAL = 0 };
enum { BUP_WRITE_MODE_OVERWRITE = 0 };
enum { BUP_LANG_ENGLISH = 1 };
typedef struct { int unused; } bup_t;
typedef struct { int unused; } bup_devices_t;
typedef struct { char filename[16]; char comment[16]; int language; uint32_t data_size; } bup_directory_t;
typedef struct { uint32_t free_blocks, block_size; } bup_stat_t;
void bup_init(bup_t *bup, bup_devices_t *devices);
bup_status_t bup_read(int dev, const char *name, void *buf);
int bup_dir(int dev, const char *name, int n, bup_directory_t *d);
bup_status_t bup_write(int dev, const bup_directory_t *dir, const void *buf, int mode);
bup_status_t bup_status(int dev, uint32_t size, bup_stat_t *st);
#endif
