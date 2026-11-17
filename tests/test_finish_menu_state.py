"""Source finish-menu boundary, navigation and both RNG streams; no reload claim."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from import_source_art import ArtError
from finish_menu_source import SOURCE,closure,ids_header
from records_source import function
from results_source import select_us


def full_draw_reference(code):
    """Run the uncut original drawer with inert graphics-only operations.

    State, navigation, dialogs and audio calls remain original. This checks
    that dropping its graphics-only span did not drop a state side effect.
    It does not qualify the omitted rendering or any reload consumer.
    """
    raw=function(select_us(SOURCE.read_text()),'Menus_DrawTimeAttackFinishMenu')
    raw=raw.replace('    gDPPipeSync(gfx++);',
        '    m->snapshot.draw_selection=sRaceMenuOptionIndex;\n'
        '    m->snapshot.draw_reveal=sGeneralRaceMenuScissorBoxTimer;\n'
        '    gDPPipeSync(gfx++);',1)
    mocks=r'''
#define TIME_ATTACK_FINISH_SHIFT 0
#define FONT_SET_5 5
#define RACE_MENU_RETRY 0
#define RACE_MENU_GHOST_SAVE 1
#define RACE_MENU_SETTINGS 2
#define RACE_MENU_CHANGE_MACHINE 3
#define RACE_MENU_CHANGE_COURSE 4
#define RACE_MENU_QUIT 5
#define gDPPipeSync(...) ((void)0)
#define gDPSetScissor(...) ((void)0)
#define gSPDisplayList(...) ((void)0)
#define gDPLoadTLUT_pal256(...) ((void)0)
#define gDPSetPrimColor(...) ((void)0)
#define gDPSetTextureLUT(...) ((void)0)
static Gfx *Menus_DrawBeveledBox(Gfx *g,int a,int b,int c,int d,int r,int v,int u,int t) {
    (void)a;(void)b;(void)c;(void)d;(void)r;(void)v;(void)u;(void)t;return g;
}
static Gfx *Menus_SetOptionColor(Gfx *g,int a){(void)a;return g;}
static Gfx *Menus_DrawRaceMenuTexture(Gfx *g,int a,int b,int c){(void)a;(void)b;(void)c;return g;}
static Gfx *Font_DrawScaledString(Gfx *g,int a,int b,const char *s,int c,int d,int e,float f,float h) {
    (void)a;(void)b;(void)s;(void)c;(void)d;(void)e;(void)f;(void)h;return g;
}
'''
    start=code.index('static Gfx* Menus_DrawTimeAttackFinishMenu')
    end=code.index('static bool valid(',start)
    return code[:start]+mocks+raw+'\n'+code[end:]


@unittest.skipUnless(SOURCE.is_file(),'pinned source required')
class Extraction(unittest.TestCase):
    def test_drift_rejected(self):
        text=SOURCE.read_text()
        for old,new in [('sGpResultsEndMenuScissorBoxTimer = sGeneralRaceMenuScissorBoxTimer = 60;',
                         'sGpResultsEndMenuScissorBoxTimer = sGeneralRaceMenuScissorBoxTimer = 61;'),
                        ('s32 Menus_UpdateHighlightedOptionVertical(', 's32 ChangedVertical('),
                        ('sMenuHighlightedOptionMoveTimer = 0;', 'sMenuHighlightedOptionMoveTimer = 1;'),
                        ('Gfx* Menus_DrawTimeAttackFinishMenu(', 'Gfx* ChangedFinishMenu(')]:
            with self.subTest(anchor=old),self.assertRaises(ArtError):closure(text.replace(old,new))


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux GCC/Clang required')
@unittest.skipUnless(SOURCE.is_file(),'pinned source required')
class State(unittest.TestCase):
    def test_state_and_controls(self):
        code=closure(SOURCE.read_text()).decode()
        with tempfile.TemporaryDirectory(prefix='fzx-finish-menu-') as td:
            tmp=Path(td);(tmp/'finish_menu_source_ids.h').write_bytes(ids_header())
            def run(text,cc):
                (tmp/'source.c').write_text(text)
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow',
                    '-fsigned-char','-fno-fast-math','-ffp-contract=off',
                    '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(ROOT/'include'),
                    '-I'+str(tmp),str(tmp/'source.c'),str(ROOT/'tests/finish_menu_state_driver.c'),'-o',str(tmp/'test')]
                built=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                return subprocess.run([str(tmp/'test')],capture_output=True,text=True)
            expected=None
            for cc in ('gcc','clang'):
                got=run(code,cc);self.assertEqual(got.returncode,0,got.stdout+got.stderr)
                if expected is None:expected=got.stdout
                self.assertEqual(expected,got.stdout)
                reference=run(full_draw_reference(code),cc)
                self.assertEqual(reference.returncode,0,reference.stdout+reference.stderr)
                self.assertEqual(reference.stdout,expected,'Uncut original state transcript differs')
            controls=[('    Math_Rand2();','    /* omitted second RNG */'),
                      ('stickY > 50','stickY >= 50'),
                      ('sMenuHighlightedOptionMoveTimer == 60','sMenuHighlightedOptionMoveTimer == 61'),
                      ('sMenuHighlightedOptionMoveTimer = 59;','sMenuHighlightedOptionMoveTimer = 58;'),
                      ('sGeneralRaceMenuScissorBoxTimer--;','sGeneralRaceMenuScissorBoxTimer-=2;'),
                      ('sRaceMenuOptionIndex++;','sRaceMenuOptionIndex+=2;'),
                      ('sMenuStateFlags |= MENU_STATE_RETRY;','sMenuStateFlags |= MENU_STATE_QUIT;')]
            for old,new in controls:
                with self.subTest(control=old):
                    self.assertIn(old,code)
                    # Keep the now-unreferenced helper live for strict builds.
                    mutated=code.replace(old,new)
                    if 'omitted second RNG' in new:mutated=mutated.replace(new,'    (void)&Math_Rand2;')
                    got=run(mutated,'gcc');self.assertNotEqual(got.returncode,0,'Control survived '+old)
                    self.assertIn('finish menu check line',got.stderr)
            print('finish menu HOST GCC/Clang and seven negative controls PASS')


if __name__=='__main__':unittest.main()
