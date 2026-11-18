"""ROM-free HOST finish-menu lifecycle and presentation callback qualification.

Runs the production scene TU with mocked simulation/draw/upload collaborators;
fake mapped registers establish branch behavior, never Saturn timing evidence.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux HOST compilers required')
class FinishMenuScene(unittest.TestCase):
    def test_lifecycle_palette_and_controls(self):
        source = (ROOT / 'saturn/records_scene.c').read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-finish-menu-scene-') as directory:
            tmp = Path(directory)
            identities = {
                'records_identity.h': '#define FZX_RECORDS_UI_BYTES 64u\n#define FZX_RECORDS_UI_HASH 1u\n',
                'records_models_identity.h': '#define FZX_RECORDS_MODELS_BYTES 64u\n'
                    '#define FZX_RECORDS_MODELS_HASH 2u\n#define FZX_RECORDS_MODELS_MAX_VRAM_BYTES 32u\n'
                    '#define FZX_RECORDS_MODELS_SIX_VRAM_BYTES 192u\n',
                'results_identity.h': '#define FZX_RESULTS_TINT_BYTES 512u\n'
                    '#define FZX_RESULTS_TINT_BANK_BYTES 32u\n'
                    'static const uint32_t fzx_results_tint_offsets[8]={0};\n',
                'countdown_identity.h': '#define FZX_COUNTDOWN_TEXTURE_BYTES 256u\n',
                'finish_menu_identity.h': '#define FZX_FINISH_MENU_BYTES 64u\n#define FZX_FINISH_MENU_HASH 3u\n',
            }
            for name, text in identities.items():
                (tmp / name).write_text(text)

            def run(text, compiler):
                (tmp / 'records_scene_under_test.c').write_text(text)
                exe = tmp / 'scene'
                args = [compiler, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wshadow',
                        '-Wno-misleading-indentation', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                        '-DFZX_SOURCE_FINISH_MENU=1', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections']
                args += ['-I' + str(p) for p in (tmp, ROOT / 'tests/results_scene_stub',
                    ROOT / 'saturn', ROOT / 'include', ROOT / 'ref', ROOT / 'third_party/SH2ZAM/saturn/include')]
                args += [str(ROOT / 'tests/finish_menu_scene_driver.c'), '-o', str(exe)]
                build = subprocess.run(args, capture_output=True, text=True, timeout=30)
                self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
                return subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)

            for compiler in ('gcc', 'clang'):
                result = run(source, compiler)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('FINISH_MENU_SCENE_HOST_PASS', result.stdout)
                print(compiler + ': ' + result.stdout.strip())
            controls = [
                ('late menu frame', 'ref_finish_menu_input(&input,frame,connected)',
                    'ref_finish_menu_input(&input,frame+1u,connected)'),
                ('skip RNG commit', 'if(scene->menu_snapshot.rng1_calls&&',
                    'if(false&&scene->menu_snapshot.rng1_calls&&'),
                ('step after action', 'if(!scene->menu_snapshot.action)', 'if(true)'),
                ('stale backdrop', 'backdrop.frame=frame;', 'backdrop.frame=frame-1u;'),
                ('restart pending retained', 'scene->menu_borrowed=false;fzx_finish_menu_pending=0;',
                    'scene->menu_borrowed=false;'),
                ('scanout accepted', '!scene->menu_active||!blank||', '!scene->menu_active||'),
                ('stale tick accepted', 'scene->menu_palette.tick!=fzx_finish_menu_pending||', ''),
                ('CRAM mode ignored', '(r[0x0e/2]&0x3000u)!=0||', ''),
                ('sprite profile ignored', 'r[0xe0/2]!=0x1520u||', ''),
                ('priority profile ignored', '(r[0xf0/2]&0x0707u)!=0x0506u||', ''),
                ('palette offset ignored', '(r[0xe6/2]&0x0070u)||', ''),
            ]
            for name, before, after in controls:
                self.assertEqual(source.count(before), 1, name)
                result = run(source.replace(before, after), 'gcc')
                self.assertNotEqual(result.returncode, 0, 'control survived: ' + name)
                self.assertIn('line ', result.stderr)
                print('rejected: ' + name)


if __name__ == '__main__':
    unittest.main()
