/* HOST synthetic address contract; includes the real native submitter. */
#include <stdio.h>
#include <string.h>
#include "planes_native.c"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"compact gate line %d\n",__LINE__);return 1;}}while(0)

/* Uncalled integration dependencies: this executable tests emission only. */
unsigned char sat_stub_data_end[1];
uint32_t fzx_texture_cd_scratch_bytes(void){return 0;}
bool fzx_texture_cd_load(const char *n,uint32_t b,uint32_t h,void *v,void *s,uint32_t z)
{(void)n;(void)b;(void)h;(void)v;(void)s;(void)z;return false;}
bool fzx_planes_draw(const fzx_view_t *v,const RefBackdrop *b,const uint16_t f[3],
    const uint16_t m[3],uint32_t n,fzx_plane_emit_fn e,void *c,fzx_plane_stats_t *s)
{(void)v;(void)b;(void)f;(void)m;(void)n;(void)e;(void)c;(void)s;return false;}

#if FZX_PLANE_CLOUD_ONLY
static bool rejects(fzx_plane_quad_t q) {
    vdp1_cmdt_t cmd,saved;uint16_t shades[4],before[4];
    memset(&cmd,0xa5,sizeof(cmd));memcpy(&saved,&cmd,sizeof(cmd));
    memset(shades,0x5a,sizeof(shades));memcpy(before,shades,sizeof(before));
    submit_t submit={&cmd,0x100000u,shades,0};
    return !emit(&submit,&q) && !submit.count && !memcmp(&cmd,&saved,sizeof(cmd)) &&
        !memcmp(shades,before,sizeof(shades));
}
#endif

int main(void) {
    unsigned records=0;
    for(unsigned variant=0;variant<8;++variant)for(unsigned level=0;level<4;++level) {
        const unsigned width=64u>>level,height=32u>>level,size=width*height*2u;
        for(unsigned tile=0;tile<(1u<<(2*level));++tile) {
            fzx_plane_quad_t q={.layer=1,.level=(uint8_t)level,.variant=(uint8_t)variant,
                .width=(uint16_t)width,.height=(uint16_t)height,
                .offset=131072u+variant*16384u+level*4096u+tile*size,
                .x={1,2,3,4},.y={5,6,7,8},.shade={0x8000,0x8210,0x8421,0xffff}};
            fzx_plane_quad_t saved=q;vdp1_cmdt_t cmd;uint16_t shades[4];
            submit_t submit={&cmd,0x100000u,shades,0};
            CHECK(emit(&submit,&q));CHECK(submit.count==1);
            CHECK(!memcmp(&q,&saved,sizeof(q)));
            const unsigned expected=q.offset-(FZX_PLANE_CLOUD_ONLY?131072u:0u);
            CHECK(cmd.texture==0x100000u+expected);
            CHECK(expected+size<=FZX_PLANE_RESIDENT_BYTES);
            CHECK(cmd.width==width && cmd.height==height && cmd.kind==2);
            CHECK(cmd.mode.cc_mode==VDP1_CMDT_CC_REPLACE && cmd.mode.color_mode==VDP1_CMDT_CM_RGB_32768);
            CHECK(cmd.cmd_xa==1 && cmd.cmd_yd==8 && !memcmp(shades,q.shade,8));
            ++records;
        }
    }
    CHECK(records==680);
#if FZX_PLANE_CLOUD_ONLY
    const fzx_plane_quad_t valid={.layer=1,.width=64,.height=32,.offset=131072};
    fzx_plane_quad_t bad=valid;bad.layer=0;CHECK(rejects(bad));
    bad=valid;bad.layer=2;CHECK(rejects(bad));
    bad=valid;bad.level=4;CHECK(rejects(bad));
    bad=valid;bad.variant=8;CHECK(rejects(bad));
    bad=valid;bad.width=0;CHECK(rejects(bad));
    bad=valid;bad.width=32;CHECK(rejects(bad));
    bad=valid;bad.height=65535;CHECK(rejects(bad));
    bad=valid;bad.offset=0;CHECK(rejects(bad));
    bad=valid;bad.offset=131071;CHECK(rejects(bad));
    bad=valid;bad.offset=131073;CHECK(rejects(bad));
    bad=valid;bad.offset=262144-4096+8;CHECK(rejects(bad));
    bad=valid;bad.offset=UINT32_MAX;CHECK(rejects(bad));
#endif
    puts("680 original cloud offsets preserve commands and fit resident range");
    return 0;
}
