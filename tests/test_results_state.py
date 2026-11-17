"""Original results extraction, HOST state/packet timing and failing controls."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from import_source_art import ArtError
from results_source import closure,ids_header,select_us,SOURCE,matrix_bridge,matrix_host_support


class ResultsExtractionTests(unittest.TestCase):
    def test_condition_boundary(self):
        self.assertEqual(select_us('#if defined(PORT) && defined(EXPANSION_KIT)\nx\n#else\ny\n#endif'), 'y\n')
        self.assertEqual(select_us('#ifdef VERSION_US\nx\n#elif defined(PORT)\ny\n#else\nz\n#endif'), 'x\n')
        with self.assertRaises(ArtError):select_us('#ifdef UNREVIEWED\nx\n#endif')

    @unittest.skipUnless(SOURCE.is_file(),'pinned source required')
    def test_extraction_drift(self):
        source=SOURCE.read_text()
        with self.assertRaises(ArtError):closure(source.replace('void Menus_InitFinish(', 'void Renamed_InitFinish(',1))
        with self.assertRaises(ArtError):closure(source.replace('if (sTimeAttackResultsTimer < 400) {',
                                                               'if (sTimeAttackResultsTimer < 401) {',1))


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux HOST compilers required')
@unittest.skipUnless(SOURCE.is_file(),'pinned source required')
class ResultsStateTests(unittest.TestCase):
    def test_record_pulse_strict_float_and_fast_math_control(self):
        # Yaul's default specs add fast-math. The source closure must retain
        # the original sequence of rounded binary32 operations instead.
        source=closure(SOURCE.read_text()).decode()+r'''
#include <stdio.h>
int main(void) {
    fzx_results_state_t state={0};m=&state;Gfx dummy=0;
    for(unsigned frame=0;frame<32;++frame) {
        state.input.frame=frame;func_8007F090(&dummy,255,0,0);
        printf("%u %u %u %u\n",draw_color[0],draw_color[1],draw_color[2],draw_color[3]);
    }
    return 0;
}
'''
        f32=lambda x:struct.unpack('f',struct.pack('f',x))[0]
        expected=[]
        for frame in range(32):
            fraction=f32((frame&15)/15)
            shade=int(f32(255*fraction) if frame&16 else f32(f32(-255*fraction)+255))
            expected.append(f'255 {shade} {shade} 255')
        expected='\n'.join(expected)+'\n'
        with tempfile.TemporaryDirectory(prefix='fzx-results-pulse-')as td:
            tmp=Path(td);(tmp/'results_source_ids.h').write_bytes(ids_header());(tmp/'pulse.c').write_text(source)
            for cc,fast in [('gcc',False),('clang',False),('gcc',True)]:
                exe=tmp/'pulse'
                args=[cc,'-O2','-std=c11','-fsigned-char','-ffp-contract=off',
                    '-ffast-math'if fast else'-fno-fast-math','-I'+str(ROOT/'include'),'-I'+str(tmp),
                    str(tmp/'pulse.c'),'-o',str(exe)]
                p=subprocess.run(args,capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                output=subprocess.check_output([str(exe)],text=True)
                if fast:self.assertNotEqual(output,expected,'fast-math pulse control survived')
                else:self.assertEqual(output,expected)

    def test_original_finish_matrices(self):
        source=matrix_host_support().decode()+matrix_bridge().decode().replace('#include "global.h"','')
        source+='\n#include "'+str(ROOT/'tests/results_state_driver.c')+'"\n'
        with tempfile.TemporaryDirectory(prefix='fzx-results-matrix-') as td:
            tmp=Path(td);(tmp/'results_source_ids.h').write_bytes(ids_header())
            def run(text,cc):
                (tmp/'matrix.c').write_text(text);exe=tmp/cc
                p=subprocess.run([cc,'-O2','-std=c11','-DFZX_RESULTS_MATRIX_DRIVER=1','-ffp-contract=off',
                    '-Wall','-Wextra','-Werror','-Wshadow','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),'-I'+str(tmp),str(tmp/'matrix.c'),'-lm','-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            outputs=[]
            for cc in ('gcc','clang'):
                p=run(source,cc);self.assertEqual(p.returncode,0,p.stdout+p.stderr);outputs.append(p.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            for before,after in [('Matrix_AffineMultiply(&view_f,&model_f,&combined_f);',
                                   'Matrix_AffineMultiply(&model_f,&view_f,&combined_f);'),
                                  ('const s32 angle=f->angle;','const s32 angle=-f->angle;')]:
                self.assertEqual(source.count(before),1)
                p=run(source.replace(before,after),'gcc');self.assertNotEqual(p.returncode,0,'Matrix control survived')

    def test_source_timeline_and_controls(self):
        source=closure(SOURCE.read_text()).decode()
        with tempfile.TemporaryDirectory(prefix='fzx-results-state-') as td:
            tmp=Path(td);(tmp/'results_source_ids.h').write_bytes(ids_header())
            def run(text,cc):
                (tmp/'results.c').write_text(text);exe=tmp/cc
                p=subprocess.run([cc,'-O2','-std=c11','-fsigned-char','-ffp-contract=off',
                    '-Wall','-Wextra','-Werror','-Wshadow','-fsanitize=undefined','-fno-sanitize-recover=all',
                    '-I'+str(ROOT/'include'),'-I'+str(tmp),str(tmp/'results.c'),
                    str(ROOT/'tests/results_state_driver.c'),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)
                return subprocess.run([str(exe)],capture_output=True,text=True)
            outputs=[]
            for cc in ('gcc','clang'):
                p=run(source,cc);self.assertEqual(p.returncode,0,p.stdout+p.stderr);outputs.append(p.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            controls=[
                ('sFinishedSuccessTime < sPlayerFinishTimer[i]', 'sFinishedSuccessTime <= sPlayerFinishTimer[i]'),
                ('sPlayerWinnerLoserFinishGameoverTimer[playerIndex] == 2', 'sPlayerWinnerLoserFinishGameoverTimer[playerIndex] == 3'),
                ('sPlayerResultsTimer[playerIndex] > 100', 'sPlayerResultsTimer[playerIndex] >= 100'),
                ('sTimeAttackResultsTimer += 8;', 'sTimeAttackResultsTimer += 7;'),
                ('courseRecords->timeRecord[i] = MAX_TIMER;', 'courseRecords->timeRecord[i] = 0;'),
                ('gRacers[0].raceTime < gCurrentCourseInfo->timeRecord[i]', 'gRacers[0].raceTime <= gCurrentCourseInfo->timeRecord[i]'),
                ('source_sync_input();source_consume_save();', 's->input=copy;source_sync_input();source_consume_save();'),
                ('top -= 16;', 'top -= 15;'),
                ('x2q/4+draw_copy', 'x2q/4'),
                ('*out=s->snapshot;', '((fzx_results_state_t *)s)->finish_timer[0]++; *out=s->snapshot;'),
            ]
            for before,after in controls:
                self.assertIn(before,source)
                p=run(source.replace(before,after),'gcc')
                self.assertNotEqual(p.returncode,0,'Control survived: '+before)


if __name__=='__main__':unittest.main()
