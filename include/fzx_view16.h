#ifndef FZX_VIEW16_H
#define FZX_VIEW16_H
#include "fzx_view.h"
#include "sh2zam/view_experimental.h"

/* Once-per-camera conversion for the source road stream. eye is the source
 * camera position in Q16.16 source units (not the old 1/16 world scale).
 * The caller supplies eye from the same camera as view. Source viewport
 * centres round to the nearest pixel; half-open clipping becomes inclusive. The Y
 * centre is reduced by one to adapt the library's upward raster rounding
 * to the existing positive-screen truncation. This can differ by one pixel
 * at exact integer projections. Neither the source view nor out changes on
 * failure. Does not transform vertices or replace any library kernel. */
bool fzx_view16_from_source(sh2z_view_t *out,const fzx_view_t *view,
                            const int32_t eye[3]);
#endif
