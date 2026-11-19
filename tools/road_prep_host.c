/* Offline track preparation (scenes lane): runs the engine's native-layout prepare
 * (src/fzx_road_stream_draw.c, fzx_road_stream_draw_prepare) on the admitted road packs
 * at build time and writes its result as ROADPREP.PAK, in the SH-2's in-memory layout
 * (big-endian, the same record sizes), so the image reads each table straight into its
 * work array and skips the prepare at load. HOST tier; private stage only: the inputs
 * are the ROM-derived road packs. Built and run by tools/road_prep.py.
 *
 *   road_prep_host ROADLOD.PAK ROADLTX.PAK OUT.PAK
 *
 * OUT (big-endian): "FZXRPP01", u32 version 1, u32 file bytes, u32 ROADLOD FNV,
 * u32 ROADLTX FNV, u32 npos_count, u32 quads, u32 chunks, u32 images, u32 strips, then six
 * (u32 offset, u32 bytes) for nunit, npos, nquads, nspans, nchunks, images; then the
 * tables, each 4-aligned: nunit as u16[strips], npos as 3 x i16 per position, nquads as
 * 8 bytes per quad, nspans as 4 x i16, nchunks as 4 x u16, images as 3 x (u32 pixels, u32
 * palette, u16 width, u16 height, u16 mode, u16 reserved) then u8 mean[3], u8 reserved. */
#include "fzx_road_stream_draw.h"
#include "road_lod_identity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { VERTICES = 800, NATIVE_COPIES = 256 };

static uint8_t *read_file(const char *path, uint32_t *bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    const long size = ftell(f);
    if (size <= 0 || size > 16000000) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *data = malloc((size_t)size + 64u);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(data); return NULL; }
    fclose(f);
    *bytes = (uint32_t)size;
    return data;
}
static uint32_t fnv(const uint8_t *p, uint32_t n) {
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
typedef struct { uint8_t *b; uint32_t n, cap; } buf_t;
static void put(buf_t *o, const void *p, uint32_t n) {
    if (o->n + n > o->cap) { o->cap = (o->n + n) * 2u; o->b = realloc(o->b, o->cap); }
    memcpy(o->b + o->n, p, n); o->n += n;
}
static void u8(buf_t *o, uint8_t v) { put(o, &v, 1); }
static void u16(buf_t *o, uint16_t v) { const uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; put(o, b, 2); }
static void u32(buf_t *o, uint32_t v) {
    const uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    put(o, b, 4);
}
static void align4(buf_t *o) { while (o->n & 3u) u8(o, 0); }
static void set32(buf_t *o, uint32_t at, uint32_t v) {
    o->b[at] = (uint8_t)(v >> 24); o->b[at + 1] = (uint8_t)(v >> 16); o->b[at + 2] = (uint8_t)(v >> 8); o->b[at + 3] = (uint8_t)v;
}
#define FAIL(...) do { fprintf(stderr, "road_prep_host: " __VA_ARGS__); fputc('\n', stderr); return 1; } while (0)

int main(int argc, char **argv) {
    if (argc != 4) FAIL("usage: road_prep_host ROADLOD.PAK ROADLTX.PAK OUT.PAK");
    uint32_t lod_bytes, ltx_bytes;
    uint8_t *lod = read_file(argv[1], &lod_bytes), *ltx = read_file(argv[2], &ltx_bytes);
    if (!lod || !ltx) FAIL("cannot read the packs");
    if (lod_bytes != FZX_ROAD_LOD_BYTES || fnv(lod, lod_bytes) != FZX_ROAD_LOD_FILE_HASH) FAIL("ROADLOD.PAK is not the identity's");
    if (ltx_bytes != FZX_ROAD_LOD_TEXTURE_BYTES || fnv(ltx, ltx_bytes) != FZX_ROAD_LOD_TEXTURE_FILE_HASH) FAIL("ROADLTX.PAK is not the identity's");
    static fzx_road_stream_pack_t pack;
    if (!fzx_road_stream_open(&pack, lod, lod_bytes, FZX_ROAD_LOD_FILE_HASH, FZX_ROAD_LOD_TEXTURE_BYTES)) FAIL("road pack refused");
    const uint32_t nv = pack.count[FZX_RLD_VERTICES], nq = pack.count[FZX_RLD_QUADS], nc = pack.count[FZX_RLD_CHUNKS];
    const uint32_t ni = pack.count[FZX_RLD_IMAGES], ns = pack.count[FZX_RLD_STRIPS];
    static fzx_road_stream_draw_work_t w;
    memset(&w, 0, sizeof(w));
    w.remap = calloc(nv, sizeof(*w.remap)); w.remap_capacity = nv;
    w.positions = calloc(VERTICES, sizeof(*w.positions)); w.vertex_capacity = VERTICES;
    w.projected = calloc(VERTICES, sizeof(*w.projected));
    w.images = calloc(ni, sizeof(*w.images)); w.image_capacity = ni;
    w.npos_capacity = nv + NATIVE_COPIES;
    w.npos = calloc(w.npos_capacity, sizeof(*w.npos));
    w.nquads = calloc(nq, sizeof(*w.nquads));
    w.nspans = calloc(nq, sizeof(*w.nspans));
    w.nchunks = calloc(nc, sizeof(*w.nchunks));
    if (!w.remap || !w.positions || !w.projected || !w.images || !w.npos || !w.nquads || !w.nspans || !w.nchunks) FAIL("no memory");
    if (!fzx_road_stream_draw_prepare(&w, &pack, ltx, ltx_bytes, FZX_ROAD_LOD_TEXTURE_FILE_HASH) || !w.native)
        FAIL("prepare refused the packs");
    buf_t o = { 0 };
    put(&o, "FZXRPP01", 8);
    u32(&o, 1); u32(&o, 0); u32(&o, FZX_ROAD_LOD_FILE_HASH); u32(&o, FZX_ROAD_LOD_TEXTURE_FILE_HASH);
    u32(&o, w.npos_count); u32(&o, nq); u32(&o, nc); u32(&o, ni); u32(&o, ns);
    const uint32_t dir = o.n;
    for (unsigned i = 0; i < 12u; ++i) u32(&o, 0);
    uint32_t at[6], len[6];
    at[0] = o.n; for (uint32_t s = 0; s < ns; ++s) u16(&o, w.nunit[s]); len[0] = o.n - at[0]; align4(&o);
    at[1] = o.n; for (uint32_t i = 0; i < w.npos_count; ++i) { u16(&o, (uint16_t)w.npos[i].x); u16(&o, (uint16_t)w.npos[i].y); u16(&o, (uint16_t)w.npos[i].z); }
    len[1] = o.n - at[1]; align4(&o);
    at[2] = o.n; for (uint32_t i = 0; i < nq; ++i) {
        const fzx_road_stream_nquad_t *q = &w.nquads[i];
        for (unsigned j = 0; j < 4; ++j) u8(&o, q->v[j]);
        u8(&o, q->strip); u8(&o, q->flags); u8(&o, q->order); u8(&o, q->reserved);
    }
    len[2] = o.n - at[2]; align4(&o);
    at[3] = o.n; for (uint32_t i = 0; i < nq; ++i) {
        const fzx_road_stream_nspan_t *s = &w.nspans[i];
        u16(&o, (uint16_t)s->l0); u16(&o, (uint16_t)s->l1); u16(&o, (uint16_t)s->r0); u16(&o, (uint16_t)s->r1);
    }
    len[3] = o.n - at[3]; align4(&o);
    at[4] = o.n; for (uint32_t i = 0; i < nc; ++i) {
        const fzx_road_stream_nchunk_t *c = &w.nchunks[i];
        u16(&o, c->quad_first); u16(&o, c->quad_count); u16(&o, c->vertex_first); u16(&o, c->vertex_end);
    }
    len[4] = o.n - at[4]; align4(&o);
    at[5] = o.n; for (uint32_t i = 0; i < ni; ++i) {
        const fzx_road_stream_image_t *m = &w.images[i];
        for (unsigned v = 0; v < 3; ++v) {
            u32(&o, m->variant[v].pixels); u32(&o, m->variant[v].palette);
            u16(&o, m->variant[v].width); u16(&o, m->variant[v].height); u16(&o, m->variant[v].mode); u16(&o, m->variant[v].reserved);
        }
        u8(&o, m->mean[0]); u8(&o, m->mean[1]); u8(&o, m->mean[2]); u8(&o, m->reserved);
    }
    len[5] = o.n - at[5]; align4(&o);
    for (unsigned i = 0; i < 6u; ++i) { set32(&o, dir + 8u * i, at[i]); set32(&o, dir + 8u * i + 4u, len[i]); }
    set32(&o, 12, o.n);
    FILE *f = fopen(argv[3], "wb");
    if (!f || fwrite(o.b, 1, o.n, f) != o.n) FAIL("cannot write %s", argv[3]);
    fclose(f);
    printf("road prep: %u positions, %u quads, %u chunks, %u images, %u strips, %u bytes\n",
           (unsigned)w.npos_count, (unsigned)nq, (unsigned)nc, (unsigned)ni, (unsigned)ns, (unsigned)o.n);
    return 0;
}
