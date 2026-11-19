#include "engine_path.h"
#include <string.h>

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint32_t fzx_engine_fnv(const void *data, uint32_t bytes, uint32_t skip_word) {
    const uint8_t *p = data;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < bytes; ++i) {
        const uint8_t b = (i >> 2) == skip_word ? 0 : p[i];
        h = (h ^ b) * 16777619u;
    }
    return h;
}

bool fzx_engine_path_open(fzx_engine_path_t *out, const void *data, uint32_t bytes, uint32_t expected_hash) {
    const uint8_t *p = data;
    if (!out || !p || ((uintptr_t)p & 3u) || bytes < FZX_EPATH_HEADER_WORDS * 4u || !expected_hash) return false;
    memset(out, 0, sizeof(*out));
#define W(n) be32(p + 4u * (n))
    if (W(0) != FZX_EPATH_MAGIC || W(1) != FZX_EPATH_VERSION || W(2) != bytes || W(3) != expected_hash) return false;
    const uint32_t ticks = W(4), record = W(5), chunks = W(6), anchors = W(29), records = W(30);
    if (!ticks || record != sizeof(fzx_engine_cam_t) || chunks < 3 || chunks > FZX_EPATH_MAX_CHUNKS ||
        W(7) > 8 || (anchors & 3u) || (records & 3u) || anchors < FZX_EPATH_HEADER_WORDS * 4u ||
        anchors > bytes || chunks * 6u > bytes - anchors || records < anchors + chunks * 6u ||
        records > bytes || ticks > (bytes - records) / record || records + ticks * record != bytes) return false;
    if (fzx_engine_fnv(p, bytes, 3) != expected_hash) return false;
    fzx_engine_consts_t *k = &out->consts;
    k->near_plane = (int32_t)W(8); k->far_plane = (int32_t)W(9);
    k->center[0] = (int32_t)W(10); k->center[1] = (int32_t)W(11);
    for (unsigned i = 0; i < 4; ++i) k->clip[i] = (int16_t)W(12 + i);
    k->pa = (int32_t)W(16); k->pb = (int32_t)W(17);
    k->viewport[0] = (int32_t)W(18); k->viewport[1] = (int32_t)W(19);
    k->origin_distance = (int32_t)W(20); k->far_distance = (int32_t)W(21); k->split_distance = (int32_t)W(22);
    k->fog_start = (int32_t)W(23); k->aspect = (int32_t)W(24); k->lateral_sign = (int32_t)W(25);
    k->center_h = (int32_t)W(26); k->center_v = (int32_t)W(27);
    for (unsigned i = 0; i < 4; ++i) k->fog[i] = p[28 * 4 + i];
    k->anchor_shift = W(7);
#undef W
    if (k->near_plane <= 0 || k->far_plane <= k->near_plane || k->viewport[0] <= 0 || k->viewport[1] <= 0 ||
        k->clip[0] >= k->clip[2] || k->clip[1] >= k->clip[3] || k->pb > 0 || k->aspect <= 0 ||
        (k->lateral_sign != 1 && k->lateral_sign != -1) || k->fog_start < 0 || k->fog_start > 996) return false;
    out->cams = (const fzx_engine_cam_t *)(const void *)(p + records);
    out->anchors = (const fzx_engine_anchor_t *)(const void *)(p + anchors);
    out->ticks = ticks; out->chunks = chunks; out->bytes = bytes; out->hash = expected_hash;
    return true;
}
