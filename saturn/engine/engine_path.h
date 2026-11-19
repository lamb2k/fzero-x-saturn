/* Engine line, E0: the baked camera path and course constants (ENGPATH.PAK,
 * written by tools/engine_path.py from the owner's ROM at asset-build time;
 * never committed). Big-endian 32-bit words, so the SH-2 uses it in place. */
#ifndef FZX_ENGINE_PATH_H
#define FZX_ENGINE_PATH_H
#include <stdbool.h>
#include <stdint.h>
#include "engine_scene.h"

#define FZX_EPATH_MAGIC 0x45505448u /* "EPTH" */
enum { FZX_EPATH_VERSION = 1, FZX_EPATH_HEADER_WORDS = 40, FZX_EPATH_MAX_CHUNKS = 512 };
/* Header words: 0 magic, 1 version, 2 bytes, 3 FNV-1a of the file with this
 * word zero, 4 ticks, 5 record bytes, 6 chunks, 7 anchor shift, 8 near, 9 far,
 * 10-11 centre, 12-15 clip, 16 pa, 17 pb, 18-19 viewport, 20 origin distance,
 * 21 far distance, 22 split, 23 fog start, 24 aspect, 25 lateral sign,
 * 26-27 sky centre, 28 fog RGBA, 29 anchors offset, 30 records offset. */

typedef struct { int16_t x, y, z; } fzx_engine_anchor_t;

typedef struct {
    const fzx_engine_cam_t *cams;       /* ticks records; cams[t-1] is logic tick t */
    const fzx_engine_anchor_t *anchors; /* chunks anchors, ring order */
    uint32_t ticks, chunks, bytes, hash;
    fzx_engine_consts_t consts;
} fzx_engine_path_t;

uint32_t fzx_engine_fnv(const void *data, uint32_t bytes, uint32_t skip_word);
/* Validates structure, bounds and the hash. expected_hash 0 is never accepted. */
bool fzx_engine_path_open(fzx_engine_path_t *out, const void *data, uint32_t bytes, uint32_t expected_hash);
#endif
