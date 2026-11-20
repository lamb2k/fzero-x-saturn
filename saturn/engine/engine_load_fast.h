/* Engine scenes image (FZX_LOAD_VERIFIED): pack opens without a second hash pass
 * (engine_load_fast.c). */
#ifndef FZX_ENGINE_LOAD_FAST_H
#define FZX_ENGINE_LOAD_FAST_H
#include <stdbool.h>
#include <stdint.h>
#include "engine_path.h"
#include "fzx_road_pack.h"
bool fzx_engine_path_open_verified(fzx_engine_path_t *out, const void *data, uint32_t bytes, uint32_t expected_hash);
bool fzx_engine_path_open_head(fzx_engine_path_t *out, const void *data, uint32_t head_bytes, uint32_t bytes,
                               uint32_t expected_hash);
bool fzx_road_pack_open_verified(fzx_road_pack_t *out, const void *data, uint32_t bytes,
                                 uint32_t expected_hash, uint32_t expected_course, bool fixture);
#endif
