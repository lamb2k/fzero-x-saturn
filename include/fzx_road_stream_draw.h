#ifndef FZX_ROAD_STREAM_DRAW_H
#define FZX_ROAD_STREAM_DRAW_H
#include "fzx_road_stream.h"
#include "fzx_view16.h"

/* Experimental C3 command preparation. No native path selects this API.
 * Pixel/palette addresses are byte offsets into the admitted texture bank.
 * A native adapter must preserve opaque CLUT indices 0/15 and use Gouraud
 * mode; solid commands use neutral RGB1555 0xc210 as their polygon colour. */
typedef struct {
    uint32_t pixels,palette;
    uint16_t width,height,mode,reserved;
} fzx_road_stream_material_t;
typedef struct {
    fzx_road_stream_material_t variant[3];
    uint8_t mean[3],reserved;
} fzx_road_stream_image_t;
typedef struct {
    fzx_road_stream_material_t material;
    int16_t x[4],y[4];
    uint16_t shade[4];
    uint16_t quad,piece,tier,solid;
} fzx_road_stream_command_t;

enum {
    FZX_RS_FORCE_FIRST=1,
    /* HOST census only: retain unaligned CLUT addresses for diagnosis. These
     * records MUST NOT be submitted to VDP1. Normal draws reject them. */
    FZX_RS_AUDIT_UNALIGNED=2
};
enum {
    FZX_RS_INVALID=1,FZX_RS_GATHER,FZX_RS_PROJECT,FZX_RS_CLIP,
    FZX_RS_CAPACITY,FZX_RS_TEXTURE_ALIGNMENT,FZX_RS_STEP
};
typedef struct {
    uint32_t commands,positions,quads,chunks,culled,clipped,tiers[4];
    uint32_t unaligned,fog_updates,fog_error,fault,error_quad;
} fzx_road_stream_draw_stats_t;

/* Caller owns all storage, disjoint from immutable packs and output commands.
 * Zero work initially, then bind the pointers/capacities before prepare().
 * Images need pack.count[IMAGES] entries; remap needs pack.count[VERTICES].
 * Native source replay requires 576 position/projected slots (observed max552).
 * The small stepping/clipping scratch is part of work, not the C stack. */
typedef struct {
    uint16_t *remap;
    sh2z_pos16_t *positions;
    sh2z_view_vertex_t *projected;
    fzx_road_stream_image_t *images;
    uint32_t remap_capacity,vertex_capacity,image_capacity;
    const fzx_road_stream_pack_t *pack;
    bool prepared,fog_valid;
    int32_t fog_pa,fog_pb,fog_start,fog_near,fog_far;
    uint32_t fog_depth[256];
    int32_t boundary[65];
    sh2z_view_vertex_t edge[2][65];
    sh2z_view_guard_vertex_t clip_in[4],clip_out[SH2Z_VIEW_GUARD_MAX_OUT];
} fzx_road_stream_draw_work_t;

/* Boot-only image templates and approximate RGB5 material means, decoded
 * from the unfogged admitted image. expected_texture_hash is the complete
 * generated-file identity, not its embedded zero-field checksum. */
bool fzx_road_stream_draw_prepare(fzx_road_stream_draw_work_t *,
    const fzx_road_stream_pack_t *,const void *textures,uint32_t bytes,uint32_t expected_texture_hash);

/* One gather and projected batch, then shared longitudinal edge stepping.
 * No per-command callback, hash, allocation or memset. count is zero on any
 * failure; partial output/scratch must then be discarded. Stats retain the
 * first failing quad. Source view, eye, plan and packs are never modified.
 * Clipped pieces intentionally inherit the current renderer's polygon fan
 * and texture-rectangle remapping; guard attributes do not imply arbitrary
 * VDP1 UV support. Texture/image-space acceptance remains a separate gate.
 * The 256-entry fog table stores exact integer depth thresholds for the
 * source perspective form (pb <= 0). Generation uses division; an eight-step
 * binary lookup does not. Dynamic colour is applied directly each frame. */
bool fzx_road_stream_draw(fzx_road_stream_draw_work_t *,const fzx_view_t *,
    const int32_t eye[3],const RefRoadPlan *,int32_t pa,int32_t pb,unsigned flags,
    fzx_road_stream_command_t *,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *);
#endif
