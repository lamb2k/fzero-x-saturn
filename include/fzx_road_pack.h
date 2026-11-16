/* Immutable source-road storage. Big-endian on disc and in cartridge RAM. */
#ifndef FZX_ROAD_PACK_H
#define FZX_ROAD_PACK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FZX_ROAD_VERTEX, FZX_ROAD_TRIANGLE, FZX_ROAD_CHUNK,
       FZX_ROAD_MATERIAL, FZX_ROAD_TEXTURE, FZX_ROAD_TEXELS,
       FZX_ROAD_SECTIONS };
enum { FZX_ROAD_HEADER_SIZE=128, FZX_ROAD_MAX_BYTES=4*1024*1024,
       FZX_ROAD_FIXTURE=1, FZX_ROAD_OUTER=1 };
typedef struct {
    const uint8_t *data;
    uint32_t bytes, hash, flags, course_hash;
    uint32_t count[FZX_ROAD_SECTIONS], offset[FZX_ROAD_SECTIONS];
} fzx_road_pack_t;

uint16_t fzx_road_u16(const uint8_t *p);
uint32_t fzx_road_u32(const uint8_t *p);
uint32_t fzx_road_hash(const void *data, uint32_t bytes);
/* Hash field at bytes 20..23 is treated as zero. expected_hash binds the
 * runtime image to the receipted pack; zero is never an accepted identity. */
bool fzx_road_pack_open(fzx_road_pack_t *out, const void *data, uint32_t bytes,
                        uint32_t expected_hash, uint32_t expected_course,
                        bool fixture);
const uint8_t *fzx_road_record(const fzx_road_pack_t *pack, unsigned section,
                               uint32_t index);
#endif
