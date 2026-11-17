#ifndef FZX_MACHINE_MENU_MODEL_H
#define FZX_MACHINE_MENU_MODEL_H

/* Offline format, big endian; never cast packed bytes to native structures.
 * MMODELS.PAK (FZXMM01\0): 64-byte header, 30 x 96-byte descriptors.
 * Header u32[14] after magic: version=1,bytes,models=30,descriptor_offset=64,
 * descriptor_stride=96,motion_offset,motion_count,texture_offset,texture_count,
 * raw_texture_offset,raw_texture_bytes,payload_fnv,reserved=0,reserved=0.
 * Descriptor u32[24]: character,vertex_offset,vertex_count,triangle_offset,
 * triangle_count,material_offset,material_count,binding_offset,binding_count,
 * reflection_vertex_offset,reflection_vertex_count,reflection_triangle_offset,
 * reflection_triangle_count,shadow_vertex_offset,shadow_texture_index,
 * skin_rgb_offset,mini_first_triangle,mini_triangle_count,geometry_flags,
 * reflection_geometry_flags,0,0,0,0. Offsets absolute within MMODELS.PAK.
 * Vertex16: s16 XYZ,u16 source flag,s16 UV,s8 normalXYZ,u8 alpha.
 * Triangle8: u16 vertex[3],material; reflection material is zero.
 * Material16: u16 tile,u8 equation(0=texalpha,1=primitive,2=environment),
 * u8 body_environment,primitiveRGBA[4],environmentRGBA[4],u32 zero.
 * Binding16: u16 texture_index,u8 tile,u8 zero,u16 sampler[6]
 * (s_mode,t_mode,s_mask,t_mask,s_shift,t_shift).
 * SkinRGB: 4 x 3 bytes. Shadow uses four Vertex16 records and source topology
 * (0,1,2),(0,3,1). Texture32: u32 source_segmented_address,
 * format(0=RGBA16,1=I4,2=IA4),width,
 * height,absolute_raw_offset,raw_bytes,raw_fnv,zero; raw source bytes retained.
 * Motion64: s32 Q16.16 row-vector model matrix[4][4]. Index0 static scale1;
 * 1..209 select scale1 (.03 source step),210..628 settings scale1 (.015).
 * Consumer applies select scale0.1, settings scale1, outline scale1.05.
 * Camera matrices are a separate source camera closure; clip16..8192.
 *
 * MMINI.PAK / MSccs.PAK (FZXMC01\0): header64, descriptor16 x triangles,
 * then8-aligned VRAM blob. Headeru32: version,bytes,count,descriptor_offset=64,
 * blob_offset,blob_bytes,payload_fnv,width,height,character(30=all),skin,0,0,0.
 * Descriptor u32: pixel_offset,palette_offset,mode(4/16),reserved=0. Offsets
 * relative to blob; palette_offset=UINT32_MAX for RGB16. Pixels CLUT4 high
 * nibble first or RGB1555 BE. CLUT is16RGB1555 words, unique pertriangle so
 * native lighting may mutate VRAM palettes from the immutable CPU bank.
 * CLUT4 pixel spans may be shared; RGB16 pixel spans are unique pertriangle
 * because native lighting mutates them. Allcolors exactRGB5
 * samples; >16unique colors use RGB fallback. Minis use8x2 skin0; settings
 * use16x16 and sourcecharacter cc00..29 / skin s0..3. Triangle order exact.
 * Source textures are point-sampled through original UV/wrap/mirror state;
 * chart resolution and triangle rectangular mapping are explicit adaptations.
 *
 * MEFFECT.PAK (FZXME01\0): header64 +30 descriptors16 +VRAMblob.
 * Headeru32 aftermagic: version=1,bytes,count=30,descriptor_offset=64,
 * blob_offset=544,blob_bytes,payload_fnv,reflection_pixel_offset,16,16,
 * shadow_palette_offset=0,32,64,reserved=0. Bloboffsets are relative toblob.
 * Descriptoru32: shadow_pixel_offset,MMODELS_source_texture_index,0,0.
 * Shadowpixels are1024bytes I4 highnibblefirst, offline sampled through each
 * originalquad's UV/0xffff scale/mirror|clamp into boundary0,3,1,2. Original
 * untouchedtexels remain inMMODELS. SharedCLUT32bytes
 * has index0 transparent and1..15RGB1555 black. Native halftransparency is
 * an opacity adaptation. Reflection512bytes are exact originalRGB5 converted
 * toRGB1555 validpixels; source reflection uses primitivealpha60,scale2600.
 * Keep only selectedshadow1024+palette32+reflection512 inVRAM(1568bytes).
 */
#define FZX_MACHINE_MODEL_HEADER 64u
#define FZX_MACHINE_MODEL_DESCRIPTOR 96u
#define FZX_MACHINE_MODEL_VERTEX 16u
#define FZX_MACHINE_MODEL_TRIANGLE 8u
#define FZX_MACHINE_MODEL_MATERIAL 16u
#define FZX_MACHINE_MODEL_BINDING 16u
#define FZX_MACHINE_MODEL_TEXTURE 32u
#define FZX_MACHINE_MODEL_MOTION 64u
#define FZX_MACHINE_MODEL_COUNT 30u
#define FZX_MACHINE_MODEL_SELECT_FRAMES 209u
#define FZX_MACHINE_MODEL_SETTINGS_FRAMES 419u
#define FZX_MACHINE_MODEL_MOTION_COUNT 629u
#define FZX_MACHINE_CHART_DESCRIPTOR 16u

#include <stdbool.h>
#include <stdint.h>
#include "fzx_machine_menu.h"
#include <sh2zam/view_experimental.h>

#define FZX_MM_MAX_VERTICES 263u
#define FZX_MM_MAX_TRIANGLES 132u
typedef struct { const uint8_t *data; uint32_t bytes,motion_offset; } fzx_mm_pack_t;
typedef struct {
    const uint8_t *data,*blob;
    uint32_t bytes,count,blob_bytes;
    uint8_t width,height,character,skin;
} fzx_mm_charts_t;
enum { FZX_MM_BASE, FZX_MM_OUTLINE, FZX_MM_SHADOW, FZX_MM_REFLECTION };
typedef struct {
    int16_t x[4],y[4];
    uint16_t triangle;
    uint8_t kind,character;
    /* Source diffuse RGB, or source reflection texture RGB at each vertex.
     * Native palettes use the mean and Gouraud differences. */
    uint8_t shade[4][3];
} fzx_mm_quad_t;
typedef bool (*fzx_mm_emit_t)(void *,const fzx_mm_quad_t *);
typedef struct {
    sh2z_pos16_t positions[FZX_MM_MAX_VERTICES];
    sh2z_view_vertex_t projected[FZX_MM_MAX_VERTICES];
    uint8_t shade[FZX_MM_MAX_VERTICES][3];
    uint16_t order[FZX_MM_MAX_TRIANGLES];
    int32_t depth[FZX_MM_MAX_TRIANGLES];
} fzx_mm_work_t;
bool fzx_mm_open(fzx_mm_pack_t *,const void *,uint32_t,uint32_t expected_hash);
bool fzx_mm_charts_open(fzx_mm_charts_t *,const void *,uint32_t,uint32_t expected_hash,
                        const fzx_mm_pack_t *,unsigned screen,unsigned character,unsigned skin);
/* camera: two source row-vector matrices + projection XX/YY, Q16.16.
 * slot_to_character: original 30-slot permutation exported by the converter.
 * reflection: 16x16 RGB1555 big-endian original image (512 bytes).
 * No allocations or native globals. Painter ordering is local to each model. */
bool fzx_mm_render(const fzx_mm_pack_t *,const fzx_mm_charts_t *,
                   const int32_t camera[2][18],const uint8_t slot_to_character[30],
                   const uint8_t reflection[512],const fzx_machine_menu_snapshot_t *,
                   fzx_mm_work_t *,fzx_mm_emit_t,void *,unsigned *commands);
/* Exposed to test the exact same quantized camera used by native drawing. */
bool fzx_mm_view(const fzx_mm_pack_t *,const int32_t camera[18],unsigned motion,
                  int32_t scale_q16,int center_x,int center_y,sh2z_view_t *,sh2z_mat34_t *model);
#endif
