"""Synthetic cloud-slice and real native-submit gates; no ROM or SDK needed."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import plane_assets


class CloudSliceTests(unittest.TestCase):
    def test_every_variant_and_subtile_retains_bytes(self):
        floor=struct.pack('>2048H',*[0x8000|((i*7)&32767) for i in range(2048)])
        cloud=bytes((i*37+i//64)%256 for i in range(2048))
        combined,fog,mean=plane_assets.atlas(floor,cloud,(221,35,179))
        compact,cfog,cmean=plane_assets.atlas(floor,cloud,(221,35,179),cloud_only=True)
        self.assertEqual((cfog,cmean),(fog,mean))
        self.assertEqual(len(compact),131072)
        def verify(candidate):
            self.assertEqual(candidate,combined[131072:])
            count=0
            for variant in range(8):
                for level in range(4):
                    w,h=64>>level,32>>level
                    for tile in range(1<<(2*level)):
                        size=w*h*2;offset=variant*16384+level*4096+tile*size
                        self.assertEqual(offset%8,0)
                        self.assertLessEqual(offset+size,len(candidate))
                        self.assertEqual(candidate[offset:offset+size],combined[131072+offset:131072+offset+size])
                        count+=1
            self.assertEqual(count,680)
        verify(compact)
        with self.assertRaises(AssertionError):verify(combined[:131072])
        with self.assertRaises(AssertionError):verify(compact[8:]+compact[:8])


@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class CloudNativeTests(unittest.TestCase):
    def test_native_rebase_guards_and_pack_modes(self):
        source=(ROOT/'saturn/planes_native.c').read_text()
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            def build(code,cc,mode,packmode=None,size=None,floor=1,planes=1):
                packmode=mode if packmode is None else packmode
                size=(131072 if packmode else 262144) if size is None else size
                (p/'planes_native.c').write_text(code)
                (p/'planes_identity.h').write_text(
                    '/* Synthetic HOST identity; no retail bytes. */\n'
                    '#define FZX_PLANE_FIXTURE 0\n'
                    f'#define FZX_PLANE_EXPECTED_BYTES {size}u\n'
                    f'#define FZX_PLANE_CLOUD_ONLY_PACK {packmode}\n'
                    '#define FZX_PLANE_HASH 0u\n'
                    'static const uint16_t fzx_plane_fog[3]={1,2,3};\n'
                    'static const uint16_t fzx_plane_mean[3]={4,5,6};\n')
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-fsanitize=undefined','-fno-sanitize-recover=all',
                    f'-DFZX_PLANE_CLOUD_ONLY={mode}',
                    f'-DFZX_SOURCE_FLOOR_RBG={floor}',f'-DFZX_SOURCE_PLANES={planes}',
                    '-I'+str(p),'-I'+str(ROOT/'tests/plane_native_stub'),
                    '-I'+str(ROOT/'saturn'),'-I'+str(ROOT/'include'),'-I'+str(ROOT/'ref'),
                    str(ROOT/'tests/plane_compact_native_driver.c'),'-o',str(p/'gate')]
                result=subprocess.run(command,capture_output=True,text=True)
                return result
            for cc in ('gcc','clang'):
                for mode in (0,1):
                    result=build(source,cc,mode)
                    self.assertEqual(result.returncode,0,result.stderr)
                    run=subprocess.run([str(p/'gate')],capture_output=True,text=True)
                    self.assertEqual(run.returncode,0,run.stderr)
                # Independently reject the wrong generation mode and wrong size.
                self.assertNotEqual(build(source,cc,1,0,131072).returncode,0)
                self.assertNotEqual(build(source,cc,1,1,262144).returncode,0)
                self.assertNotEqual(build(source,cc,0,1,262144).returncode,0)
                self.assertNotEqual(build(source,cc,1,floor=0).returncode,0)
                self.assertNotEqual(build(source,cc,1,planes=0).returncode,0)
            wrong=source.replace('q->offset-FZX_PLANE_LAYER_BYTES','q->offset-FZX_PLANE_LAYER_BYTES+8u')
            self.assertNotEqual(wrong,source)
            result=build(wrong,'gcc',1);self.assertEqual(result.returncode,0,result.stderr)
            self.assertNotEqual(subprocess.run([str(p/'gate')],capture_output=True).returncode,0)


if __name__=='__main__':unittest.main()
