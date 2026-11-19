/* E0 HOST = SH-2 equality, VDP2 tables slice (docs/ENGINE_PLAN.md section 5,
 * gate 3). HOST tier; private stage only: it reads the ROM-derived sky colours.
 *
 * Runs the engine's per-frame VDP2 table preparation on the view and the
 * backdrop taken from an SH-2 memory dump: the floor's rotation parameter A
 * and W0 line window (fzx_floor_rbg_prepare), the clouds' rotation parameter
 * B and W1 line window (fzx_clouds_rbg_prepare) and the 224 back-screen sky
 * colours (fzx_clouds_sky_lines, include/fzx_sky_lines_body.h). It writes them
 * big-endian, in the SH-2's struct layout; tools/e0_vdp2_eq.py compares them
 * with VDP2 VRAM, CRAM and work RAM.
 *
 * Build with the engine image's -D set (tools/e0_vdp2_eq.py build), and
 * -I generated/sky -I generated/floor_rbg.
 *
 *   e0_vdp2_host INPUT OUTPUT [--flip-view]
 *
 * INPUT (big-endian): "E0VI", version 1, tick; the SH-2's fzx_view_t (80 bytes)
 * and RefBackdrop (424 bytes) exactly as dumped.
 * OUTPUT (big-endian): "E0VO", version 1, floor bands, cloud bands, sky lines
 * set; fzx_floor_rbg_t (1016 bytes), fzx_clouds_rbg_t (1020 bytes), the sky
 * lines (448 bytes), CRAM entries 0..65 as the image sets them (130 bytes:
 * entry 0 untouched and written 0, 1 the floor fog, 2..65 the sky samples).
 *
 * --flip-view (failing control): XOR bit 16 of the view's translation[1]. */
#include "fzx_floor_rbg.h"
#include "fzx_clouds_rbg.h"
#include "fzx_sky.h"
#include "sky_colors.h"
#include "floor_rbg_identity.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {VIEW_BYTES=80,BACKDROP_BYTES=424,HEAD=12,FLOOR_BYTES=1016,CLOUDS_BYTES=1020};
_Static_assert(sizeof(fzx_view_t)==VIEW_BYTES&&sizeof(RefBackdrop)==BACKDROP_BYTES,"SH-2 record sizes");
_Static_assert(sizeof(fzx_floor_rbg_t)==FLOOR_BYTES&&sizeof(fzx_clouds_rbg_t)==CLOUDS_BYTES,"SH-2 table sizes");

/* What fzx_sky_lines_body.h needs, as saturn/clouds_vdp2_native.c and
 * saturn/sky_native.h provide it. */
#define FZX_SKY_MAX_COMMANDS 192u
static uint16_t sky_lines[224];
#define SKY_LINES sky_lines
static fzx_sky_stats_t fzx_sky_stats;
static uint32_t fzx_clouds_status=1,fzx_clouds_sky_line_count;
#include "fzx_sky_lines_body.h"

static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint16_t be16(const uint8_t *p){return (uint16_t)((unsigned)p[0]<<8|p[1]);}
static void put32(FILE *f,uint32_t v){const uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v};fwrite(b,1,4,f);}
static void put16(FILE *f,uint16_t v){const uint8_t b[2]={(uint8_t)(v>>8),(uint8_t)v};fwrite(b,1,2,f);}
#define FAIL(...) do{fprintf(stderr,"e0_vdp2_host: " __VA_ARGS__);fputc('\n',stderr);return 1;}while(0)

static bool count_band(void *context,const fzx_floor_polygon_t *q){(void)q;++*(uint32_t *)context;return true;}
/* fzx_floor_rp_t: 13 int32, 8 int16 (viewpoint, pad, center, pad), 7 int32. */
static void put_rp(FILE *f,const fzx_floor_rp_t *rp){
    for(unsigned i=0;i<3;++i)put32(f,(uint32_t)rp->start[i]);
    for(unsigned i=0;i<2;++i)put32(f,(uint32_t)rp->line[i]);
    for(unsigned i=0;i<2;++i)put32(f,(uint32_t)rp->dot[i]);
    for(unsigned i=0;i<6;++i)put32(f,(uint32_t)rp->matrix[i]);
    for(unsigned i=0;i<3;++i)put16(f,(uint16_t)rp->viewpoint[i]);
    put16(f,(uint16_t)rp->pad0);
    for(unsigned i=0;i<3;++i)put16(f,(uint16_t)rp->center[i]);
    put16(f,(uint16_t)rp->pad1);
    for(unsigned i=0;i<2;++i)put32(f,(uint32_t)rp->move[i]);
    for(unsigned i=0;i<2;++i)put32(f,(uint32_t)rp->scale[i]);
    put32(f,rp->coefficient_start);put32(f,(uint32_t)rp->coefficient_line);put32(f,(uint32_t)rp->coefficient_dot);
}
static void put_window(FILE *f,const uint16_t w[224][2]){for(unsigned y=0;y<224;++y){put16(f,w[y][0]);put16(f,w[y][1]);}}

int main(int argc,char **argv){
    if(argc!=3&&!(argc==4&&!strcmp(argv[3],"--flip-view")))FAIL("usage: e0_vdp2_host INPUT OUTPUT [--flip-view]");
    _Static_assert(offsetof(fzx_floor_rp_t,move)==68&&offsetof(fzx_floor_rp_t,coefficient_start)==84,"rp layout");
    FILE *in=fopen(argv[1],"rb");uint8_t buf[HEAD+VIEW_BYTES+BACKDROP_BYTES+1];
    if(!in)FAIL("cannot read input");
    const size_t got=fread(buf,1,sizeof(buf),in);fclose(in);
    if(got!=sizeof(buf)-1||memcmp(buf,"E0VI",4)||be32(buf+4)!=1u)FAIL("bad input record");
    const uint32_t tick=be32(buf+8);
    const uint8_t *v=buf+HEAD;fzx_view_t view;
    for(unsigned i=0;i<9;++i)view.rotation[i/3][i%3]=(int32_t)be32(v+i*4);
    for(unsigned i=0;i<3;++i)view.translation[i]=(int32_t)be32(v+36+i*4);
    for(unsigned i=0;i<2;++i){view.focal[i]=(int32_t)be32(v+48+i*4);view.center[i]=(int32_t)be32(v+56+i*4);}
    view.near_plane=(int32_t)be32(v+64);view.far_plane=(int32_t)be32(v+68);
    for(unsigned i=0;i<4;++i)view.clip[i]=(int16_t)be16(v+72+i*2);
    if(argc==4)view.translation[1]^=1<<16;
    /* RefBackdrop: faults, count, float state[24], 20 vertices of
     * {short position[3], u16 flag, short uv[2], u8 color[4]}. */
    const uint8_t *b=v+VIEW_BYTES;RefBackdrop bd;memset(&bd,0,sizeof(bd));
    bd.faults=be32(b);bd.count=be32(b+4);
    for(unsigned i=0;i<24;++i){const uint32_t bits=be32(b+8+i*4);memcpy(&bd.state[i],&bits,4);}
    for(unsigned i=0;i<20;++i){const uint8_t *q=b+104+i*16;RefBackdropVertex *x=&bd.vertices[i];
        for(unsigned j=0;j<3;++j)x->position[j]=(short)be16(q+j*2);
        x->flag=be16(q+6);x->uv[0]=(short)be16(q+8);x->uv[1]=(short)be16(q+10);memcpy(x->color,q+12,4);}

    /* The master's prelude order: sky lines, clouds, (skyline), floor. */
    static fzx_floor_rbg_t floor;static fzx_clouds_rbg_t clouds;
    uint32_t floor_bands=0,cloud_bands=0;
    if(!fzx_clouds_sky_lines(&view,&bd))FAIL("sky lines refused: fault %u",(unsigned)fzx_sky_stats.fault);
    if(!fzx_clouds_rbg_prepare(&view,&bd,&clouds,count_band,&cloud_bands))FAIL("clouds refused: fault %u",(unsigned)clouds.fault);
    if(!fzx_floor_rbg_prepare(&view,&bd,&floor,count_band,&floor_bands))FAIL("floor refused: fault %u",(unsigned)floor.fault);

    FILE *f=fopen(argv[2],"wb");if(!f)FAIL("cannot write output");
    fwrite("E0VO",1,4,f);put32(f,1u);put32(f,floor_bands);put32(f,cloud_bands);put32(f,fzx_clouds_sky_line_count);
    put_rp(f,&floor.rp);put_window(f,floor.window);
    put32(f,floor.fault);put32(f,floor.visible_lines);put32(f,floor.index_min);put32(f,floor.index_max);
    put32(f,floor.commands);put32(f,floor.hash);
    put_rp(f,&clouds.rp);put_window(f,clouds.window);
    put32(f,clouds.fault);put32(f,clouds.visible_lines);put32(f,clouds.degenerate);put32(f,clouds.commands);
    put32(f,clouds.inner_alpha);put32(f,clouds.active);put32(f,clouds.hash);
    for(unsigned y=0;y<224;++y)put16(f,sky_lines[y]);
    put16(f,0);put16(f,(uint16_t)FZX_FLOOR_FOG);for(unsigned i=0;i<64;++i)put16(f,fzx_sky_colors[i]);
    if(fclose(f))FAIL("write failed");
    printf("tick %u floor bands %u lines %u; cloud bands %u lines %u; sky lines set %u\n",(unsigned)tick,
        (unsigned)floor_bands,(unsigned)floor.visible_lines,(unsigned)cloud_bands,(unsigned)clouds.visible_lines,
        (unsigned)fzx_clouds_sky_line_count);
    return 0;
}
