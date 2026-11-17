"""HOST schedule/RNG evidence only; no native reset implementation claim."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from import_source_art import ArtError
from retry_flow_source import GAME,MENUS,RACE,TRANSITION,closure,ids_header,generate


@unittest.skipUnless(GAME.is_file(),'pinned source required')
class Extraction(unittest.TestCase):
    def test_drift_rejected(self):
        for key,path,old,new in [
            ('game',GAME,'        case GAMEMODE_CHANGE_START_RELOAD:','        case CHANGED_RELOAD:'),
            ('menus',MENUS,'        if (!sMenuOptionTriggered && (D_i3_801419A6 == 0)) {',
             '        if (!sMenuOptionTriggered && (D_i3_801419A6 == 1)) {'),
            ('menus',MENUS,'sVsSlotInitialSpeed[VS_SLOT_0] =','sVsSlotInitialSpeed[0] ='),
            ('menus',MENUS,'for (i = 0; i < 4; i++) {','for (i = 0; i < 3; i++) {'),
            ('race',RACE,'void func_i2_80103A70(void)', 'void ChangedReset(void)'),
            ('transition',TRANSITION,'void Transition_FadeUpdate(Transition* transition)',
             'void ChangedFadeUpdate(Transition* transition)')]:
            with self.subTest(anchor=old),self.assertRaises(ArtError):
                closure(**{key:path.read_text().replace(old,new)})

    def test_generation_deterministic(self):
        self.assertEqual(closure(),closure())
        self.assertEqual(ids_header(),ids_header())


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux GCC/Clang required')
@unittest.skipUnless(GAME.is_file(),'pinned source required')
class State(unittest.TestCase):
    def test_schedule_rng_and_negative_controls(self):
        source=closure().decode()
        with tempfile.TemporaryDirectory(prefix='fzx-retry-flow-') as td:
            tmp=Path(td);(tmp/'retry_flow_ids.h').write_bytes(ids_header())
            def run(code,cc):
                (tmp/'source.c').write_text(code)
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow',
                    '-fsigned-char','-fno-fast-math','-ffp-contract=off',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(ROOT/'include'),
                    '-I'+str(tmp),str(tmp/'source.c'),str(ROOT/'tests/retry_flow_driver.c'),'-o',str(tmp/'test')]
                got=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(got.returncode,0,got.stdout+got.stderr)
                return subprocess.run([str(tmp/'test')],capture_output=True,text=True)
            reference=None
            controls=[
                ('source_game_before_race();\n    source_menu_dispatch();',
                 'source_menu_dispatch();\n    source_game_before_race();'),
                ('transition->timer >= 2','transition->timer >= 1'),
                ('Transition_FadeInit(transition, 255, 20);','Transition_FadeInit(transition, 255, 19);'),
                ('    Controller_Reset();\n    Racer_Init();','    Racer_Init();\n    Controller_Reset();'),
                ('    Camera_Init();','    (void)&Camera_Init;'),
                ('    ++m->reload_count;','    m->reload_count+=0;'),
                ('for(i=0;i<4;i++)','for(i=0;i<3;i++)'),
                ('++r->calls2;','++r->calls2; r->seed2=0;'),
            ]
            for cc in ('gcc','clang'):
                got=run(source,cc);self.assertEqual(got.returncode,0,got.stdout+got.stderr)
                if reference is None:reference=got.stdout
                self.assertEqual(got.stdout,reference)
                for old,new in controls:
                    with self.subTest(compiler=cc,mutation=old):
                        self.assertIn(old,source)
                        bad=run(source.replace(old,new),cc)
                        self.assertNotEqual(bad.returncode,0,'Control survived '+old)
                        self.assertIn('retry check line',bad.stderr)
            print('retry HOST GCC/Clang: 80 ticks, 25 live RNG boundaries, 8 controls/compiler PASS; transcript sha256='+
                  hashlib.sha256(reference.encode()).hexdigest())


if __name__=='__main__':unittest.main()
