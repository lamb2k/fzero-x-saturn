"""Bounded original finish-menu draw capture; HOST evidence only."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from finish_menu_draw_source import closure,MENUS


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux GCC/Clang required')
@unittest.skipUnless(MENUS.is_file(),'pinned source required')
class Draw(unittest.TestCase):
    def test_capture_and_controls(self):
        code=closure().decode()
        with tempfile.TemporaryDirectory(prefix='fzx-finish-draw-') as directory:
            tmp=Path(directory)
            def run(text,cc):
                (tmp/'draw.c').write_text(text)
                built=subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-Wshadow','-fsigned-char','-fno-fast-math','-ffp-contract=off',
                    '-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),str(tmp/'draw.c'),
                    str(ROOT/'tests/finish_menu_draw_driver.c'),'-o',str(tmp/'test')],
                    capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stderr)
                return subprocess.run([str(tmp/'test')],capture_output=True,text=True)
            expected=None
            for cc in ('gcc','clang'):
                result=run(code,cc)
                self.assertEqual(result.returncode,0,result.stderr)
                if expected is None:expected=result.stdout
                self.assertEqual(result.stdout,expected)
                for old,new in (
                    ('sGeneralRaceMenuScissorBoxTimer + 205','sGeneralRaceMenuScissorBoxTimer + 204'),
                    ('128, 128, 128, 255','127, 128, 128, 255'),
                    ('glyph->width*sx','glyph->width*(sx+0.1f)'),
                    ('p->image=glyph->index','p->image=glyph->index+1'),
                    ('p->image=(uint16_t)image','p->image=(uint16_t)(image+1)'),
                    ('m->snapshot.frame=s->input.frame','m->snapshot.frame=s->input.frame+1')):
                    with self.subTest(compiler=cc,mutation=old):
                        self.assertIn(old,code)
                        self.assertNotEqual(run(code.replace(old,new,1),cc).returncode,0)


if __name__=='__main__':unittest.main()
