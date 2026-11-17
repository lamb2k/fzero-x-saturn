/* C5 synthetic hardware feasibility probe. No game source or assets. */
#include <yaul.h>
#include <stddef.h>
#include <string.h>

#ifndef PROBE_MODE
#define PROBE_MODE 0
#endif
_Static_assert(PROBE_MODE >= 0 && PROBE_MODE <= 6, "mode 0..6");
_Static_assert(sizeof(vdp2_scrn_rp_table_t) == 96, "RPT ABI");
_Static_assert(offsetof(vdp2_scrn_rp_table_t, kast) == 84, "KAst ABI");
#define BITMAP VDP2_VRAM_ADDR(0, 0)
#define COEFF VDP2_VRAM_ADDR(1, 0)
#define CHARS VDP2_VRAM_ADDR(2, 0)
#define MAP VDP2_VRAM_ADDR(3, 0)
#define RPT VDP2_VRAM_ADDR(3, 0x4000)
#define BACK VDP2_VRAM_ADDR(3, 0x1fffe)
#define Q 65536

/* Read these symbols with the emulator debugger; ready becomes 0x44524247
 * only after two syncs. Hashes cover actual uploaded bytes, in table order:
 * bitmap 65536, coefficient 131072, chars 128, map 16384, RPT 256, CRAM 64.
 * The shadow snapshot is not a hardware readback: capture VDP2 registers too. */
volatile uint32_t dual_rbg_ready, dual_rbg_fields;
volatile uint32_t dual_rbg_mode = PROBE_MODE;
volatile uint32_t dual_rbg_hashes[6];
volatile uint16_t dual_rbg_registers[144];
vdp2_scrn_rp_table_t dual_rbg_params[2];

static uint32_t hash(const volatile void *ptr, unsigned bytes) {
    const volatile uint16_t *p = ptr;
    uint32_t h = 2166136261u;
    while (bytes) {
        const uint16_t word=*p++;
        h=(h^(word>>8))*16777619u;
        h=(h^(word&255u))*16777619u;
        bytes-=2;
    }
    return h;
}

static unsigned floor_pixel(unsigned x, unsigned y) {
    if (!(x & 31) || !(y & 31)) return 3;
    return ((x >> 5) ^ (y >> 5)) & 1 ? 1 : 2;
}
static unsigned cloud_pixel(unsigned tile, unsigned x, unsigned y) {
    if (!tile) return 0;
    if (tile == 1) return (x == 3 || y == 3) ? 1 : 0;
    if (tile == 2) return ((x ^ y) & 1) ? 0 : 2;
    return (x == y || x + y == 7) ? 3 : 0;
}

static void art_init(void) {
    volatile uint16_t *b = (volatile uint16_t *)BITMAP;
    for (unsigned y=0; y<256; ++y)
        for (unsigned x=0; x<512; x+=4)
            *b++ = (uint16_t)((floor_pixel(x,y)<<12) | (floor_pixel(x+1,y)<<8) |
                (floor_pixel(x+2,y)<<4) | floor_pixel(x+3,y));
    volatile uint32_t *k = (volatile uint32_t *)COEFF;
    for (unsigned i=0; i<32768; ++i) k[i] = (256u * Q) / (i + 128u);
    b = (volatile uint16_t *)CHARS;
    for (unsigned t=0; t<4; ++t)
        for (unsigned y=0; y<8; ++y)
            for (unsigned x=0; x<8; x+=4)
                *b++ = (uint16_t)((cloud_pixel(t,x,y)<<12) | (cloud_pixel(t,x+1,y)<<8) |
                    (cloud_pixel(t,x+2,y)<<4) | cloud_pixel(t,x+3,y));
    volatile uint32_t *p = (volatile uint32_t *)MAP;
    for (unsigned y=0; y<64; ++y)
        for (unsigned x=0; x<64; ++x) {
            const unsigned t = ((x/4 + y/4) & 1) ? 0 : 1 + ((x/8+y/8)%3);
            *p++ = 0x00012000u + t; /* palette 1, character address / 32 */
        }
    volatile uint16_t *pal = (volatile uint16_t *)VDP2_CRAM(0);
    for (unsigned i=0;i<32;++i) pal[i]=0;
    pal[1]=RGB1555(1,2,8,20).raw; pal[2]=RGB1555(1,2,16,26).raw;
    pal[3]=RGB1555(1,8,28,31).raw;
    pal[17]=RGB1555(1,31,10,1).raw; pal[18]=RGB1555(1,31,24,3).raw;
    pal[19]=RGB1555(1,31,2,17).raw;
}

static void params_init(void) {
    const int32_t s = PROBE_MODE == 4 ? 0 : 16384;
    const int32_t c = PROBE_MODE == 4 ? Q : 63488;
    vdp2_scrn_rp_table_t *a = &dual_rbg_params[0];
    memset(dual_rbg_params,0,sizeof(dual_rbg_params));
    a->xst = -160*c + 112*s; a->yst = -160*s - 112*c;
    a->zst = 96*Q;
    a->delta_xst = -s; a->delta_yst = c;
    a->delta_x = c; a->delta_y = s;
    a->matrix.param.a = Q; a->matrix.param.f = Q;
    a->mx=256*Q; a->my=64*Q; a->kx=Q; a->ky=Q;
    /* KTAOF=0. A1 byte 0x20000 /4 = 0x8000 entries.
     * Screen position increments and denominator use the same rolled Y. */
    a->kast = 0x81000000u + a->yst;
    a->delta_kast = c; a->delta_kax = s;
    dual_rbg_params[1] = *a;
    vdp2_scrn_rp_table_t *b = &dual_rbg_params[1];
    b->matrix.param.a=49152; b->zst=64*Q;
    b->mx=192*Q; b->my=96*Q;
    if (PROBE_MODE == 2) *b = *a; /* wrong B geometry control */
    volatile uint32_t *out=(volatile uint32_t *)RPT;
    for (unsigned i=0;i<64;++i) out[i]=0;
    /* CPU writes to video RAM stay word/longword sized. */
    for (unsigned i=0;i<24;++i) {
        uint32_t wa,wb;
        memcpy(&wa,(const uint8_t *)a+4*i,4);
        memcpy(&wb,(const uint8_t *)b+4*i,4);
        out[i]=wa; out[32+i]=wb;
    }
}

void user_init(void) {
    vdp2_tvmd_display_clear();
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,
        VDP2_TVMD_HORZ_NORMAL_A, VDP2_TVMD_VERT_224);
}

int main(void) {
    dual_rbg_mode=PROBE_MODE;
    art_init(); params_init();
    vdp2_ioregs_t *r = vdp2_regs_get();
    /* Explicit registers: the SDK has no RBG1 format enum. Configuring
     * RBG0_PB as a second screen would overwrite RBG0 format instead. */
    r->ramctl=0x1307; /* CRAM1, split A/B, A0 bitmap, A1 coefficient, B=00 */
    r->bgon=PROBE_MODE==1 ? 0x0010 : 0x0030;
    r->chctla=0; r->chctlb=0x0200; /* RBG1 cells, RBG0 512x256 CLUT4 */
    r->bmpnb=0; r->pncn0=0; r->pncr=0; r->plsz=0;
    r->mpofr=0;
    uint16_t *maps=&r->mpabrb;
    for (unsigned i=0;i<8;++i) maps[i]=0x1818; /* 0x60000 / 0x4000 */
    r->rpmd=0; r->rprctl=0; r->ktaof=0;
    r->ktctl=PROBE_MODE==3 ? 0 : PROBE_MODE==5 ? 0x0001 :
        PROBE_MODE==6 ? 0x0100 : 0x0101;
    r->rpta=0x00032000; /* byte 0x64000; B is byte base+0x80 */
    r->wctla=0; r->wctlb=0; r->wctlc=0; r->wctld=0;
    r->craofa=0; r->craofb=0; r->ccctl=0; r->sfccmd=0;
    r->sfprmd=0; r->clofen=0; r->lnclen=0;
    r->prina=5; r->prinb=0; r->prir=3;
    r->prisa=0; r->prisb=0; r->prisc=0; r->prisd=0;
    vdp2_scrn_back_color_set(BACK,RGB1555(1,1,1,2));
    const uintptr_t addr[]={BITMAP,COEFF,CHARS,MAP,RPT,VDP2_CRAM(0)};
    const unsigned size[]={65536,131072,128,16384,256,64};
    for(unsigned i=0;i<6;++i) dual_rbg_hashes[i]=hash((void *)addr[i],size[i]);
    vdp2_tvmd_display_set();
    memcpy((void *)dual_rbg_registers,r,sizeof(*r));
    for (;;) {
        vdp2_sync(); vdp2_sync_wait();
        ++dual_rbg_fields;
        if(dual_rbg_fields>=2) dual_rbg_ready=0x44524247;
    }
}
