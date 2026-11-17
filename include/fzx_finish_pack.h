#ifndef FZX_FINISH_PACK_H
#define FZX_FINISH_PACK_H
#include <stdbool.h>
#include <stdint.h>
#include <sh2zam/view_experimental.h>

/* FZXFIN01 (all integers BE), header u32[14] after magic:
 * metadata_bytes,texture_bytes,texture_fnv,vertex_count,triangle_count,
 * image_count,vertex_at,triangle_at,image_at,self_fnv,solid_rgb1555,
 * final_geometry_flags,palette_at,reserved. self_fnv treats its own word as 0.
 * Vertex16: s16 xyz[3],scaled source Q5 uv[2],source attributes[4],zero[2].
 * Triangle16: u16 vertex[3],material,image,source_index,zero[2].
 * Image16: u16 width,height;u32 texture_at;u16 source_triangle,zero[3].
 * Material0 is the original constant-color side; image=0xffff.
 * Material1 is the original face; chart A/B top corners,C collapsed bottom.
 * Preserve source order and back-face culling. No lighting or depth sort.
 * Texture blob: 16 BE RGB1555 CLUT entries followed by 128B aligned 16x16
 * CLUT4 charts, high nibble first. The first13 palette entries retain ALL
 * original face colors; remaining entries repeat entry0. Every entry opaque.
 * VDP1 SPD MUST BE 1: palette index0 is opaque, not transparent. ECD=1.
 * Source camera is C2160.c's dedicated 45-degree finish projection, not race
 * camera; source model matrix includes0.07 scale. Native projection is external.
 */
enum { FZX_FINISH_VERTICES=256,FZX_FINISH_TRIANGLES=222,FZX_FINISH_IMAGES=98,
       FZX_FINISH_METADATA_BYTES=9280,FZX_FINISH_PALETTE_BYTES=32,
       FZX_FINISH_CHART_BYTES=128,FZX_FINISH_MAX_TEXTURE_BYTES=12576,
       FZX_FINISH_FACE_COLORS=13,FZX_FINISH_OPAQUE_PALETTE_ZERO=1,
       FZX_FINISH_SOURCE_GEOMETRY=0x800404 };
typedef struct {
    const uint8_t *data;
    uint32_t bytes,texture_bytes,texture_hash,vertex_at,triangle_at,image_at,palette_at;
    uint16_t solid_color;
} fzx_finish_pack_t;
typedef struct { int16_t position[3],uv[2];uint8_t attributes[4]; } fzx_finish_vertex_t;
typedef struct { uint16_t vertex[3],material,image,source_index; } fzx_finish_triangle_t;
typedef struct { uint32_t texture;uint16_t width,height,triangle; } fzx_finish_image_t;

/* Boot-only admission, immutable metadata retained by caller. No allocation.
 * Rejection leaves output untouched. Full-file expected hashes are generated
 * identities; self hash additionally protects format-independent tooling.
 * Call texture_check against actual uploaded bytes before using the pack. */
bool fzx_finish_pack_open(fzx_finish_pack_t *,const void *,uint32_t bytes,
                         uint32_t metadata_hash,uint32_t texture_bytes,uint32_t texture_hash);
bool fzx_finish_texture_check(const fzx_finish_pack_t *,const void *,uint32_t bytes);
bool fzx_finish_vertex(const fzx_finish_pack_t *,unsigned index,fzx_finish_vertex_t *);
bool fzx_finish_positions(const fzx_finish_pack_t *,sh2z_pos16_t *,unsigned capacity);
bool fzx_finish_triangle(const fzx_finish_pack_t *,unsigned index,fzx_finish_triangle_t *);
bool fzx_finish_image(const fzx_finish_pack_t *,unsigned index,fzx_finish_image_t *);
#endif
