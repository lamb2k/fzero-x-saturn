"""Original records state extraction, source-tick boundaries and failing controls."""
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
from records_source import SOURCE, closure, ids_header
from results_source import SOURCE as RESULTS, closure as results_closure, ids_header as results_ids


@unittest.skipUnless(SOURCE.is_file(),'pinned source required')
class RecordsExtractionTests(unittest.TestCase):
    def test_drift_rejected(self):
        source=SOURCE.read_text()
        for before,after in [('void RecordsEntry_InitNameEntry(', 'void Changed_InitNameEntry('),
                             ('    courseInfo = &gCourseInfos[gCourseIndex];', '    courseInfo = NULL;'),
                             ('        spinningCharacterCompTexInfo =', '        changedSpinner =')]:
            with self.subTest(anchor=before), self.assertRaises(ArtError):
                closure(source.replace(before,after))


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux GCC/Clang required')
@unittest.skipUnless(SOURCE.is_file(),'pinned source required')
class RecordsStateTests(unittest.TestCase):
    def test_source_state_and_controls(self):
        source=closure(SOURCE.read_text()).decode()
        with tempfile.TemporaryDirectory(prefix='fzx-records-state-') as td:
            tmp=Path(td)
            (tmp/'records_source_ids.h').write_bytes(ids_header())
            (tmp/'results_source_ids.h').write_bytes(results_ids())
            (tmp/'results.c').write_bytes(results_closure(RESULTS.read_text()))

            def run(text,cc):
                (tmp/'records.c').write_text(text)
                command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wshadow',
                         '-fsigned-char','-ffp-contract=off','-fno-fast-math',
                         '-fsanitize=undefined','-fno-sanitize-recover=all',
                         '-I'+str(ROOT/'include'),'-I'+str(tmp),str(tmp/'records.c'),
                         str(tmp/'results.c'),str(ROOT/'tests/records_state_driver.c'),
                         '-o',str(tmp/'test')]
                built=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                return subprocess.run([str(tmp/'test')],capture_output=True,text=True)

            outputs=[]
            for cc in ('gcc','clang'):
                got=run(source,cc)
                self.assertEqual(got.returncode,0,got.stdout+got.stderr)
                outputs.append(got.stdout)
            self.assertEqual(outputs[0],outputs[1]);print(outputs[0].strip())
            controls=[
                ('sRecordsEntryNameEntryTransitionTimer > 60', 'sRecordsEntryNameEntryTransitionTimer >= 60'),
                ('courseInfo->recordNames[gCurrentTimeAttackRecordPosition - 1][i]',
                 'courseInfo->recordNames[0][i]'),
                ('gRecordNameEnteredLength--;', '(void)gRecordNameEnteredLength;'),
                ('gInputPressed = gInputButtonPressed | gRetriggeredButtonCurrentPressed;',
                 'gInputPressed = gInputButtonPressed;'),
                ('gRandSeed1 * LCG_MULTIPLIER + LCG_INCREMENT_1;',
                 'gRandSeed1 * LCG_MULTIPLIER + LCG_INCREMENT_1 + 1u;'),
                ('*out=s->snapshot;', '((fzx_records_state_t *)s)->snapshot.tick++; *out=s->snapshot;'),
                ('if(!p->record_position)RecordsEntry_ToRecordsState();',
                 'if(!p->record_position)(void)0;'),
                ('sPreviousRecordsEntryNameEntryState=sRecordsEntryNameEntryState;',
                 '(void)sPreviousRecordsEntryNameEntryState;'),
                ('m->snapshot.spinner_keyboard_index=(int16_t)keyboardIndex;',
                 'm->snapshot.spinner_keyboard_index=(int16_t)(sKeyboardCursorY*10+sKeyboardCursorX);'),
            ]
            for before,after in controls:
                with self.subTest(control=before):
                    self.assertEqual(source.count(before),1)
                    got=run(source.replace(before,after),'gcc')
                    self.assertNotEqual(got.returncode,0,'Control survived: '+before)
                    self.assertIn('records check line',got.stderr)
            print('records HOST 9 negative controls caught')


if __name__=='__main__':
    unittest.main()
