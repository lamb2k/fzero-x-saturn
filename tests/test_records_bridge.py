"""Actual source Controller ABI and guarded records RNG publication."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'upstream/G-Diffuser/decomp'

@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux GCC/Clang required')
@unittest.skipUnless((D/'include/controller.h').is_file(),'pinned original headers required')
class RecordsBridge(unittest.TestCase):
    def test_actual_source_controller_and_rng_boundary(self):
        source=(ROOT/'ref/records_bridge.c').read_text()
        controls=[('port','gControllers[gPlayerControlPorts[0]]','gControllers[0]'),
                  ('disconnect','if(connected) {','if(true) {'),
                  ('stale_seed','(uint32_t)gRandSeed1!=input->rng_seed1','false'),
                  ('no_mask_write','gRandMask1=frame->rng_mask1;',';')]
        with tempfile.TemporaryDirectory() as temp:
            out=Path(temp)
            flags=['-m32','-O2','-std=gnu11','-fsanitize=undefined','-fno-sanitize-recover=all',
                   '-fno-strict-aliasing','-fno-builtin','-D_LANGUAGE_C','-DLANGUAGE_C','-DF3DEX_GBI_2',
                   '-DVERSION_US=1','-DBUILD_VERSION=VERSION_I','-DASSET_VERSION=us','-DASSET_REVISION=rev0',
                   '-DLEO_VERSION=LEO_VERSION_A','-DMFS_VERSION=MFS_VERSION_A','-DNON_MATCHING','-DAVOID_UB',
                   '-D_MIPS_SZLONG=32','-D_MIPS_SZINT=32','-DFZX_RESULTS_SOURCE_ABI=1',
                   '-I'+str(D/'include'),'-I'+str(D/'include/libultra'),'-I'+str(D/'include/libc'),
                   '-I'+str(D),'-I'+str(D/'src'),'-I'+str(ROOT/'include'),
                   '-include',str(ROOT/'ref/overrides.h')]
            for cc in ('gcc','clang'):
                for name,old,new in [('good',None,None),*controls]:
                    text=source
                    if old:
                        self.assertEqual(text.count(old),1,old);text=text.replace(old,new)
                    path=out/(name+'.c');path.write_text(text);exe=out/(cc+'-'+name)
                    build=subprocess.run([cc,*flags,str(path),str(ROOT/'tests/records_bridge_driver.c'),'-o',str(exe)],capture_output=True,text=True)
                    self.assertEqual(build.returncode,0,build.stderr[-5000:])
                    run=subprocess.run([str(exe)],capture_output=True,text=True)
                    if name=='good':self.assertEqual(run.returncode,0,(cc,run.returncode,run.stderr))
                    else:self.assertNotEqual(run.returncode,0,(cc,name,'control survived'))

if __name__=='__main__':unittest.main()
