"""Original US cartridge menu closure, compiler parity and deliberate controls."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from menu_source import closure,cartridge,SOURCE,DECOMP,ArtError
from title_source import sound_header


@unittest.skipUnless(SOURCE.is_file(),'pinned source submodule required')
class MenuExtractionTests(unittest.TestCase):
    def test_us_cartridge_branch(self):
        text=cartridge(SOURCE.read_text())
        self.assertNotIn('gdx_',text)
        self.assertIn('gSelectedMode += 4;',text)
        self.assertNotIn('gSelectedMode = gdx_main_menu_navigate',text)
        for invalid in ('#ifdef NEW_CONFIG\n#endif','#else','#ifdef PORT\n'):
            with self.assertRaises(ArtError):cartridge(invalid)


@unittest.skipUnless(SOURCE.is_file(),'pinned source submodule required')
@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux host compilers required')
class MenuStateTests(unittest.TestCase):
    def test_source_navigation_draws_and_negative_controls(self):
        source=closure(SOURCE.read_text()).decode()
        with tempfile.TemporaryDirectory()as td:
            tmp=Path(td)
            (tmp/'menu_game_modes.h').write_bytes((DECOMP/'include/fzx_game.h').read_bytes())
            (tmp/'menu_sound_ids.h').write_bytes(sound_header((DECOMP/'include/sfx.h').read_text()))
            def run(text,cc):
                (tmp/'menu.c').write_text(text)
                exe=tmp/cc
                p=subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(ROOT/'include'),'-I'+str(tmp),
                    str(tmp/'menu.c'),str(ROOT/'tests/menu_state_driver.c'),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            outputs=[]
            for cc in ('gcc','clang'):
                p=run(source,cc);self.assertEqual(p.returncode,0,p.stdout+p.stderr);outputs.append(p.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            controls=[('gSelectedMode += 4;','gSelectedMode += 3;'),
                      ('buttons & (uint16_t)~s->held','buttons'),
                      ('s->same_input_ticks>=15','s->same_input_ticks>=14'),
                      ('if (gTransitionState != TRANSITION_INACTIVE)','if (0)'),
                      ('OBJECT_COUNTER(Object_Get(OBJECT_MAIN_MENU_MODE_SIGN_3)) == 12',
                       'OBJECT_COUNTER(Object_Get(OBJECT_MAIN_MENU_MODE_SIGN_3)) == 11'),
                      ('*out=s->snapshot;','((fzx_menu_state_t *)s)->objects[3].counter++; *out=s->snapshot;'),
                      ('d->a=m->color[3]','d->a=255')]
            for before,after in controls:
                self.assertEqual(source.count(before),1,before)
                p=run(source.replace(before,after),'gcc')
                self.assertNotEqual(p.returncode,0,'Control survived: '+before)


if __name__=='__main__':unittest.main()
