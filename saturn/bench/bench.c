/* G1.3 control: time one function of the original tick on the SH-2, as the original float
 * code (GCC software float) and as SH2ZAM fixed point, with data in high work RAM and in the
 * 4 MB RAM cartridge. MIT; the float body is Math_OrthonormalizeAroundForward from the CC0
 * F-Zero X decompilation (src/sys/math.c:293), unchanged apart from types.
 *
 * Timing: FRT at clock/8, interrupts masked, 64 inputs per pass, 8 passes, minimum pass kept.
 * Results on the VDP2 console: cycles per call, then fixed-point error versus float in LSBs
 * of Q16.16. In an emulator these are EMULATOR/MODELLED; on a console they are HARDWARE. */
#include <yaul.h>
#include <stdint.h>
#include <string.h>
#include <sh2zam/sh2zam.h>

#define CASES 64
#define PASSES 8

typedef struct { float x, y, z; } vec3f;
typedef struct { vec3f x, y, z; } mtx3f;
typedef struct { sh2z_vec3_t x, y, z; } mtx3x;

#define SQ(a) ((a) * (a))

/* Correctly rounded binary32 square root from integer operations (bit-by-bit on the
 * significand). Yaul's toolchain has no libm, so a software-float port needs one like this;
 * VR4300 sqrt.s is correctly rounded too. Positive normal inputs; zero and negatives -> 0. */
static float __noinline soft_sqrtf(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    if ((int32_t)u <= 0) return 0.0f;
    int32_t e = (int32_t)(u >> 23) - 127;
    uint32_t m = (u & 0x7FFFFFu) | 0x800000u;
    if (e & 1) { m <<= 1; --e; }
    /* m * 2^23 is the significand scaled to an even power: its integer root has 24 bits. */
    uint64_t rem = (uint64_t)m << 23, root = 0, bit = (uint64_t)1 << 48;
    while (bit > rem) bit >>= 2;
    while (bit) {
        if (rem >= root + bit) { rem -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
        bit >>= 2;
    }
    if (rem > root) ++root; /* round to nearest (ties cannot occur for a square root) */
    uint32_t r = (uint32_t)root;
    int32_t re = e / 2;
    while (r >= 0x1000000u) { r >>= 1; ++re; }
    u = ((uint32_t)(re + 127) << 23) | (r & 0x7FFFFFu);
    memcpy(&f, &u, 4);
    return f;
}

/* Original, float (decomp src/sys/math.c:293). */
static int32_t __noinline orth_float(mtx3f *basis)
{
    float t = SQ(basis->x.x) + SQ(basis->x.y) + SQ(basis->x.z);
    if (t == 0.0f) return -1;
    t = 1.0f / soft_sqrtf(t);
    basis->x.x *= t; basis->x.y *= t; basis->x.z *= t;
    basis->z.x = basis->y.y * basis->x.z - basis->y.z * basis->x.y;
    basis->z.y = basis->y.z * basis->x.x - basis->y.x * basis->x.z;
    basis->z.z = basis->y.x * basis->x.y - basis->y.y * basis->x.x;
    t = SQ(basis->z.x) + SQ(basis->z.y) + SQ(basis->z.z);
    if (t == 0.0f) return -1;
    t = 1.0f / soft_sqrtf(t);
    basis->z.x *= t; basis->z.y *= t; basis->z.z *= t;
    basis->y.x = basis->x.y * basis->z.z - basis->x.z * basis->z.y;
    basis->y.y = basis->x.z * basis->z.x - basis->x.x * basis->z.z;
    basis->y.z = basis->x.x * basis->z.y - basis->x.y * basis->z.x;
    return 0;
}

/* Straight SH2ZAM translation: normalize and cross from the library. */
static int32_t __noinline orth_fx_api(mtx3x *b)
{
    b->x = sh2z_vec3_normalize(b->x);
    if (b->x.x == 0 && b->x.y == 0 && b->x.z == 0) return -1;
    b->z = sh2z_vec3_normalize(sh2z_vec3_cross(b->y, b->x));
    if (b->z.x == 0 && b->z.y == 0 && b->z.z == 0) return -1;
    b->y = sh2z_vec3_cross(b->x, b->z);
    return 0;
}

/* The original's shape: one reciprocal square root per axis, then scale. */
static int32_t __noinline orth_fx_rsqrt(mtx3x *b)
{
    sh2z_fx_t s = sh2z_rsqrt(sh2z_vec3_dot(b->x, b->x));
    if (s == 0) return -1;
    b->x = sh2z_vec3_scale(b->x, s);
    b->z = sh2z_vec3_cross(b->y, b->x);
    s = sh2z_rsqrt(sh2z_vec3_dot(b->z, b->z));
    if (s == 0) return -1;
    b->z = sh2z_vec3_scale(b->z, s);
    b->y = sh2z_vec3_cross(b->x, b->z);
    return 0;
}

static mtx3f in_f[CASES], work_f[CASES];
static mtx3x in_x[CASES], work_x[CASES];
static uint32_t rng = 1;
static float rnd(void) /* [-1, 1) */
{
    rng = rng * 1103515245u + 12345u;
    return (float)((int32_t)(rng >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}
static sh2z_fx_t to_fx(float f) { return (sh2z_fx_t)(f * 65536.0f); }
static vec3f unitish(void)
{
    vec3f v = { rnd(), rnd(), rnd() };
    float l = soft_sqrtf(SQ(v.x) + SQ(v.y) + SQ(v.z));
    float s = (0.95f + 0.1f * (rnd() * 0.5f + 0.5f)) / (l > 0.01f ? l : 1.0f);
    v.x *= s; v.y *= s; v.z *= s;
    return v;
}

static uint32_t time_float(mtx3f *work)
{
    uint32_t best = UINT32_MAX;
    for (int p = 0; p < PASSES; ++p) {
        memcpy(work, in_f, sizeof(in_f));
        cpu_frt_count_set(0);
        for (int i = 0; i < CASES; ++i) orth_float(&work[i]);
        const uint32_t t = cpu_frt_count_get();
        if (t < best) best = t;
    }
    return best * 8u / CASES;
}

static uint32_t time_fx(mtx3x *work, int32_t (*fn)(mtx3x *))
{
    uint32_t best = UINT32_MAX;
    for (int p = 0; p < PASSES; ++p) {
        memcpy(work, in_x, sizeof(in_x));
        cpu_frt_count_set(0);
        for (int i = 0; i < CASES; ++i) fn(&work[i]);
        const uint32_t t = cpu_frt_count_get();
        if (t < best) best = t;
    }
    return best * 8u / CASES;
}

static uint32_t max_error(const mtx3x *x)
{
    uint32_t worst = 0;
    for (int i = 0; i < CASES; ++i) {
        const float *f = &work_f[i].x.x;
        const sh2z_fx_t *q = &x[i].x.x;
        for (int k = 0; k < 9; ++k) {
            int32_t d = to_fx(f[k]) - q[k];
            uint32_t a = (uint32_t)(d < 0 ? -d : d);
            if (a > worst) worst = a;
        }
    }
    return worst;
}

void user_init(void)
{
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE, VDP2_TVMD_HORZ_NORMAL_A,
                              VDP2_TVMD_VERT_224);
    vdp2_scrn_back_color_set(VDP2_VRAM_ADDR(3, 0x01FFFE), RGB1555(1, 2, 2, 8));
    vdp2_tvmd_display_set();
}

int main(void)
{
    dbgio_init();
    dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);
    dbgio_dev_font_load();
    dram_cart_init();
    for (int i = 0; i < CASES; ++i) {
        in_f[i].x = unitish(); in_f[i].y = unitish(); in_f[i].z = unitish();
        const float *f = &in_f[i].x.x;
        sh2z_fx_t *q = &in_x[i].x.x;
        for (int k = 0; k < 9; ++k) q[k] = to_fx(f[k]);
    }
    const dram_cart_id_t cart = dram_cart_id_get();
    /* Direct probe of cartridge RAM (A-bus CS0, cache-through), for emulators whose ID read
     * does not match Yaul's. The game itself must use the ID (G1.7). */
    volatile uint32_t *const cart_ram = (volatile uint32_t *)0x22400000u;
    cart_ram[0] = 0x5A5AA5A5u;
    cart_ram[1] = 0x12345678u;
    const bool cart_probe = cart_ram[0] == 0x5A5AA5A5u && cart_ram[1] == 0x12345678u;
    mtx3x *cart_x = (cart == DRAM_CART_ID_4MIB) ? (mtx3x *)dram_cart_area_get()
                  : cart_probe ? (mtx3x *)0x22400000u : NULL;

    cpu_frt_init(CPU_FRT_CLOCK_DIV_8);
    const uint8_t mask = cpu_intc_mask_get();
    cpu_intc_mask_set(15);
    const uint32_t c_float = time_float(work_f);
    const uint32_t c_api = time_fx(work_x, orth_fx_api);
    const uint32_t e_api = max_error(work_x);
    const uint32_t c_rsqrt = time_fx(work_x, orth_fx_rsqrt);
    const uint32_t e_rsqrt = max_error(work_x);
    const uint32_t c_cart = cart_x ? time_fx(cart_x, orth_fx_api) : 0;
    cpu_intc_mask_set(mask);

    dbgio_printf("\x1b[H\x1b[2J G1.3 SH-2 BENCH\n"
                 " Math_OrthonormalizeAroundForward\n"
                 " cycles per call (FRT/8, %d cases)\n\n"
                 " FLOAT    HWRAM %6lu\n"
                 " FX-API   HWRAM %6lu  ERR %4lu\n"
                 " FX-RSQRT HWRAM %6lu  ERR %4lu\n"
                 " FX-API   CART  %6lu  %s\n"
                 " CART ID BYTE %02X\n\n"
                 " BENCH DONE\n",
                 CASES, (unsigned long)c_float, (unsigned long)c_api, (unsigned long)e_api,
                 (unsigned long)c_rsqrt, (unsigned long)e_rsqrt, (unsigned long)c_cart,
                 cart == DRAM_CART_ID_4MIB ? "4MB" : cart == DRAM_CART_ID_1MIB ? "1MB"
                 : cart_probe ? "RAM BY PROBE" : "NO CART",
                 (unsigned int)*(volatile uint8_t *)0x24FFFFFFu);
    dbgio_flush();
    vdp2_sync();
    vdp2_sync_wait();
    while (true) {
        vdp2_sync();
        vdp2_sync_wait();
    }
}
