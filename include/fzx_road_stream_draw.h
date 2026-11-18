#ifndef FZX_ROAD_STREAM_DRAW_H
#define FZX_ROAD_STREAM_DRAW_H
#include "fzx_road_stream.h"
#include "fzx_view16.h"
#include "sh2zam/shade_experimental.h"

/* Experimental C3 command preparation, selected by the default-off native stream.
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

/* Native road layout (opt-in, built once by prepare from the admitted pack).
 * Vertices are renumbered in chunk order by first use, so the chunks of one
 * visible run reference one contiguous host-endian position range: no gather,
 * remap or memset per frame. A chunk may reference vertices from the previous
 * chunk; any older reference (the course wrap) is a duplicate position.
 * Quad records are 8 bytes: chunk-local corner indices in source order, the
 * strip, flags (bit0 outer, bit1 primitive) and the texture corner order
 * (2 bits each: left start, right start, right end, left end). Texture spans
 * are the corners' longitudinal texture coordinates, read only after culling. */
typedef struct { uint8_t v[4],strip,flags,order,reserved; } fzx_road_stream_nquad_t;
typedef struct { int16_t l0,l1,r0,r1; } fzx_road_stream_nspan_t;
typedef struct { uint16_t quad_first,quad_count,vertex_first,vertex_end; } fzx_road_stream_nchunk_t;
_Static_assert(sizeof(fzx_road_stream_nquad_t)==8&&sizeof(fzx_road_stream_nspan_t)==8&&
    sizeof(fzx_road_stream_nchunk_t)==8,"native road record sizes");

typedef struct {
    int32_t boundary[65];
    sh2z_view_vertex_t edge[2][65];
    sh2z_view_guard_vertex_t clip_in[4],clip_out[SH2Z_VIEW_GUARD_MAX_OUT];
    uint32_t end_masks[65];
    uint8_t runs[64];
} fzx_road_stream_hot_t;

/* Caller owns all storage, disjoint from immutable packs and output commands.
 * Zero work initially, then bind the pointers/capacities before prepare().
 * Images need pack.count[IMAGES] entries; remap needs pack.count[VERTICES].
 * Selected native replay requires 600 position/projected slots (observed max600).
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
    /* Per-quad stepping/clipping scratch: local, or bound elsewhere (hot,
     * e.g. faster RAM) before the first draw; NULL selects local. */
    fzx_road_stream_hot_t local,*hot;
    /* Fog (SH2ZAM shade-1 table from sh2z_shade_fog_build, prepared by
     * shade-2's sh2z_shade_fogp_prepare). fogp_ok false selects the shade-1
     * fallback (per-corner lookup, then the batch colour forms). The colour
     * ramps are rebuilt only when their source colours change; the MIX ramp
     * (solid pieces) only on first use after a change. */
    sh2z_shade_fog_t fog;
    sh2z_shade_fogp_t fogp;
    sh2z_shade_ramp_t ramp_offset,ramp_mix;
    uint8_t ramp_key[12];
    bool fogp_ok,ramp_valid,mix_valid;
    /* Optional native layout: bind all four (capacities: npos_capacity
     * positions, pack quads, pack chunks) before prepare to select it. With
     * it, remap/positions are unused and projected holds the visible runs. */
    sh2z_pos16_t *npos;
    fzx_road_stream_nquad_t *nquads;
    fzx_road_stream_nspan_t *nspans;
    fzx_road_stream_nchunk_t *nchunks;
    uint32_t npos_capacity,npos_count;
    bool native;
    uint16_t nunit[256];
} fzx_road_stream_draw_work_t;

/* One VDP1 command table entry (32 bytes, hardware layout). */
typedef struct {
    uint16_t ctrl,link,pmod,colr,srca,size;
    int16_t xa,ya,xb,yb,xc,yc,xd,yd;
    uint16_t grda,reserved;
} fzx_road_stream_vdp1_t;
_Static_assert(sizeof(fzx_road_stream_vdp1_t)==32,"VDP1 command size");
/* Direct VDP1 emit target. templates: fully initialized RGB1555 and CLUT16
 * distorted sprites and the solid polygon (the native adapter's modes).
 * shades receives 4 RGB1555 Gouraud words per command. Offsets are VDP1 VRAM
 * byte offsets (bits 0..18) of the texture bank and of shades[0]; each
 * command's CMDGRDA addresses its own four words. */
typedef struct {
    fzx_road_stream_vdp1_t templates[3];
    fzx_road_stream_vdp1_t *commands;
    uint16_t *shades;
    uint32_t texture_offset,shade_offset;
} fzx_road_stream_emit_t;

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
/* The same draw, written as final VDP1 commands and Gouraud words instead of
 * packets: the bytes the native adapter would produce from the packets. */
bool fzx_road_stream_draw_vdp1(fzx_road_stream_draw_work_t *,const fzx_view_t *,
    const int32_t eye[3],const RefRoadPlan *,int32_t pa,int32_t pb,unsigned flags,
    const fzx_road_stream_emit_t *,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *);
/* The same draws from a prepared centre view (inclusive clip within
 * +/-900): the source-view entries above convert theirs first. */
bool fzx_road_stream_draw_center(fzx_road_stream_draw_work_t *,const sh2z_view_center_t *,
    const RefRoadPlan *,int32_t pa,int32_t pb,unsigned flags,
    fzx_road_stream_command_t *,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *);
bool fzx_road_stream_draw_vdp1_center(fzx_road_stream_draw_work_t *,const sh2z_view_center_t *,
    const RefRoadPlan *,int32_t pa,int32_t pb,unsigned flags,
    const fzx_road_stream_emit_t *,uint32_t capacity,uint32_t *count,
    fzx_road_stream_draw_stats_t *);
#if FZX_SOURCE_CAMERA
#include "ref.h"
/* SH2ZAM view-camera-1 centre view of the source camera (basis, Q20.12 eye,
 * fov), with the centre, depth planes and clip of the source view. */
bool fzx_road_stream_camera_view(sh2z_view_center_t *,const RefCamera *,const fzx_view_t *);
#endif
#endif
