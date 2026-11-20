/* HOST stand-in for saturn/texture_cd.h (tools/attacks_host): the loader reads the named pack from disk. */
#ifndef FZX_TEXTURE_CD_H
#define FZX_TEXTURE_CD_H
#include <stdbool.h>
#include <stdint.h>
bool fzx_texture_cd_load(const char *name, uint32_t bytes, uint32_t hash, void *dst, void *scratch, uint32_t scratch_bytes);
#endif
