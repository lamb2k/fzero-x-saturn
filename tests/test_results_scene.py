"""ROM-free actual-scene lifecycle with synthetic state/input/CD collaborators."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux HOST compilers required')
class ResultsScene(unittest.TestCase):
    def test_clock_restart_guards_and_controls(self):
        source=(ROOT/'saturn/results_scene.c').read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-results-scene-') as directory:
            tmp=Path(directory)
            # Synthetic identities only; load/render functions are discarded by
            # section GC. The actual tick/restart/guard functions remain linked.
            (tmp/'results_identity.h').write_text(
                '#define FZX_RESULTS_UI_BYTES 64u\n#define FZX_RESULTS_UI_HASH 1u\n'
                '#define FZX_RESULTS_TINT_BYTES 512u\n#define FZX_RESULTS_TINT_HASH 2u\n'
                '#define FZX_RESULTS_TINT_BANK_BYTES 32u\n'
                'static const uint32_t fzx_results_tint_offsets[8]={0};\n')
            (tmp/'finish_identity.h').write_text(
                '#define FZX_FINISH_BYTES 64u\n#define FZX_FINISH_HASH 3u\n'
                '#define FZX_FINISH_TEXTURE_BYTES 64u\n#define FZX_FINISH_TEXTURE_HASH 4u\n')
            (tmp/'countdown_identity.h').write_text(
                '#define FZX_COUNTDOWN_TEXTURE_BYTES 256u\n#define FZX_COUNTDOWN_TEXTURE_HASH 5u\n')

            def run(text,compiler):
                (tmp/'results_scene_under_test.c').write_text(text)
                exe=tmp/'scene'
                args=[compiler,'-O2','-std=c11','-Wall','-Wextra','-Werror','-Wshadow',
                      '-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all',
                      '-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
                args+=['-I'+str(path) for path in
                       (tmp,ROOT/'tests/results_scene_stub',ROOT/'saturn',ROOT/'include',
                        ROOT/'ref',ROOT/'third_party/SH2ZAM/saturn/include')]
                args+=[str(ROOT/'tests/results_scene_driver.c'),'-o',str(exe)]
                build=subprocess.run(args,capture_output=True,text=True)
                self.assertEqual(build.returncode,0,build.stdout+build.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)

            for compiler in ('gcc','clang'):
                result=run(source,compiler)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('RESULTS_SCENE_LIFECYCLE_PASS',result.stdout)
                print(compiler+': '+result.stdout.strip())

            controls=[
                ('capped frame','scene->frame+1u,connected',
                 '(scene->frame<12288u?scene->frame+1u:12288u),connected'),
                ('skipped reset','scene->frame=0;scene->published=false;',
                 'scene->published=false;'),
                ('missing reset guard status',
                 'if(!fzx_results_guards())return fail(FZX_RESULTS_GUARD);\n    fzx_results_busy=1;',
                 'if(!fzx_results_guards())return false;\n    fzx_results_busy=1;'),
                ('skipped texture restore','if(fzx_results_texture_borrowed&&',
                 'if(false&&fzx_results_texture_borrowed&&'),
                ('discarded records','&scene->state.config,&scene->state.records',
                 '&scene->state.config,NULL'),
            ]
            for name,before,after in controls:
                self.assertEqual(source.count(before),1,name)
                result=run(source.replace(before,after),'gcc')
                self.assertNotEqual(result.returncode,0,'control survived: '+name)
                self.assertIn('line ',result.stderr,'control must fail a fixture assertion: '+name)
                print('rejected: '+name)


if __name__=='__main__':unittest.main()
