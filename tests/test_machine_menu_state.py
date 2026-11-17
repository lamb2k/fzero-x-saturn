"""Original machine choice/settings: HOST compiler parity and wrong-behavior controls."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from machine_menu_source import closure,SOURCE,DECOMP
from title_source import sound_header

@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux host compilers required')
@unittest.skipUnless(SOURCE.is_file(),'pinned source required')
class MachineMenuStateTests(unittest.TestCase):
    def test_source_timeline_and_controls(self):
        source=closure(SOURCE.read_text()).decode()
        with tempfile.TemporaryDirectory()as td:
            tmp=Path(td)
            (tmp/'machine_menu_game_modes.h').write_bytes((DECOMP/'include/fzx_game.h').read_bytes())
            (tmp/'machine_menu_sound_ids.h').write_bytes(sound_header((DECOMP/'include/sfx.h').read_text()))
            def run(text,cc):
                (tmp/'machine.c').write_text(text);exe=tmp/cc
                p=subprocess.run([cc,'-O2','-std=c11','-fsigned-char','-ffp-contract=off',
                    '-Wall','-Wextra','-Werror','-Wshadow','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),'-I'+str(tmp),str(tmp/'machine.c'),
                    str(ROOT/'tests/machine_menu_state_driver.c'),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            outputs=[]
            for cc in ('gcc','clang'):
                p=run(source,cc);self.assertEqual(p.returncode,0,p.stdout+p.stderr);outputs.append(p.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            controls=[('sMachineSelectIndex[i] += 5;','sMachineSelectIndex[i] += 4;'),
                      ('var_fa1 = 0.0078125f;','var_fa1 = 0.015625f;'),
                      ('gPlayerEngine[i] = 1.0f;','gPlayerEngine[i] = 0.99f;'),
                      ('if (gTransitionState != TRANSITION_INACTIVE)','if (0)'),
                      ('buttons & (uint16_t)~s->held','buttons'),
                      ('s->same_input_ticks>=15','s->same_input_ticks>=14'),
                      ('return GAMEMODE_LX_MACHINE_SETTINGS;','return GAMEMODE_FLX_COURSE_SELECT;'),
                      ('*out=s->snapshot;','((fzx_machine_menu_state_t *)s)->select_phase++; *out=s->snapshot;')]
            for before,after in controls:
                self.assertEqual(source.count(before),1,before)
                p=run(source.replace(before,after),'gcc')
                self.assertNotEqual(p.returncode,0,'Control survived: '+before)
            p=run(source.replace('top -= 8;','top -= 7;'),'gcc')
            self.assertNotEqual(p.returncode,0,'Glyph baseline control survived')

if __name__=='__main__':unittest.main()
