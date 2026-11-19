/* SH-2 side of the E1 physics bit-exact check and cost measurement: the same glue and
 * SH2ZAM vehicle-dynamics-1 code as the HOST driver, built big-endian for the SH-2 and
 * run instruction by instruction in the SH2ZAM model harness (phys_harness.c).
 * No startup code: the harness writes the pack and each tick's input into RAM and calls
 * these functions. MIT; see ../../LICENSE. */
#include <stddef.h>
#include <stdint.h>

#include "physics_native.h"

uint8_t vd_pack[16384];
uint32_t vd_pack_size;
int32_t vd_zones = 1;
sh2z_vd_input_t vd_in;
uint32_t vd_hash_hi = 0xCBF29CE4u, vd_hash_lo = 0x84222325u;

static FzxPhysCourse course;
static sh2z_vd_body_t body;

/* Freestanding build: the glue's memset and the struct copies need these. */
void* memset(void* d, int c, size_t n) {
    unsigned char* p = d;
    while (n--) *p++ = (unsigned char) c;
    return d;
}

void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* p = d;
    const unsigned char* q = s;
    while (n--) *p++ = *q++;
    return d;
}

int vd_bridge_init(void) {
    if (fzx_phys_load(&course, vd_pack, vd_pack_size, vd_zones) != 0) return 0;
    body = course.start;
    vd_hash_hi = 0xCBF29CE4u;
    vd_hash_lo = 0x84222325u;
    return 1;
}

/* One model tick with vd_in: the cost being measured. */
int vd_bridge_step(void) {
    sh2z_vd_step(&body, &course.track, &course.vehicle, &fzx_phys_rules, &vd_in);
    return 1;
}

/* FNV-1a 64 over the body's words in big-endian byte order, as the HOST driver does. */
int vd_bridge_hash(void) {
    const uint32_t* w = (const uint32_t*) (const void*) &body;
    uint64_t h = ((uint64_t) vd_hash_hi << 32) | vd_hash_lo;
    size_t i;
    for (i = 0; i < sizeof(body) / 4; i++) {
        int k;
        for (k = 24; k >= 0; k -= 8) h = (h ^ ((w[i] >> k) & 0xFFu)) * 0x100000001B3ull;
    }
    vd_hash_hi = (uint32_t) (h >> 32);
    vd_hash_lo = (uint32_t) h;
    return 1;
}
