/* Engine scenes image (FZX_LOAD_VERIFIED): pack opens without a second pass over bytes
 * the CD reader has already checked as it read them.
 *
 * engine_path.c and src/fzx_road_pack.c are pinned by their asset receipts
 * (engine_path.receipt, road_pack.receipt), so their opens stay as they are for every
 * other image. These are the same structural checks, in the same order, minus the
 * whole-file FNV pass (engine path) and the whole-file and per-texture FNV passes (road
 * pack): the scenes image's readers compute the file hash while the sectors arrive
 * (texture_cd.c, road_native.c) and refuse the pack before these run. Keep each in step
 * with its original. */
#include <string.h>
#include "engine_path.h"
#include "fzx_road_pack.h"

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* fzx_engine_path_open (engine_path.c) without the FNV pass. */
bool fzx_engine_path_open_verified(fzx_engine_path_t *out, const void *data, uint32_t bytes, uint32_t expected_hash) {
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

/* fzx_road_pack_open (src/fzx_road_pack.c) without the FNV passes. */
static const uint32_t strides[FZX_ROAD_SECTIONS] = { 16, 12, 64, 80, 16, 1 };
static bool zero(const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) if (p[i]) return false;
    return true;
}
bool fzx_road_pack_open_verified(fzx_road_pack_t *out, const void *data, uint32_t bytes,
                                 uint32_t expected_hash, uint32_t expected_course, bool fixture) {
    const uint8_t *p = data;
    fzx_road_pack_t pack;
    uint32_t end = FZX_ROAD_HEADER_SIZE, triangle_end = 0, texel_end = 0;
    static const uint32_t maximum[] = { 65535, 65535, 1024, 256, 64, FZX_ROAD_MAX_BYTES };
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!p || bytes < FZX_ROAD_HEADER_SIZE || bytes > FZX_ROAD_MAX_BYTES ||
        !expected_hash || !expected_course || memcmp(p, "FZXROAD1", 8) ||
        fzx_road_u32(p + 8) != 1 || fzx_road_u32(p + 12) != bytes ||
        fzx_road_u32(p + 16) != (fixture ? FZX_ROAD_FIXTURE : 0u) ||
        fzx_road_u32(p + 20) != expected_hash || fzx_road_u32(p + 104) != expected_course ||
        !zero(p + 108, 20)) return false;
    memset(&pack, 0, sizeof(pack));
    pack.data = p; pack.bytes = bytes; pack.hash = expected_hash;
    pack.flags = fzx_road_u32(p + 16); pack.course_hash = expected_course;
    for (unsigned s = 0; s < FZX_ROAD_SECTIONS; ++s) {
        const uint32_t n = fzx_road_u32(p + 24 + s * 4), at = fzx_road_u32(p + 48 + s * 4);
        if (!n || n > maximum[s] || at != end || n > (bytes - end) / strides[s]) return false;
        pack.count[s] = n; pack.offset[s] = at;
        end += n * strides[s];
        const uint32_t aligned = (end + 3u) & ~3u;
        if (aligned > bytes || !zero(p + end, aligned - end)) return false;
        end = aligned;
    }
    if (end != bytes) return false;
    for (uint32_t i = 0; i < pack.count[FZX_ROAD_VERTEX]; ++i) {
        const uint8_t *v = fzx_road_record(&pack, FZX_ROAD_VERTEX, i);
        if (!zero(v + 14, 2)) return false;
    }
    for (uint32_t i = 0; i < pack.count[FZX_ROAD_CHUNK]; ++i) {
        const uint8_t *c = fzx_road_record(&pack, FZX_ROAD_CHUNK, i);
        const uint32_t first = fzx_road_u32(c + 8), n = fzx_road_u32(c + 12);
        if (fzx_road_u16(c + 4) >= 64 || fzx_road_u16(c + 6) || first != triangle_end ||
            n > pack.count[FZX_ROAD_TRIANGLE] - triangle_end) return false;
        triangle_end += n;
        for (uint32_t t = first; t < triangle_end; ++t) {
            const uint8_t *tri = fzx_road_record(&pack, FZX_ROAD_TRIANGLE, t);
            for (unsigned k = 0; k < 3; ++k)
                if (fzx_road_u16(tri + 2 * k) >= pack.count[FZX_ROAD_VERTEX]) return false;
            if (fzx_road_u16(tri + 6) >= pack.count[FZX_ROAD_MATERIAL] ||
                fzx_road_u16(tri + 8) != i || (fzx_road_u16(tri + 10) & ~FZX_ROAD_OUTER)) return false;
        }
    }
    if (triangle_end != pack.count[FZX_ROAD_TRIANGLE]) return false;
    for (uint32_t i = 0; i < pack.count[FZX_ROAD_MATERIAL]; ++i) {
        const uint8_t *m = fzx_road_record(&pack, FZX_ROAD_MATERIAL, i);
        if (fzx_road_u16(m + 40) >= pack.count[FZX_ROAD_TEXTURE] || !zero(m + 68, 12)) return false;
        const uint8_t *t = fzx_road_record(&pack, FZX_ROAD_TEXTURE, fzx_road_u16(m + 40));
        if (fzx_road_u16(m + 42) != fzx_road_u16(t + 8) || fzx_road_u16(m + 44) != fzx_road_u16(t + 10) ||
            fzx_road_u16(m + 46) != 1 || fzx_road_u16(m + 52) > 7 || fzx_road_u16(m + 54)) return false;
    }
    for (uint32_t i = 0; i < pack.count[FZX_ROAD_TEXTURE]; ++i) {
        const uint8_t *t = fzx_road_record(&pack, FZX_ROAD_TEXTURE, i);
        const uint32_t at = fzx_road_u32(t), n = fzx_road_u32(t + 4);
        const uint32_t w = fzx_road_u16(t + 8), h = fzx_road_u16(t + 10);
        if (!w || !h || w > 128 || h > 64 || n != w * h * 2u || at != texel_end ||
            n > pack.count[FZX_ROAD_TEXELS] - texel_end) return false;
        texel_end += n;
    }
    if (texel_end != pack.count[FZX_ROAD_TEXELS]) return false;
    *out = pack;
    return true;
}
