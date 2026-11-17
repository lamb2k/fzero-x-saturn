#ifndef FZX_VEHICLE_H
#define FZX_VEHICLE_H
#include "fzx_view16.h"
#include "ref.h"

/* C4 model-specific preparation. Original model X/Y/Z map to source basis
 * Z/Y/X. No simulation state is modified. The view is consumed by SH2ZAM. */
typedef struct {
    sh2z_view_t view;
    int32_t eye[3];
    int16_t basis[3][3]; /* local-to-world, Q1.14, without model scale */
    uint16_t eye_error; /* conservative units per component for plane culling */
    uint8_t octant;
    bool cull_valid;
} fzx_vehicle_pose_t;

bool fzx_vehicle_pose(fzx_vehicle_pose_t *,const fzx_view_t *,
                     const RefCamera *,const RefMachine *);

enum {FZX_VEHICLE_POSITIONS=128,FZX_VEHICLE_NORMALS=256,
      FZX_VEHICLE_FACES=256,FZX_VEHICLE_PALETTES=64};
typedef struct {
    uint32_t vertices,positions,normals,faces,vertex_at,position_at,normal_at,face_at,order_at,triangles;
} fzx_vehicle_lod_t;
typedef struct {
    const uint8_t *data;
    uint32_t bytes,textures,palette_at,lut_at,palettes;
    fzx_vehicle_lod_t lod[3];
} fzx_vehicle_pack_t;
typedef struct {
    sh2z_pos16_t input[FZX_VEHICLE_POSITIONS];
    sh2z_view_vertex_t projected[FZX_VEHICLE_POSITIONS];
    uint8_t light[FZX_VEHICLE_NORMALS][4];
    uint16_t palette[FZX_VEHICLE_PALETTES][16];
    uint8_t representative[FZX_VEHICLE_PALETTES][3],body[3];
    uint8_t palette_dirty[FZX_VEHICLE_PALETTES];
    unsigned cached_lod;
    bool palette_valid;
} fzx_vehicle_work_t;
typedef struct {
    int16_t x[4],y[4];
    uint16_t gouraud[4],palette,face,width,height,color;
    uint32_t texture;
    bool solid;
} fzx_vehicle_quad_t;
typedef struct {
    uint32_t draws,culled,clipped,saturated,positions,normals,palette_updates,lod,fault;
} fzx_vehicle_stats_t;
typedef bool (*fzx_vehicle_emit_fn)(void *,const fzx_vehicle_quad_t *);
bool fzx_vehicle_pack_open(fzx_vehicle_pack_t *,const void *,uint32_t,uint32_t,uint32_t,bool);
/* Work must initially be zero, and be reset whenever the immutable pack changes. */
bool fzx_vehicle_draw(const fzx_vehicle_pack_t *,const fzx_view_t *,const RefCamera *,
                      const RefMachine *,fzx_vehicle_work_t *,unsigned,
                      fzx_vehicle_emit_fn,void *,fzx_vehicle_stats_t *);
#endif
