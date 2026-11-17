#ifndef FZX_ROAD_STREAM_H
#define FZX_ROAD_STREAM_H
#include "fzx_road_pack.h"
#include "road_plan.h"
#include "sh2zam/view_experimental.h"

enum {FZX_RLD_VERTICES,FZX_RLD_QUADS,FZX_RLD_REPEATS,FZX_RLD_CHUNKS,
      FZX_RLD_STRIPS,FZX_RLD_IMAGES,FZX_RLD_FAR2,FZX_RLD_FAR4,FZX_RLD_SECTIONS};
typedef struct {
    const uint8_t *data;
    uint32_t bytes,texture_bytes;
    uint32_t count[FZX_RLD_SECTIONS],offset[FZX_RLD_SECTIONS];
    uint8_t base_fog[3];
} fzx_road_stream_pack_t;

/* Boot-only, allocation-free admission of the compact C2 pack. expected_hash
 * is the complete-file FNV from the generated identity, not the embedded
 * zero-field checksum. The texture loader must separately verify the complete
 * texture-file FNV from that same identity before drawing. Structural checks
 * bound all records and image references. Far records are retained but are
 * not rendering permission; the offline receipt owns their chord proof.
 * Failure leaves out unchanged. Metadata must remain immutable and resident. */
bool fzx_road_stream_open(fzx_road_stream_pack_t *out,const void *metadata,
                          uint32_t bytes,uint32_t expected_hash,uint32_t texture_bytes);
const uint8_t *fzx_road_stream_record(const fzx_road_stream_pack_t *pack,
                                     unsigned section,uint32_t index);
/* Gather the exact original vertices used by the source's current road plan.
 * remap has at least vertex_count entries, set to UINT16_MAX for unused rows.
 * positions[0..count) is host-endian, directly consumable by sh2z_view_batch.
 * Buffers are caller-owned and disjoint; no allocations or transforms occur.
 * On failure count is zero; partial scratch contents must not be consumed. */
bool fzx_road_stream_gather(const fzx_road_stream_pack_t *pack,const RefRoadPlan *plan,
                            uint16_t *remap,uint32_t remap_count,sh2z_pos16_t *positions,
                            uint32_t capacity,uint32_t *count);
#endif
