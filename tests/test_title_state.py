"""Compile the extracted original title update, with presentation-only bindings."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from title_source import closure,sound_header,SOURCE,DECOMP


@unittest.skipUnless(SOURCE.is_file(),'pinned source submodule required')
@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux host compilers required')
class TitleStateTests(unittest.TestCase):
    def test_source_timeline_and_negative_controls(self):
        source=closure(SOURCE.read_text()).decode()
        with tempfile.TemporaryDirectory()as td:
            tmp=Path(td)
            (tmp/'title_game_modes.h').write_bytes((DECOMP/'include/fzx_game.h').read_bytes())
            (tmp/'title_sound_ids.h').write_bytes(sound_header((DECOMP/'include/sfx.h').read_text()))
            def run(text,cc):
                (tmp/'title.c').write_text(text)
                exe=tmp/cc
                p=subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(ROOT/'include'),'-I'+str(tmp),
                    str(tmp/'title.c'),str(ROOT/'tests/title_state_driver.c'),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            outputs=[]
            for cc in ('gcc','clang'):
                p=run(source,cc);self.assertEqual(p.returncode,0,p.stdout+p.stderr);outputs.append(p.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            for before,after in [('D_i4_8011D790 < 80','D_i4_8011D790 < 79'),
                                 ('buttons & (uint16_t)~state->held','buttons'),
                                 ('state->timer<95','state->timer<94'),
                                 ('if (gControllersConnected == 0)','if (0)')]:
                self.assertEqual(source.count(before),1)
                self.assertNotEqual(run(source.replace(before,after),'gcc').returncode,0)


if __name__=='__main__':unittest.main()
