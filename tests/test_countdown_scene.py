"""Guarded scene ownership with synthetic CD/pack/source/draw API mocks."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux GCC/Clang required')
class CountdownSceneTests(unittest.TestCase):
    def test_load_publication_and_guards(self):
        source=(ROOT/'saturn/countdown_scene.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)
            (path/'countdown_identity.h').write_text(
                '#define FZX_COUNTDOWN_BYTES 2352u\n'
                '#define FZX_COUNTDOWN_TEXTURE_BYTES 70400u\n'
                '#define FZX_COUNTDOWN_HASH 0x12345678u\n'
                '#define FZX_COUNTDOWN_TEXTURE_HASH 0x87654321u\n')
            clock=path/'clock.h'
            clock.write_text('#include <stdint.h>\nuint16_t cpu_frt_count_get(void);\n')
            def run(text,cc):
                unit=path/'scene.c';unit.write_text(text);exe=path/'scene'
                args=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion',
                    '-Wshadow','-Wpedantic','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-include',str(clock),'-I'+str(path)]
                for inc in ('tests/countdown_native_stub','saturn','include','ref',
                            'third_party/SH2ZAM/saturn/include'):
                    args.append('-I'+str(ROOT/inc))
                result=subprocess.run(args+[str(unit),str(ROOT/'tests/countdown_scene_driver.c'),
                    '-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            for cc in ('gcc','clang'):
                result=run(source,cc)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            for old,new in (
                ('reserved_bytes<FZX_COUNTDOWN_TEXTURE_BYTES',
                 'reserved_bytes+1u<FZX_COUNTDOWN_TEXTURE_BYTES'),
                ('if(!scene->published)return fail(FZX_COUNTDOWN_SCENE_PUBLISH);',
                 'if(false)return fail(FZX_COUNTDOWN_SCENE_PUBLISH);'),
                ('if(!guards||!okay){*count=0;','if(!guards||!okay){')):
                self.assertEqual(source.count(old),1)
                self.assertNotEqual(run(source.replace(old,new),'gcc').returncode,0,old)


if __name__=='__main__':
    unittest.main()
