"""Original Controller ABI, actual absent-ghost check and atomic dual-RNG commit."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'upstream/G-Diffuser/decomp'


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux GCC/Clang required')
@unittest.skipUnless((D/'include/controller.h').is_file(),'pinned original headers required')
class FinishMenuBridge(unittest.TestCase):
    def test_source_scope_and_dual_rng(self):
        source=(ROOT/'ref/finish_menu_bridge.c').read_text()
        controls=[('port','gControllers[gPlayerControlPorts[0]]','gControllers[0]'),
                  ('disconnect','if(connected){','if(true){'),
                  ('ghost','gFastestGhost==NULL','true'),
                  ('seed2_stale','(uint32_t)gRandSeed2!=input->seed2','false'),
                  ('mask2_stale','gRandMask2!=input->mask2','false'),
                  ('no_seed2','memcpy(&gRandSeed2,&frame->seed2,sizeof(gRandSeed2));',';'),
                  ('no_mask2','gRandMask2=frame->mask2;',';')]
        flags=['-m32','-O2','-std=gnu11','-fsanitize=undefined','-fno-sanitize-recover=all',
               '-fno-strict-aliasing','-fno-builtin','-D_LANGUAGE_C','-DLANGUAGE_C','-DF3DEX_GBI_2',
               '-DVERSION_US=1','-DBUILD_VERSION=VERSION_I','-DASSET_VERSION=us','-DASSET_REVISION=rev0',
               '-DLEO_VERSION=LEO_VERSION_A','-DMFS_VERSION=MFS_VERSION_A','-DNON_MATCHING','-DAVOID_UB',
               '-D_MIPS_SZLONG=32','-D_MIPS_SZINT=32','-DFZX_RESULTS_SOURCE_ABI=1',
               '-I'+str(D/'include'),'-I'+str(D/'include/libultra'),'-I'+str(D/'include/libc'),
               '-I'+str(D),'-I'+str(D/'src'),'-I'+str(ROOT/'include'),
               '-include',str(ROOT/'ref/overrides.h')]
        with tempfile.TemporaryDirectory(prefix='fzx-finish-bridge-') as td:
            tmp=Path(td)
            for cc in ('gcc','clang'):
                for name,old,new in [('good',None,None),*controls]:
                    text=source
                    if old:self.assertEqual(text.count(old),1,old);text=text.replace(old,new)
                    path=tmp/(name+'.c');path.write_text(text);exe=tmp/(cc+'-'+name)
                    build=subprocess.run([cc,*flags,str(path),str(ROOT/'tests/finish_menu_bridge_driver.c'),'-o',str(exe)],capture_output=True,text=True)
                    self.assertEqual(build.returncode,0,build.stderr[-5000:])
                    run=subprocess.run([str(exe)],capture_output=True,text=True)
                    if name=='good':self.assertEqual(run.returncode,0,(cc,run.returncode,run.stderr))
                    else:self.assertNotEqual(run.returncode,0,(cc,name,'control survived'))
            print('finish menu original ABI and seven dual-RNG bridge mutation controls PASS')


if __name__=='__main__':unittest.main()
