"""Original cartridge course selection: compiler parity and failing controls."""
from pathlib import Path
import os,re,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from course_menu_source import closure,us_source,sine_table,SOURCE,DECOMP,ArtError
from title_source import sound_header


@unittest.skipUnless(SOURCE.is_file(),'pinned source submodule required')
class CourseMenuExtractionTests(unittest.TestCase):
    def test_scope_and_sine(self):
        source=us_source(SOURCE.read_text())
        self.assertNotIn('gdx_',source)
        self.assertNotIn('DDSave_',source)
        self.assertIn('sCourseSelectTrackNo < 5',source)
        self.assertIn('MENU_CHANGE_EXIT_COURSE_SELECT',source)
        table=sine_table()
        values=[float.fromhex(x)for x in re.findall(r'(-?0x[\da-f]+\.[\da-f]+p[+-]\d+)f',table)]
        self.assertEqual(len(values),256)
        self.assertEqual(values[0],0.0);self.assertEqual(values[64],1.0);self.assertEqual(values[192],-1.0)
        with self.assertRaises(ArtError):us_source('#if NEW_CONDITION\n#endif')


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux host compilers required')
@unittest.skipUnless(SOURCE.is_file(),'pinned source submodule required')
class CourseMenuStateTests(unittest.TestCase):
    def test_source_timeline_and_controls(self):
        source=closure(SOURCE.read_text()).decode()
        with tempfile.TemporaryDirectory()as td:
            tmp=Path(td)
            (tmp/'course_menu_game_modes.h').write_bytes((DECOMP/'include/fzx_game.h').read_bytes())
            (tmp/'course_menu_sound_ids.h').write_bytes(sound_header((DECOMP/'include/sfx.h').read_text()))
            def run(text,cc):
                (tmp/'course.c').write_text(text);exe=tmp/cc
                p=subprocess.run([cc,'-O2','-std=c11','-fsigned-char','-ffp-contract=off','-Wall','-Wextra','-Werror','-Wshadow',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(ROOT/'include'),'-I'+str(tmp),
                    str(tmp/'course.c'),str(ROOT/'tests/course_menu_state_driver.c'),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            outputs=[]
            for cc in ('gcc','clang'):
                p=run(source,cc);self.assertEqual(p.returncode,0,p.stdout+p.stderr);outputs.append(p.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            controls=[('sCourseSelectTrackNo < 5','sCourseSelectTrackNo < 4'),
                      ('buttons & (uint16_t)~s->held','buttons'),
                      ('s->same_input_ticks>=15','s->same_input_ticks>=14'),
                      ('if (gTransitionState != TRANSITION_INACTIVE)','if (0)'),
                      ('timeRecord < ((ghostInfo.raceTime * 115) / 100)',
                       'timeRecord <= ((ghostInfo.raceTime * 115) / 100)'),
                      ('OBJECT_COUNTER(headerObj) > 12','OBJECT_COUNTER(headerObj) > 11'),
                      ('if (D_i5_801190D0)','if (1)'),
                      ('*out=s->snapshot;','((fzx_course_menu_state_t *)s)->course++; *out=s->snapshot;'),
                      ('return GAMEMODE_FLX_MACHINE_SELECT;','return GAMEMODE_RECORDS;')]
            for before,after in controls:
                self.assertEqual(source.count(before),1,before)
                p=run(source.replace(before,after),'gcc')
                self.assertNotEqual(p.returncode,0,'Control survived: '+before)
            p=run(source.replace('top -= 16;','top -= 15;'),'gcc')
            self.assertNotEqual(p.returncode,0,'Glyph baseline control survived')


if __name__=='__main__':unittest.main()
