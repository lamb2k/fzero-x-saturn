#ifndef FZX_TEXTURE_CD_H
#define FZX_TEXTURE_CD_H
#include <yaul.h>
/* The machine-menu disc has 256 directory slots. Keep this boot-only gap
 * in sync with ref/sat/gen_sat.py's low-RAM input-log reservation. */
#if FZX_SOURCE_MACHINE_MENU
#define FZX_TEXTURE_CD_SCRATCH_RESERVE 12288u
#else
#define FZX_TEXTURE_CD_SCRATCH_RESERVE 4096u
#endif
typedef struct {
    uint32_t status, bytes, sectors, hash, scratch_start, scratch_end;
    int32_t io_result;
} fzx_texture_cd_status_t;
extern volatile fzx_texture_cd_status_t fzx_texture_cd_status;
/* Boot only; CPU stores to aligned VDP1 VRAM. Scratch must outlive CDFS use. */
bool fzx_texture_cd_load(const char *name,uint32_t bytes,uint32_t hash,
                         void *vram,void *scratch,uint32_t scratch_bytes);
uint32_t fzx_texture_cd_scratch_bytes(void);
#if FZX_CD_STREAM
/* Scenes image: whole-file loads read again on a wrong arrival (texture_cd.c). */
extern volatile uint32_t fzx_texture_cd_retries[3];
/* Scenes image: forget the kept root list; the next load initialises the CD block. */
void fzx_texture_cd_reset(void);
/* Bytes [offset, offset + bytes) of a file, FNV-1a checked; see texture_cd.c. */
bool fzx_texture_cd_load_range(const char *name,uint32_t file_bytes,uint32_t offset,uint32_t bytes,uint32_t expected,
                               void *dest,void *scratch,uint32_t scratch_bytes);
extern volatile uint32_t fzx_texture_cd_fault;
#endif
#endif
