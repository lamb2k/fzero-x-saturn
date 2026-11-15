/* G1.3 SH-2 op microbenchmark: cycles per operation for libgcc soft-float binary32 and SH2ZAM
 * Q16.16, with operands in high work RAM and in the 4 MB cartridge. MIT; see ../../LICENSE.
 *
 * Each case runs N calls over operand tables and is timed with the free-running timer (master
 * clock / 8). A copy loop over the same tables is subtracted, so a figure is the cost of the
 * operation and its call, not of the loads and stores. Results go to `fzx_bench` (read it over
 * MCP) and the screen. Emulator timings are MODELLED; the console run is the owner's. */
#include <yaul.h>

#include <stdint.h>
#include <string.h>

#include <sh2zam/sh2zam.h>

#define N 256
#define CART_BASE ((volatile uint8_t*) 0x22400000) /* 4 MB DRAM cartridge, cache-through */

float fzx_soft_sqrtf(float x);

enum { K_COPY, K_FADD, K_FSUB, K_FMUL, K_FDIV, K_FCMP, K_FTOI, K_ITOF, K_FSQRT,
       K_XADD, K_XMUL, K_XDIV, K_XSQRT, K_COUNT };
static const char* const kNames[K_COUNT] = { "copy", "f add", "f sub", "f mul", "f div", "f cmp", "f->i",
                                             "i->f", "f sqrt", "x add", "x mul", "x div", "x sqrt" };

typedef struct {
    uint32_t magic;          /* 'FZXB' when done */
    uint32_t n;              /* calls per case */
    uint32_t hw[K_COUNT];    /* FRT/8 counts, operands in high work RAM */
    uint32_t cart[K_COUNT];  /* FRT/8 counts, operands in the cartridge (0 if absent) */
    uint32_t cart_present;
} Bench;

Bench fzx_bench;

static float fa_hw[N], fb_hw[N], fo_hw[N];
static int32_t xa_hw[N], xb_hw[N], xo_hw[N];

static uint32_t rng = 0x12345678u;
static uint32_t next(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

/* Race-like magnitudes: floats in about +-0.01 .. +-5000, never zero (no fast paths). */
static float rand_float(void) {
    uint32_t r = next();
    float m = 1.0f + (float) (r & 0xFFFF) / 65536.0f;
    int e = (int) ((r >> 16) % 20) - 7;
    float v = m;
    while (e > 0) { v *= 2.0f; e--; }
    while (e < 0) { v *= 0.5f; e++; }
    return (r >> 31) ? -v : v;
}

static uint32_t run(int kind, const float* fa, const float* fb, float* fo, const int32_t* xa, const int32_t* xb,
                    int32_t* xo) {
    int i;
    uint32_t t;
    cpu_frt_count_set(0);
    switch (kind) {
    case K_COPY: for (i = 0; i < N; i++) fo[i] = fa[i]; break;
    case K_FADD: for (i = 0; i < N; i++) fo[i] = fa[i] + fb[i]; break;
    case K_FSUB: for (i = 0; i < N; i++) fo[i] = fa[i] - fb[i]; break;
    case K_FMUL: for (i = 0; i < N; i++) fo[i] = fa[i] * fb[i]; break;
    case K_FDIV: for (i = 0; i < N; i++) fo[i] = fa[i] / fb[i]; break;
    case K_FCMP: for (i = 0; i < N; i++) fo[i] = (fa[i] < fb[i]) ? fa[i] : fb[i]; break;
    case K_FTOI: for (i = 0; i < N; i++) xo[i] = (int32_t) fa[i]; break;
    case K_ITOF: for (i = 0; i < N; i++) fo[i] = (float) xa[i]; break;
    case K_FSQRT: for (i = 0; i < N; i++) fo[i] = fzx_soft_sqrtf(fa[i] < 0 ? -fa[i] : fa[i]); break;
    case K_XADD: for (i = 0; i < N; i++) xo[i] = sh2z_add(xa[i], xb[i]); break;
    case K_XMUL: for (i = 0; i < N; i++) xo[i] = sh2z_mul(xa[i], xb[i]); break;
    case K_XDIV: for (i = 0; i < N; i++) xo[i] = sh2z_div(xa[i], xb[i]); break;
    case K_XSQRT: for (i = 0; i < N; i++) xo[i] = sh2z_sqrt(xa[i] < 0 ? -xa[i] : xa[i]); break;
    }
    t = cpu_frt_count_get();
    return t;
}

void user_init(void) {
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 2, 2, 2));
    vdp2_tvmd_display_set();
}

int main(void) {
    Bench* b = &fzx_bench;
    float *fa_c, *fb_c, *fo_c;
    int32_t *xa_c, *xb_c, *xo_c;
    volatile uint32_t* probe = (volatile uint32_t*) CART_BASE;
    int i, k;

    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    memset(b, 0, sizeof(*b));
    b->n = N;
    for (i = 0; i < N; i++) {
        fa_hw[i] = rand_float();
        fb_hw[i] = rand_float();
        xa_hw[i] = (int32_t) (next() % (64u << 16)) - (32 << 16); /* +-32.0 */
        xb_hw[i] = (int32_t) (next() % (8u << 16)) + (1 << 14);   /* 0.25 .. 8.25 */
    }
    /* Cartridge present if a write reads back. */
    probe[0] = 0xA5C3F00Du;
    b->cart_present = probe[0] == 0xA5C3F00Du;
    fa_c = (float*) (CART_BASE + 0x1000);
    fb_c = fa_c + N;
    fo_c = fb_c + N;
    xa_c = (int32_t*) (fo_c + N);
    xb_c = xa_c + N;
    xo_c = xb_c + N;
    if (b->cart_present) {
        memcpy(fa_c, fa_hw, sizeof(fa_hw));
        memcpy(fb_c, fb_hw, sizeof(fb_hw));
        memcpy(xa_c, xa_hw, sizeof(xa_hw));
        memcpy(xb_c, xb_hw, sizeof(xb_hw));
    }
    cpu_frt_init(CPU_FRT_CLOCK_DIV_8);
    for (k = 0; k < K_COUNT; k++) {
        b->hw[k] = run(k, fa_hw, fb_hw, fo_hw, xa_hw, xb_hw, xo_hw);
        if (b->cart_present) b->cart[k] = run(k, fa_c, fb_c, fo_c, xa_c, xb_c, xo_c);
    }
    b->magic = 0x465A5842;

    dbgio_printf("\x1b[H\x1b[2J SH-2 OP BENCH (CYCLES/OP, MODELLED)\n CART %s\n\n", b->cart_present ? "YES" : "NO");
    for (k = 1; k < K_COUNT; k++) {
        dbgio_printf(" %-7s HWRAM %5lu  CART %5lu\n", kNames[k],
                     (unsigned long) ((b->hw[k] - b->hw[K_COPY]) * 8 / N),
                     (unsigned long) (b->cart_present ? (b->cart[k] - b->cart[K_COPY]) * 8 / N : 0));
    }
    dbgio_flush();
    for (;;) {
        vdp2_sync();
        vdp2_sync_wait();
    }
    return 0;
}
