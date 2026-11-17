#ifndef FZX_COUNTDOWN_PACK_H
#define FZX_COUNTDOWN_PACK_H
#include "fzx_countdown.h"

enum {FZX_COUNTDOWN_VERTICES=92,FZX_COUNTDOWN_QUADS=23,FZX_COUNTDOWN_IMAGES=28};
typedef struct {
    const uint8_t *data;
    uint32_t bytes,texture_bytes,vertex_at,quad_at,image_at;
} fzx_countdown_pack_t;
typedef struct {
    uint32_t texture;
    uint16_t width,height;
    bool solid;
} fzx_countdown_image_t;

/* Admission is boot-only. Both hashes are complete-file identities; the caller
 * must separately verify the uploaded texture bytes against texture_hash.
 * A rejected pack leaves out unchanged. No storage is allocated here. */
bool fzx_countdown_pack_open(fzx_countdown_pack_t *out,const void *data,
    uint32_t bytes,uint32_t metadata_hash,uint32_t texture_bytes,uint32_t texture_hash);
bool fzx_countdown_positions(const fzx_countdown_pack_t *,sh2z_pos16_t *,unsigned capacity);
bool fzx_countdown_image(const fzx_countdown_pack_t *,unsigned quad,
    const fzx_countdown_phase_t *,fzx_countdown_image_t *);
#endif
