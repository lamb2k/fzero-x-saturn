/* Bounded native machine image probe; no display-list execution. */
#ifndef FZX_MODEL_H
#define FZX_MODEL_H
#include "fzx_material.h"

typedef struct {
    int16_t x_q4, y_q4;
    uint16_t inverse_z;
    int16_t u_q5, v_q5; /* source signed tc, before gSPTexture scaling */
    fzx_material_color_t shade;
} fzx_model_vertex_t;
typedef struct { uint16_t vertex[3], material; } fzx_model_triangle_t;
typedef struct {
    fzx_material_combine_t combine;
    uint16_t texture;
    bool body_environment;
} fzx_model_material_t;
typedef struct {
    const fzx_model_vertex_t *vertices;
    const fzx_model_triangle_t *triangles;
    const fzx_model_material_t *materials;
    uint16_t vertex_count, triangle_count, material_count;
} fzx_model_view_t;
typedef struct {
    uint16_t *pixels;
    uint32_t *depth;
    uint16_t width,height;
    uint32_t tested_pixels, drawn_pixels, drawn_triangles;
} fzx_model_image_t;

/* All vertices must already be projected in front of the near plane. This
 * first probe gate is intentionally not a world camera or near-plane clipper.
 * Bounds/indices/texture spans are validated before writing the image.
 * Numerically unsafe slivers fail during setup; callers must discard the
 * entire output on false (it may already contain earlier triangles). */
bool fzx_model_render(const fzx_model_view_t *view,
                      const fzx_material_texture_t *textures, unsigned texture_count,
                      const uint8_t body[3], const uint8_t fog[3],
                      fzx_model_image_t *image);
#endif
