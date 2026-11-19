/* SPDX-License-Identifier: GPL-2.0-or-later
 * E1 physics on the SH-2 instruction model: runs phys_bridge.c's big-endian SH-2 build
 * in SH2ZAM's model harness (Yabause's unmodified interpreter, flat RAM, no caches or
 * wait states: MODELLED cycles, not Saturn timing), feeds it the inputs the HOST driver
 * used, and checks the state hash after every tick against the HOST build.
 *
 *   phys_harness bridge.elf bridge.sym PACK DUMP [zones]
 * DUMP is fzx-phys --dump output. Prints one JSON line: ticks, first mismatching tick
 * (or -1), and the per-tick step cost (mean, p50, p95, max cycles and instructions).
 * Built against the SH2ZAM checkout's saturn/benchmarks/sh2-model/harness.c, whose
 * GPL licence this file shares. Copyright (c) 2026 SH2ZAM Contributors. */
#define main sh2_model_main
#include "harness.c"
#undef main

static int cmp_u32(const void* a, const void* b) {
    const u32 x = *(const u32*) a, y = *(const u32*) b;
    return x < y ? -1 : x > y;
}

int main(int argc, char** argv) {
    static u32 cycles[40000], insns[40000];
    FILE* f;
    unsigned char pack[16384];
    size_t size, i;
    char line[256];
    long ticks = 0, mismatch = -1;
    uint64_t total_c = 0, total_i = 0;
    u32 base;
    if (argc < 5) {
        fprintf(stderr, "usage: phys_harness bridge.elf bridge.sym PACK DUMP [zones]\n");
        return 2;
    }
    load_symbols(argv[2]);
    load_elf(argv[1]);
    place_init(argv[1]);
    cpu.model = SHMT_SH2;
    cpu.MappedMemoryReadByte = rb;
    cpu.MappedMemoryReadWord = rw;
    cpu.MappedMemoryReadLong = rl;
    cpu.MappedMemoryWriteByte = wb;
    cpu.MappedMemoryWriteWord = ww;
    cpu.MappedMemoryWriteLong = wl;
    for (i = 0; i < 65536; i++) cpu.opcodes[i] = decode(SHMT_SH2, (u16) i);
    for (i = 0; i < 256; i++) cpu.fetchlist[i] = fetch;
    f = fopen(argv[3], "rb");
    if (!f) { perror(argv[3]); return 2; }
    size = fread(pack, 1, sizeof(pack), f);
    fclose(f);
    base = sym("_vd_pack");
    for (i = 0; i < size; i++) wb(&cpu, base + (u32) i, pack[i]);
    wl(&cpu, sym("_vd_pack_size"), (u32) size);
    wl(&cpu, sym("_vd_zones"), argc > 5 ? (u32) atoi(argv[5]) : 1u);
    if (invoke("_vd_bridge_init", 0, 0, 0, 0).result != 1) fail("pack rejected on the SH-2", 0);
    f = fopen(argv[4], "r");
    if (!f) { perror(argv[4]); return 2; }
    while (fgets(line, sizeof(line), f) && ticks < 40000) {
        unsigned held, pressed, released;
        int sx, sy;
        unsigned long long want;
        u32 in = sym("_vd_in");
        struct cost c;
        uint64_t got;
        if (sscanf(line, "%u %u %u %d %d %llx", &held, &pressed, &released, &sx, &sy, &want) != 6) fail("bad dump line", 0);
        ww(&cpu, in + 0, (u16) held);
        ww(&cpu, in + 2, (u16) pressed);
        ww(&cpu, in + 4, (u16) released);
        wb(&cpu, in + 6, (u8) (int8_t) sx);
        wb(&cpu, in + 7, (u8) (int8_t) sy);
        c = invoke("_vd_bridge_step", 0, 0, 0, 0);
        cycles[ticks] = c.cycles;
        insns[ticks] = (u32) c.instructions;
        total_c += c.cycles;
        total_i += c.instructions;
        invoke("_vd_bridge_hash", 0, 0, 0, 0);
        got = ((uint64_t) rl(&cpu, sym("_vd_hash_hi")) << 32) | rl(&cpu, sym("_vd_hash_lo"));
        if (got != want && mismatch < 0) mismatch = ticks;
        ticks++;
    }
    fclose(f);
    if (ticks == 0) fail("empty dump", 0);
    {
        u32 max_c = 0, max_i = 0;
        long k;
        for (k = 0; k < ticks; k++) {
            if (cycles[k] > max_c) max_c = cycles[k];
            if (insns[k] > max_i) max_i = insns[k];
        }
        qsort(cycles, (size_t) ticks, sizeof(u32), cmp_u32);
        printf("{\"ticks\":%ld,\"first_mismatch\":%ld,\"cycles_mean\":%.1f,\"cycles_p50\":%u,\"cycles_p95\":%u,"
               "\"cycles_max\":%u,\"instructions_mean\":%.1f,\"instructions_max\":%u}\n",
               ticks, mismatch, (double) total_c / ticks, cycles[ticks / 2], cycles[(ticks * 95) / 100], max_c,
               (double) total_i / ticks, max_i);
    }
    return mismatch < 0 ? 0 : 1;
}
