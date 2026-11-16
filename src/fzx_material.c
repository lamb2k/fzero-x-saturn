#include "fzx_material.h"
#include <limits.h>

static int32_t floor_div_pow2(int32_t value, unsigned shift)
{
    const int32_t divisor = (int32_t)(1u << shift);
    /* C division truncates toward zero, while source RSP shifts round down. */
    return value / divisor - (value % divisor < 0);
}

int32_t fzx_material_vertex_uv(int16_t tc, uint16_t scale)
{
    const int32_t scaled = floor_div_pow2((int32_t)tc * scale, 16);
    return scaled * 8;
}

bool fzx_material_texture_valid(const fzx_material_texture_t *t)
{
    if (!t || !t->data || !t->width || !t->height || t->width > 256 || t->height > 256 ||
        t->format > FZX_TEX_IA4 || t->s_mode > 3 || t->t_mode > 3 ||
        t->s_mask > 8 || t->t_mask > 8 || t->s_shift > 15 || t->t_shift > 15)
        return false;
    const uint32_t stride = t->format == FZX_TEX_RGBA16 ? t->width * 2u : (t->width + 1u) / 2u;
    if (t->size != stride * t->height) return false;
    /* This bounded pack contains complete, power-of-two source images. It
     * does not emulate TMEM aliases or mask periods larger than an image. */
    if (t->s_mask && (1u << t->s_mask) != t->width) return false;
    if (t->t_mask && (1u << t->t_mask) != t->height) return false;
    return true;
}

static int32_t shifted(int32_t coordinate, unsigned shift)
{
    if (shift <= 10) return floor_div_pow2(coordinate, shift);
    const int32_t factor = (int32_t)(1u << (16u - shift));
    /* Saturate before address reduction for unsupported extreme caller UVs;
     * original signed vertex UVs and the source pack never reach these bounds. */
    if (coordinate > INT32_MAX / factor) return INT32_MAX;
    if (coordinate < INT32_MIN / factor) return INT32_MIN;
    return coordinate * factor;
}

static int32_t clamp_coordinate(int32_t coordinate, unsigned extent, unsigned mode)
{
    if (mode & FZX_ADDRESS_CLAMP) {
        if (coordinate < 0) return 0;
        if (coordinate > (int32_t)(extent - 1u) * 256) return (int32_t)(extent - 1u) * 256;
    }
    return coordinate;
}

static unsigned address(int32_t index, unsigned extent, unsigned mask, unsigned mode)
{
    if (mode & FZX_ADDRESS_CLAMP) {
        if (index < 0) return 0;
        if (index >= (int32_t)extent) return extent - 1u;
        return (unsigned)index;
    }
    const unsigned period = mask ? 1u << mask : extent;
    const unsigned total = mode & FZX_ADDRESS_MIRROR ? period * 2u : period;
    int32_t reduced = index % (int32_t)total;
    if (reduced < 0) reduced += (int32_t)total;
    if ((mode & FZX_ADDRESS_MIRROR) && reduced >= (int32_t)period)
        reduced = (int32_t)(2u * period - 1u) - reduced;
    return (unsigned)reduced;
}

static void texel(const fzx_material_texture_t *t, int32_t x, int32_t y, int32_t result[4])
{
    const unsigned u = address(x, t->width, t->s_mask, t->s_mode);
    const unsigned v = address(y, t->height, t->t_mask, t->t_mode);
    if (t->format == FZX_TEX_RGBA16) {
        const unsigned offset = (v * t->width + u) * 2u;
        const unsigned word = (unsigned)t->data[offset] << 8 | t->data[offset + 1u];
        result[0] = (int32_t)(((word >> 11) & 31u) * 255u / 31u);
        result[1] = (int32_t)(((word >> 6) & 31u) * 255u / 31u);
        result[2] = (int32_t)(((word >> 1) & 31u) * 255u / 31u);
        result[3] = (word & 1u) ? 255 : 0;
    } else {
        const unsigned packed = t->data[v * ((t->width + 1u) / 2u) + u / 2u];
        const unsigned nibble = (packed >> ((u & 1u) ? 0 : 4)) & 15u;
        const int32_t intensity = (int32_t)(t->format == FZX_TEX_I4 ? nibble * 17u : (nibble >> 1) * 36u);
        result[0] = result[1] = result[2] = intensity;
        result[3] = t->format == FZX_TEX_I4 ? intensity : (nibble & 1u) ? 255 : 0;
    }
}

fzx_material_color_t fzx_material_sample(const fzx_material_texture_t *t, int32_t u, int32_t v)
{
    u = clamp_coordinate(shifted(u, t->s_shift), t->width, t->s_mode);
    v = clamp_coordinate(shifted(v, t->t_shift), t->height, t->t_mode);
    const int32_t x = floor_div_pow2(u, 8), y = floor_div_pow2(v, 8);
    const unsigned fx = (unsigned)(u - x * 256), fy = (unsigned)(v - y * 256);
    int32_t a[4], b[4], c[4];
    unsigned wx, wy;
    if (fx + fy < 256u) {
        texel(t, x, y, a); texel(t, x + 1, y, b); texel(t, x, y + 1, c);
        wx = fx; wy = fy;
    } else {
        texel(t, x + 1, y + 1, a); texel(t, x, y + 1, b); texel(t, x + 1, y, c);
        wx = 256u - fx; wy = 256u - fy;
    }
    uint16_t out[4];
    for (unsigned channel = 0; channel < 4; ++channel) {
        const int32_t q8 = a[channel] * 256 + (b[channel] - a[channel]) * (int32_t)wx +
                           (c[channel] - a[channel]) * (int32_t)wy;
        out[channel] = (uint16_t)((q8 * 257 + 128) / 256);
    }
    return (fzx_material_color_t){out[0], out[1], out[2], out[3]};
}

/* Rounded multiply by UNORM16, with exact zero/one endpoints. Products fit
 * uint32_t; no per-channel SH-2 software division or 64-bit multiply. */
static uint32_t modulate(uint32_t value, uint32_t factor)
{
    const uint32_t product = value * factor + 32768u;
    return (product + (product >> 16)) >> 16;
}

fzx_material_color_t fzx_material_shade(const fzx_material_combine_t *m, fzx_material_color_t t,
                                      fzx_material_color_t s, const uint8_t fog[3])
{
    const uint16_t tex[3] = {t.r,t.g,t.b}, light[3] = {s.r,s.g,s.b};
    uint16_t result[3];
    for (unsigned c = 0; c < 3; ++c) {
        uint32_t material;
        const uint32_t environment = (uint32_t)m->environment[c] * 257u;
        if (m->texel_alpha) {
            material = modulate(tex[c], t.a) + modulate(environment, 65535u - t.a);
        } else {
            material = modulate((uint32_t)m->primitive[c] * 257u, tex[c]) +
                       modulate(environment, 65535u - tex[c]);
        }
        if (material > 65535u) material = 65535u;
        const uint32_t shaded = modulate(material, light[c]);
        uint32_t reflected = modulate(shaded, 65535u - s.a) +
                             modulate((uint32_t)fog[c] * 257u, s.a);
        if (reflected > 65535u) reflected = 65535u;
        result[c] = (uint16_t)reflected;
    }
    return (fzx_material_color_t){result[0], result[1], result[2], 65535};
}

uint16_t fzx_material_pixel(const fzx_material_combine_t *m, fzx_material_color_t t,
                           fzx_material_color_t s, const uint8_t fog[3])
{
    const fzx_material_color_t result = fzx_material_shade(m,t,s,fog);
    const uint16_t channels[3] = {result.r,result.g,result.b};
    uint16_t color = 0x8000u;
    for (unsigned c=0;c<3;++c) {
        /* Same 8-bit then top-five-bit convention as the source art decoder. */
        const uint32_t byte = ((uint32_t)channels[c] + 128u) / 257u;
        color |= (uint16_t)((byte >> 3) << (c * 5));
    }
    return color;
}
