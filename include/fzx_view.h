#ifndef FZX_VIEW_H
#define FZX_VIEW_H
#include <stdbool.h>
#include <stdint.h>

/* Explicit source-camera presentation boundary. World coordinates/translation
 * use Q16.16 at 1/16 source scale; rotation uses Q2.30 to retain small distant
 * world differences. Focal lengths and screen centres are Q16.16 pixels.
 * Screen rectangle is half-open. This is not a simulation representation. */
typedef struct {
    int32_t rotation[3][3], translation[3];
    int32_t focal[2], center[2], near_plane, far_plane;
    int16_t clip[4];
} fzx_view_t;

bool fzx_view_from_source(fzx_view_t *out, const float view[4][4],
                         const float projection[4][4], const float viewport[4],
                         const float scissor[4], float near_plane, float far_plane);
bool fzx_view_transform(const fzx_view_t *view, const int32_t world[3], int32_t out[3]);
int64_t fzx_view_plane(const fzx_view_t *view, const int32_t point[3], unsigned plane);
bool fzx_view_project(const fzx_view_t *view, const int32_t point[3], int32_t screen[2]);
/* Exact Q0.16 edge fraction for opposite-sign distances, without dp*65536 overflow. */
int32_t fzx_view_fraction(int64_t previous, int64_t current);
/* Convex triangle/quad clipping. Returns -1 on invalid input/capacity, otherwise
 * 0..10 vertices. Intersections use Q0.32 and snap inward on the integer lattice;
 * all output points satisfy all six planes. Raster scissor is still half-open. */
int fzx_view_clip(const fzx_view_t *view, const int32_t input[][3], unsigned count,
                  int32_t output[10][3]);
/* FZX_RBG_LEAN (default 0; compiled only when set): clip a convex quad against
 * the near and far planes only, project, then clip in 2D (Q16.16 screen) to the
 * view rectangle grown by guard pixels on every side. VDP1's user clip trims
 * the rest, so the visible pixels match fzx_view_clip plus projection apart
 * from sub-pixel rounding on the guard edges. Returns -1 on failure, else 0..10. */
#ifndef FZX_RBG_SHARE
#define FZX_RBG_SHARE 0
#endif
#if FZX_RBG_SHARE
/* FZX_RBG_SHARE: fzx_view_project_guarded's 2D clip of a quad already projected
 * and wholly between the near and far planes (exact). */
int fzx_view_guard_projected(const fzx_view_t *view, const int32_t projected[4][2],
                             int32_t guard, int32_t output[10][2]);
#endif
int fzx_view_project_guarded(const fzx_view_t *view, const int32_t input[4][3],
                             int32_t guard, int32_t output[10][2]);
#endif
