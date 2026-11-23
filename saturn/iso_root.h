#ifndef FZX_ISO_ROOT_H
#define FZX_ISO_ROOT_H
/* The root directory of an ISO 9660 volume, read record by record (no Saturn headers: host-tested by
 * tests/test_iso_root.c). It replaces cdfs_filelist_root_read in the scenes image: that reader dropped the
 * file whose directory record ends exactly on a 2,048-byte sector boundary (no padding follows it), so a
 * root list could silently lose one entry ("TRACK LOAD" 35 at Sector Beta, whose RLOD14.PAK record ended at
 * byte 2,048 of the second root sector once the six decal packs became 24). Records never cross a sector;
 * a zero length byte pads the rest of a sector, and a record that ends at the sector's end has no padding. */
#include <stdint.h>

#define FZX_ISO_SECTOR 2048u

typedef struct {
    uint32_t lba, size;
    uint8_t dir, name_len;       /* name_len: characters before the ';' version suffix */
    char name[13];               /* NUL terminated, at most 12 characters */
} fzx_iso_rec_t;

typedef void (*fzx_iso_read_t)(uint32_t lba, uint8_t *sector, void *ctx);
/* Called for each file or directory record (not '.' or '..'); returns 0 to stop. */
typedef int (*fzx_iso_emit_t)(const fzx_iso_rec_t *rec, void *ctx);

static inline uint32_t fzx_iso_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Walks the root directory named by the primary volume descriptor (sector 16) and emits every entry.
 * `sector` is a 2,048-byte buffer. Returns the number of entries emitted, or -1 for a volume descriptor
 * or a record that is malformed (a name over 12 characters included). */
static inline int fzx_iso_root_walk(fzx_iso_read_t read, void *rctx, uint8_t *sector, fzx_iso_emit_t emit, void *ectx) {
    read(16u, sector, rctx);
    if (sector[0] != 1u || sector[1] != 'C' || sector[2] != 'D' || sector[3] != '0' || sector[4] != '0' || sector[5] != '1') return -1;
    const uint32_t root = fzx_iso_le32(sector + 158u), bytes = fzx_iso_le32(sector + 166u);
    int count = 0;
    for (uint32_t at = 0; at < bytes; at += FZX_ISO_SECTOR) {
        read(root + at / FZX_ISO_SECTOR, sector, rctx);
        uint32_t o = 0;
        while (o < FZX_ISO_SECTOR) {
            const uint32_t len = sector[o];
            if (!len) break;                                      /* padding to the end of the sector */
            if (len < 34u || o + len > FZX_ISO_SECTOR || 33u + sector[o + 32u] > len) return -1;
            const uint8_t nl = sector[o + 32u];
            const uint8_t *nm = sector + o + 33u;
            if (!(nl == 1u && nm[0] <= 1u)) {                     /* skip '.' and '..' */
                fzx_iso_rec_t r;
                uint32_t n = 0;
                r.lba = fzx_iso_le32(sector + o + 2u);
                r.size = fzx_iso_le32(sector + o + 10u);
                r.dir = (sector[o + 25u] & 2u) ? 1u : 0u;
                while (n < nl && nm[n] != ';') ++n;
                if (n > 12u) return -1;
                for (uint32_t i = 0; i < n; ++i) r.name[i] = (char)nm[i];
                r.name[n] = 0;
                r.name_len = (uint8_t)n;
                ++count;
                if (!emit(&r, ectx)) return count;
            }
            o += len;
        }
    }
    return count;
}
#endif
