#ifndef FZX_VEHICLE_H
#define FZX_VEHICLE_H
#include "fzx_view16.h"
#include "ref.h"
#if defined(FZX_VEHICLE_NATIVE)&&FZX_VEHICLE_NATIVE
#include "sh2zam/shade_experimental.h"
#endif

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

#if defined(FZX_VEHICLE_NATIVE)&&FZX_VEHICLE_NATIVE
/* Native arm capacities, sized to this vehicle (LOD0: 68 positions, 133
 * normals, 112 faces, 219 static pairs, 22 palettes) with a small margin to
 * keep the high work RAM set small; a larger pack fails to open or faults.
 * The kernels want 4-byte aligned rows. */
enum {FZX_VEHICLE_POSITIONS=80,FZX_VEHICLE_NORMALS=144,
      FZX_VEHICLE_FACES=256,FZX_VEHICLE_PALETTES=32};
enum {FZX_VEHICLE_NATIVE_FACES=120,FZX_VEHICLE_NATIVE_PAIRS=224};
#define FZX_VEHICLE_ALIGN4 _Alignas(4)
#else
enum {FZX_VEHICLE_POSITIONS=128,FZX_VEHICLE_NORMALS=256,
      FZX_VEHICLE_FACES=256,FZX_VEHICLE_PALETTES=64};
#define FZX_VEHICLE_ALIGN4
#endif
typedef struct {
    uint32_t vertices,positions,normals,faces,vertex_at,position_at,normal_at,face_at,order_at,triangles;
} fzx_vehicle_lod_t;
typedef struct {
    const uint8_t *data;
    uint32_t bytes,textures,palette_at,lut_at,palettes;
    fzx_vehicle_lod_t lod[3];
} fzx_vehicle_pack_t;
typedef struct {
#if !(defined(FZX_VEHICLE_NATIVE)&&FZX_VEHICLE_NATIVE)
    sh2z_pos16_t input[FZX_VEHICLE_POSITIONS];
#endif
    sh2z_view_vertex_t projected[FZX_VEHICLE_POSITIONS];
    FZX_VEHICLE_ALIGN4 uint8_t light[FZX_VEHICLE_NORMALS][4];
    uint16_t palette[FZX_VEHICLE_PALETTES][16];
    uint8_t representative[FZX_VEHICLE_PALETTES][3],body[3];
    uint8_t palette_dirty[FZX_VEHICLE_PALETTES];
    unsigned cached_lod;
    bool palette_valid;
#if defined(FZX_VEHICLE_FAST)&&FZX_VEHICLE_FAST&&!(defined(FZX_VEHICLE_NATIVE)&&FZX_VEHICLE_NATIVE)
    /* Per-draw Gouraud memo (FAST): tag is palette+1 for the cached word of a normal. */
    uint8_t gouraud_tag[FZX_VEHICLE_NORMALS],gouraud_saturated[FZX_VEHICLE_NORMALS];
    uint16_t gouraud_value[FZX_VEHICLE_NORMALS];
#endif
#if defined(FZX_VEHICLE_NATIVE)&&FZX_VEHICLE_NATIVE
    /* Saturn-native arm, per draw: the per-LOD tables (owner's placement, see
     * fzx_vehicle_native_tables_t), kept faces, referenced-id marks and
     * ascending id lists, compact pairs, and the Gouraud word of each static pair. */
    struct fzx_vehicle_native_tables *tables;
    uint16_t kept[FZX_VEHICLE_NATIVE_FACES];
    FZX_VEHICLE_ALIGN4 sh2z_shade_pair_t pairs[FZX_VEHICLE_NATIVE_PAIRS];
    uint16_t pair_value[FZX_VEHICLE_NATIVE_PAIRS];
    uint8_t position_seen[FZX_VEHICLE_POSITIONS],normal_seen[FZX_VEHICLE_NORMALS],pair_seen[FZX_VEHICLE_NATIVE_PAIRS];
    uint8_t position_index[FZX_VEHICLE_POSITIONS],normal_index[FZX_VEHICLE_NORMALS],pair_index[FZX_VEHICLE_NATIVE_PAIRS];
#endif
} fzx_vehicle_work_t;
#if defined(FZX_VEHICLE_NATIVE)&&FZX_VEHICLE_NATIVE
/* Per-LOD native tables, built once per LOD from the pack alone (so several
 * works may share them): positions, plane records, the frozen orders
 * (read in place from the pack on big-endian targets), face records, corner
 * position ids, corner ids of the static (palette, normal) pair table, and
 * template command words (valid for template_key). The owner of the work
 * chooses where they live (work->tables); zero them with the work. */
typedef struct fzx_vehicle_native_tables {
    sh2z_pos16_t input[FZX_VEHICLE_POSITIONS];
    sh2z_shade_plane_t planes[FZX_VEHICLE_NATIVE_FACES];
    const uint16_t *orders;
#if !(defined(__BYTE_ORDER__)&&__BYTE_ORDER__==__ORDER_BIG_ENDIAN__)
    uint16_t order_copy[8*FZX_VEHICLE_NATIVE_FACES]; /* octant rows of model->faces ids */
#endif
    uint16_t face_info[FZX_VEHICLE_NATIVE_FACES][4];
    uint32_t face_texture[FZX_VEHICLE_NATIVE_FACES];
    FZX_VEHICLE_ALIGN4 uint8_t corner_position[FZX_VEHICLE_NATIVE_FACES][4];
    FZX_VEHICLE_ALIGN4 uint8_t corner_pair[FZX_VEHICLE_NATIVE_FACES][4];
    FZX_VEHICLE_ALIGN4 sh2z_shade_pair_t pair_table[FZX_VEHICLE_NATIVE_PAIRS];
    uint32_t templates[FZX_VEHICLE_NATIVE_FACES][3]; /* PMOD|COLR, SRCA|SIZE, GRDA|0 */
    uintptr_t template_key[2];
    uint16_t native_lod,native_pairs,template_lod;
} fzx_vehicle_native_tables_t;
#endif
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
#if defined(FZX_VEHICLE_NATIVE)&&FZX_VEHICLE_NATIVE
/* Template emit: 32-byte VDP1 commands written straight into the list as eight
 * 32-bit words, high half first (CTRL|LINK PMOD|COLR SRCA|SIZE XA|YA XB|YB XC|YC
 * XD|YD GRDA|0: the VDP1 layout on the big-endian Saturn). Textured faces copy a
 * prebuilt per-face command and patch the coordinates; their four Gouraud words
 * go to shades + palette_bytes + face*8. Clipped faces patch the solid command's
 * COLR and coordinates. The list must be 4-byte aligned. */
typedef struct {
    uint32_t (*commands)[8];
    unsigned capacity;
    uint8_t *pixels,*shades;           /* VDP1 VRAM: texture bank and shade tables */
    uint32_t palette_bytes,gouraud_bytes;
    uint16_t texture_ctrl,texture_pmod,solid_ctrl,solid_pmod;
} fzx_vehicle_target_t;
bool fzx_vehicle_draw_native(const fzx_vehicle_pack_t *,const fzx_view_t *,const RefCamera *,
                             const RefMachine *,fzx_vehicle_work_t *,const fzx_vehicle_target_t *,
                             uint32_t *,fzx_vehicle_stats_t *);
#endif
#endif
