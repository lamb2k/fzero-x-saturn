#include <yaul.h>
#include "scene_probe.h"
#include "expected.h"
#ifndef FZX_SCENE_CONTROL
#define FZX_SCENE_CONTROL 0
#endif
volatile struct {
    unsigned int done,cases,bad,first_bad,backdrop_hash,skyline_hash;
} fzx_scene_result;
void user_init(void) {
    vdp2_tvmd_display_res_set(VDP2_TVMD_INTERLACE_NONE,VDP2_TVMD_HORZ_NORMAL_A,VDP2_TVMD_VERT_224);
    vdp2_tvmd_display_set();
}
int main(void) {
    unsigned int frame,hashes[2]={0,0};
    fzx_scene_result.first_bad=0xffffffffu;
    if(ref_scene_probe_init())fzx_scene_result.bad=1;
    for(frame=0;frame<REF_SCENE_PROBE_CASES;++frame) {
        const int fault=ref_scene_probe_step(frame,hashes);
#if FZX_SCENE_CONTROL
        hashes[0]^=1;
#endif
        if(fault || hashes[0]!=expected_scene[frame][0] || hashes[1]!=expected_scene[frame][1]) {
            if(!fzx_scene_result.bad)fzx_scene_result.first_bad=frame;
            ++fzx_scene_result.bad;
        }
        fzx_scene_result.backdrop_hash=hashes[0];fzx_scene_result.skyline_hash=hashes[1];
        ++fzx_scene_result.cases;
    }
    fzx_scene_result.done=1;
    dbgio_init();dbgio_dev_default_init(DBGIO_DEV_VDP2_ASYNC);dbgio_dev_font_load();
    dbgio_printf("SCENE %lu CASES %lu DIFFER\n",(unsigned long)fzx_scene_result.cases,(unsigned long)fzx_scene_result.bad);
    dbgio_flush();
    for(;;) {vdp2_sync();vdp2_sync_wait();}
}
