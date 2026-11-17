"""ROM-free actual records-scene lifecycle against controlled collaborators."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux HOST compilers required')
class RecordsScene(unittest.TestCase):
    def test_lifecycle_and_controls(self):
        source=(ROOT/'saturn/records_scene.c').read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-records-scene-') as directory:
            tmp=Path(directory)
            # Draw/load discarded by linker GC; tick/restart/guards are actual TU.
            (tmp/'records_identity.h').write_text('#define FZX_RECORDS_UI_BYTES 64u\n#define FZX_RECORDS_UI_HASH 1u\n')
            (tmp/'records_models_identity.h').write_text(
                '#define FZX_RECORDS_MODELS_BYTES 64u\n#define FZX_RECORDS_MODELS_HASH 2u\n'
                '#define FZX_RECORDS_MODELS_MAX_VRAM_BYTES 32u\n#define FZX_RECORDS_MODELS_SIX_VRAM_BYTES 192u\n')
            (tmp/'results_identity.h').write_text(
                '#define FZX_RESULTS_TINT_BYTES 512u\n#define FZX_RESULTS_TINT_BANK_BYTES 32u\n'
                'static const uint32_t fzx_results_tint_offsets[8]={0};\n')
            (tmp/'countdown_identity.h').write_text('#define FZX_COUNTDOWN_TEXTURE_BYTES 256u\n')

            def run(text,compiler):
                (tmp/'records_scene_under_test.c').write_text(text)
                exe=tmp/'scene'
                args=[compiler,'-O2','-std=c11','-Wall','-Wextra','-Werror','-Wshadow',
                      '-Wno-misleading-indentation','-fsanitize=undefined','-fno-sanitize-recover=all',
                      '-ffunction-sections','-fdata-sections','-Wl,--gc-sections']
                args+=['-I'+str(p) for p in (tmp,ROOT/'tests/results_scene_stub',ROOT/'saturn',
                    ROOT/'include',ROOT/'ref',ROOT/'third_party/SH2ZAM/saturn/include')]
                args+=[str(ROOT/'tests/records_scene_driver.c'),'-o',str(exe)]
                build=subprocess.run(args,capture_output=True,text=True)
                self.assertEqual(build.returncode,0,build.stdout+build.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)

            for compiler in ('gcc','clang'):
                result=run(source,compiler)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn('RECORDS_SCENE_LIFECYCLE_PASS',result.stdout)
                print(compiler+': '+result.stdout.strip())
            controls=[
                ('late frame','&fzx_source_records_input,frame,connected','&fzx_source_records_input,frame+1u,connected'),
                ('skip RNG commit','if(scene->snapshot.rng_calls&&','if(false&&scene->snapshot.rng_calls&&'),
                ('step terminal','if(scene->state.snapshot.phase!=FZX_RECORDS_FINISH_MENU_READY)','if(true)'),
                ('discard name','results->records=scene->snapshot.records;','(void)results;'),
                ('retain active on restart','fzx_records_active=0;fzx_records_busy=0;','fzx_records_busy=0;'),
                ('retain texture cache','scene->borrowed=false;scene->camera_checked=false;','scene->borrowed=false;'),
            ]
            for name,before,after in controls:
                self.assertEqual(source.count(before),1,name)
                result=run(source.replace(before,after),'gcc')
                self.assertNotEqual(result.returncode,0,'control survived: '+name)
                self.assertIn('line ',result.stderr)
                print('rejected: '+name)


if __name__=='__main__':unittest.main()
