#ifndef FZX_TEXTURE_CD_H
#define FZX_TEXTURE_CD_H
#include <yaul.h>
typedef struct {
    uint32_t status, bytes, sectors, hash, scratch_start, scratch_end;
    int32_t io_result;
} fzx_texture_cd_status_t;
extern volatile fzx_texture_cd_status_t fzx_texture_cd_status;
/* Boot only; CPU stores to aligned VDP1 VRAM. Scratch must outlive CDFS use. */
bool fzx_texture_cd_load(const char *name,uint32_t bytes,uint32_t hash,
                         void *vram,void *scratch,uint32_t scratch_bytes);
uint32_t fzx_texture_cd_scratch_bytes(void);
#endif
