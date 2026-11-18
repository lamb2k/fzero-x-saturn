"""HOST check of the C3 stream's fog and colour wiring through SH2ZAM.

The fog thresholds come from SH2ZAM shade-1 (sh2z_shade_fog_build) and the
per-corner fade and 5:5:5 colour from shade-2 (sh2z_shade_fog_gouraud_refs);
SH2ZAM's own tests cover the kernels. This checks the caller: the thresholds
equal an independent evaluation of the source fog rule, the prepared table is
selected, the shade-1 fallback lookup equals a full scan, and the fused colour
equals the documented OFFSET/MIX rules applied to the reference fade. Mutated
wiring must fail.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/fzx_road_stream_draw.c'

FOG_DRIVER = r'''
#include "fzx_road_stream_draw.c"
#include <stdio.h>
/* Independent thresholds: fade >= i iff ratio >= ceil((i-fo)*2^30/fm), with
 * ratio = pa-floor(-pb*2^30/(z/4)); the depth is the first z/4 above N/(m+1). */
static uint32_t threshold(unsigned i,int32_t pa,int32_t pb,int start,int32_t far){
    const int64_t fm=128000/(1000-start),fo=(500-start)*256/(1000-start);
    const int64_t n=((int64_t)i-fo)*INT64_C(1073741824);
    const int64_t r=n/fm+(n%fm>0);
    if(r<=-INT64_C(1073741824))return 0;
    if(r>INT64_C(1073741824)||(int64_t)pa-r<0)return (uint32_t)far+1u;
    const uint64_t q=(uint64_t)(-(int64_t)pb)*UINT64_C(1073741824)/(uint64_t)((int64_t)pa-r+1)+1u;
    return q<=(uint64_t)far/4u?(uint32_t)q*4u:(uint32_t)far+1u;
}
static unsigned reference(const fzx_road_stream_draw_work_t *w,int32_t z){
    const uint32_t d=z>0?(uint32_t)z:0;unsigned r=0;
    for(unsigned i=1;i<256;++i)if(w->fog.depth[i]<=d)r=i;
    return r;
}
static uint16_t offset_word(unsigned fade,unsigned v,const uint8_t live[3],const uint8_t base[3],const uint8_t mean[3]){
    static const int level[3]={0,128,255};uint16_t out=0x8000;
    for(unsigned k=0;k<3;++k){const int32_t x=(int32_t)fade*(live[k]-mean[k])-level[v]*(base[k]-mean[k]);
        int32_t c=16+x/255+(x<0&&x%255==0);c=c<0?0:c>31?31:c;out|=(uint16_t)(c<<(5*k));}
    return out;
}
static uint16_t mix_word(unsigned fade,const uint8_t near[3],const uint8_t far[3]){
    uint16_t out=0x8000;
    for(unsigned k=0;k<3;++k)out|=(uint16_t)((((uint32_t)near[k]*(255u-fade)+(uint32_t)far[k]*fade)*31u+32512u)/65025u<<(5*k));
    return out;
}
int main(void){
    static fzx_road_stream_draw_work_t w;static fzx_road_stream_pack_t pack;unsigned tables=0;unsigned long long checks=0;
    pack.base_fog[0]=40;pack.base_fog[1]=120;pack.base_fog[2]=230;w.pack=&pack;
    RefRoadPlan plan;memset(&plan,0,sizeof(plan));
    plan.fog[0]=252;plan.fog[1]=192;plan.fog[2]=253;plan.primitive[0]=255;plan.primitive[1]=17;plan.primitive[2]=90;
    ramp_prepare(&w,&plan);if(!w.ramp_valid||w.mix_valid||!mix_prepare(&w)){puts("ramp");return 1;}
    const uint8_t means[3][3]={{0,0,0},{16,9,30},{31,31,31}};
    const int32_t pas[]={1073741824,1100000000,1200000000,1500000000};
    const int32_t pbs[]={-1,-8192,-40000,-81920,-500000};
    const int starts[]={0,250,500,750,990,996};
    const int32_t fars[]={4000,48000,128000,400000,4000000};
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<5;++b)for(unsigned s=0;s<6;++s)for(unsigned f=0;f<5;++f){
        plan.fog_start=starts[s];
        sh2z_view_t view;memset(&view,0,sizeof(view));view.near_z=16;view.far_z=fars[f];
        fzx_road_stream_draw_stats_t st;memset(&st,0,sizeof(st));w.fog_valid=false;
        fog_prepare(&w,&view,&plan,pas[a],pbs[b],&st);
        if(!w.fog_valid||!w.fogp_ok||st.fog_updates!=1){printf("table %u not prepared\n",tables);return 1;}
        ++tables;
        for(unsigned i=0;i<256;++i){++checks;
            if(w.fog.depth[i]!=(i?threshold(i,pas[a],pbs[b],starts[s],fars[f]):0u)||w.fogp.depth[i]!=w.fog.depth[i]){
                printf("table %u threshold %u\n",tables,i);return 1;}}
        int32_t zs[256*5+5];unsigned nz=0;
        for(unsigned i=0;i<256;++i)for(int d=-2;d<=2;++d){const int64_t z=(int64_t)w.fog.depth[i]+d;
            if(z>=INT32_MIN&&z<=INT32_MAX)zs[nz++]=(int32_t)z;}
        const int32_t extreme[]={INT32_MIN,-1,0,1,INT32_MAX};
        for(unsigned i=0;i<5;++i)zs[nz++]=extreme[i];
        for(unsigned i=0;i<nz;++i){const unsigned fade=reference(&w,zs[i]);++checks;
            if(fog_lookup(&w,zs[i])!=fade){printf("fallback z %ld\n",(long)zs[i]);return 1;}
            sh2z_view_vertex_t v;memset(&v,0,sizeof(v));v.z=zs[i];const sh2z_view_vertex_t *ref[1]={&v};uint16_t word;
            const unsigned variant=(unsigned)(fade>=64)+(unsigned)(fade>=192);
            for(unsigned m=0;m<3;++m){
                if(sh2z_shade_fog_gouraud_refs(&word,(const void *const *)ref,offsetof(sh2z_view_vertex_t,z),1,&w.fogp,&w.ramp_offset,means[m])!=(int)variant||
                   word!=offset_word(fade,variant,w.ramp_key,w.ramp_key+3,means[m])){printf("offset z %ld\n",(long)zs[i]);return 1;}}
            if(sh2z_shade_fog_gouraud_refs(&word,(const void *const *)ref,offsetof(sh2z_view_vertex_t,z),1,&w.fogp,&w.ramp_mix,NULL)!=(int)variant||
               word!=mix_word(fade,plan.primitive,plan.fog)){printf("mix z %ld\n",(long)zs[i]);return 1;}
        }
    }
    printf("ROAD_STREAM_FOG_SH2ZAM_HOST_PASS tables=%u checks=%llu\n",tables,checks);
    return 0;
}
'''


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'POSIX GCC/Clang required')
class RoadStreamFogShade(unittest.TestCase):
    """Fog thresholds, prepared lookup, fallback and fused colour wiring."""
    def run_driver(self, source, cc):
        lib = ROOT / 'third_party/SH2ZAM/saturn'
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            src.mkdir()
            (src / 'fzx_road_stream_draw.c').write_text(source)
            c = Path(tmp) / 'fog.c'
            exe = Path(tmp) / 'fog'
            c.write_text(FOG_DRIVER)
            files = [c, ROOT / 'src/fzx_road_stream.c', ROOT / 'src/fzx_road_pack.c',
                     ROOT / 'src/fzx_view.c', ROOT / 'src/fzx_view16.c', ROOT / 'src/fzx_view16_center.c',
                     lib / 'src/sh2zam_view.c', lib / 'src/sh2zam_view_round.c',
                     lib / 'src/sh2zam_view_guard_src.c', lib / 'src/sh2zam_view_center.c',
                     lib / 'src/sh2zam_view_guard_src_center.c',
                     lib / 'src/sh2zam_view_merge_tier.c', lib / 'src/sh2zam_shade.c', lib / 'src/sh2zam.c',
                     lib / 'src/sh2zam_shade_fused.c', lib / 'src/sh2zam_view_camera.c']
            build = subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-misleading-indentation', '-fsanitize=undefined',
                                    '-fno-sanitize-recover=all', '-DSH2Z_FORCE_PORTABLE=1',
                                    '-DFZX_SOURCE_CAMERA=1', '-I' + str(src), '-I' + str(ROOT / 'include'),
                                    '-I' + str(ROOT / 'ref'), '-I' + str(lib / 'include'),
                                    *map(str, files), '-lm', '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run([str(exe)], capture_output=True, text=True, timeout=600)

    def test_fog_and_colour_wiring(self):
        source = SOURCE.read_text()
        for cc in ('gcc', 'clang'):
            result = self.run_driver(source, cc)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(cc + ': ' + result.stdout.strip())
        for before, after in (('fo=(500-plan->fog_start)*256', 'fo=(500-plan->fog_start)*255'),
                              ('w->fogp_ok=sh2z_shade_fogp_prepare(&w->fogp,&w->fog);', 'w->fogp_ok=false;'),
                              ('if(depth>=w->fog.depth[middle])lo=middle;', 'if(depth>w->fog.depth[middle])lo=middle;'),
                              ('static const uint8_t level[3]={0,128,255};', 'static const uint8_t level[3]={0,128,254};'),
                              ('key[6+c]=plan->primitive[c];key[9+c]=plan->fog[c];', 'key[6+c]=plan->fog[c];key[9+c]=plan->primitive[c];')):
            self.assertEqual(source.count(before), 1, before)
            result = self.run_driver(source.replace(before, after), 'gcc')
            self.assertNotEqual(result.returncode, 0, before)
            print('rejected: ' + before)


if __name__ == '__main__':
    unittest.main()
