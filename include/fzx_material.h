/* Native machine material operations. MIT; source references in NOTICE.md. */
#ifndef FZX_MATERIAL_H
#define FZX_MATERIAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FZX_TEX_RGBA16, FZX_TEX_I4, FZX_TEX_IA4 };
enum { FZX_ADDRESS_MIRROR = 1, FZX_ADDRESS_CLAMP = 2 };
typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint16_t width, height;
    uint8_t format, s_mode, t_mode, s_mask, t_mask, s_shift, t_shift;
} fzx_material_texture_t;

/* Channels are UNORM16: 0 means zero, 65535 means one. UVs are signed Q24.8
 * source texel-index coordinates, after source texture scaling. No extra
 * half-texel offset. The stock race uses triangular three-point filtering. */
typedef struct { uint16_t r, g, b, a; } fzx_material_color_t;
typedef struct {
    uint8_t primitive[3], environment[3];
    bool texel_alpha; /* (T-E)*Ta+E; false: (P-E)*T+E */
} fzx_material_combine_t;

bool fzx_material_texture_valid(const fzx_material_texture_t *texture);
/* Caller validates immutable textures once when loading the local pack. */
fzx_material_color_t fzx_material_sample(const fzx_material_texture_t *texture,
                                         int32_t u_q8, int32_t v_q8);
/* Exact signed scaling from source gSPTexture, returning Q24.8 texel units.
 * tc is the original signed Q10.5 vertex coordinate, scale is normally ffff. */
int32_t fzx_material_vertex_uv(int16_t tc, uint16_t scale);
/* Interpolated shade RGB and reflection alpha are supplied independently.
 * Reflection alpha blends toward fog RGB; it does not cut out the machine. */
uint16_t fzx_material_pixel(const fzx_material_combine_t *material,
                           fzx_material_color_t texel,
                           fzx_material_color_t shade,
                           const uint8_t fog[3]);
fzx_material_color_t fzx_material_shade(const fzx_material_combine_t *material,
                                      fzx_material_color_t texel,
                                      fzx_material_color_t shade,
                                      const uint8_t fog[3]);
#endif
