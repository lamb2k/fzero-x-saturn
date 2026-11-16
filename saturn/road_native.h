#ifndef FZX_ROAD_NATIVE_H
#define FZX_ROAD_NATIVE_H
#include "fzx_road_pack.h"
/* Boot only. No disc access or allocation occurs in the race loop. */
bool fzx_road_load(const void *course,uint32_t course_bytes);
extern fzx_road_pack_t fzx_native_road;
typedef struct {
    uint32_t status,cart_id,bytes,hash,sectors,vertices,triangles,chunks;
    int32_t io_result;
    uint32_t volume_word;
} fzx_road_load_status_t;
extern volatile fzx_road_load_status_t fzx_road_load_status;
#endif
