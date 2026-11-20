/* Baseline for tools/sh2_cycle_model.py: SH2ZAM vlc-2 on the raw 4-bit bytes of a cell, one
 * 256-symbol table, window 7, two bytes per 16-bit store. Not engine code. */
#include "sh2zam/vlc2_experimental.h"

size_t baseline_vlc2_build(uint8_t *t, size_t capacity, const uint8_t *len) {
    return sh2z_vlc2_build(t, capacity, len, 256u, 7u, 0);
}

void baseline_vlc2_bytes(volatile uint16_t *dst, uint32_t words, const uint8_t *stream, const uint8_t *t) {
    sh2z_vlc2_reader_t r;
    sh2z_vlc2_begin(&r, stream, 0u);
    while (words--) {
        const uint32_t a = sh2z_vlc2_decode(&r, t, 7u);
        const uint32_t b = sh2z_vlc2_decode(&r, t, 7u);
        *dst++ = (uint16_t)(a << 8 | b);
    }
}
