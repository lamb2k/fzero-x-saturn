"""Actual stream-enabled frame recorder with deterministic clock, HOST only."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
import struct
from tests.test_frame_profile import decode

ROOT = Path(__file__).resolve().parents[1]


class StreamDecoder(unittest.TestCase):
    def test_samples_percentiles_and_rejections(self):
        rows = []
        for i in range(4):
            start = 0xffff0000 + i * 140000
            times = [(start + j * 16000) & 0xffffffff for j in range(8)]
            rows.extend([i * 5, (i + 1) * 5, *times, (start + 126000) & 0xffffffff,
                         10 * i, 10 * i + 9, 14000, 5, 3])
        header = [0x465a4650, 2, 1, 0x290000, 4, 4096, 64, 0x2d8000, 0, 14000, 60, 1, 4, 1, 0, 7]
        road = [0x43335046, 1, 0x2d0000, 4096, 8, 4, 0x2d8000, 0]
        counters = [4, 0, 0, 0, 0, 0, 0, 0] * 7
        counters[32:40] = [4, 1000, 0, 100, 0, 10, 40, 400]
        samples = [100, 10, 200, 20, 300, 30, 400, 40]

        def packed(values):
            return struct.pack('>' + str(len(values)) + 'I', *values).hex()

        data = dict(header_hex=packed(header), rows_hex=packed(rows), guards_intact=True,
                    intentional_holds=0, subsystems_hex=packed(counters),
                    road_profile_header_hex=packed(road), road_profile_rows_hex=packed(samples))
        result = decode(data)['road_samples']
        self.assertEqual(result['commands']['p99'], 40)
        self.assertEqual(result['commands']['mean'], 25)
        self.assertAlmostEqual(result['cpu_ms']['p99'], 400 / 840)
        self.assertAlmostEqual(result['cpu_ms']['mean'], 250 / 840)
        for index, value in ((0, 0), (1, 2), (2, road[2] + 32), (3, 4095), (4, 16),
                             (5, 3), (5, 4097), (6, road[6] + 32), (7, 1)):
            changed = road.copy()
            changed[index] = value
            with self.subTest(layout=index, value=value):
                with self.assertRaisesRegex(ValueError, 'road profile layout'):
                    decode(dict(data, road_profile_header_hex=packed(changed)))
        with self.assertRaisesRegex(ValueError, 'road profile layout'):
            decode(dict(data, road_profile_rows_hex=packed(samples[:-2])))
        for index in (0, 1):
            changed = samples.copy()
            changed[index] += 1
            with self.assertRaisesRegex(ValueError, 'road samples disagree'):
                decode(dict(data, road_profile_rows_hex=packed(changed)))
        # Keep aggregate totals valid but make one row's road duration exceed
        # its scene duration. Other rows retain enough time for aggregate gate.
        narrow = rows.copy()
        narrow[7] = (narrow[6] + 50) & 0xffffffff
        with self.assertRaisesRegex(ValueError, 'road samples disagree'):
            decode(dict(data, rows_hex=packed(narrow)))
        # Totals unchanged: mismatched sample peak or command extrema still fail.
        for changed in ([200, 10, 200, 20, 300, 30, 300, 40],
                        [100, 11, 200, 19, 300, 30, 400, 40]):
            with self.assertRaisesRegex(ValueError, 'road samples disagree'):
                decode(dict(data, road_profile_rows_hex=packed(changed)))


DRIVER = r'''
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>
#include "frame_profile.c"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);return 1;}}while(0)
static uint16_t raw;
uint16_t cpu_frt_count_get(void){return raw;}
static void scene(unsigned subsystem,uint16_t ticks,uint32_t commands){
    fzx_fp_scene_begin();raw=(uint16_t)(raw+ticks);fzx_fp_scene_end(subsystem,commands);
}
int main(void){
    CHECK(mmap((void *)0x200000,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    CHECK(mmap((void *)0xfffff000,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=MAP_FAILED);
    memset((void *)0x200000,0xa5,0x100000);
    CHECK(!fzx_fp_init(0x2ffff0,60));
    CHECK(!fzx_fp_init(0x2b8000,60)); /* old64B rows fit; extra road rows do not */
    CHECK(fzx_fp_init(0x290000,60));
    CHECK(sizeof(row_t)==64&&sizeof(road_row_t)==8);
    CHECK(fzx_frame_profile[6]==64&&fzx_frame_profile[5]==4096);
    CHECK(fzx_frame_road_profile[0]==0x43335046u&&fzx_frame_road_profile[1]==1);
    CHECK(fzx_frame_road_profile[2]==fzx_frame_profile[3]+4096*64);
    CHECK(fzx_frame_road_profile[3]==4096&&fzx_frame_road_profile[4]==8&&!fzx_frame_road_profile[5]);
    CHECK(fzx_frame_road_profile[6]==fzx_frame_profile[7]&&fzx_frame_road_profile[6]==fzx_frame_road_profile[2]+4096*8);
    CHECK(!fzx_frame_road_profile[7]);
    for(unsigned i=0;i<8;++i){CHECK(((uint32_t *)rows)[(int)i-8]==GUARD);
        CHECK(((uint32_t *)(uintptr_t)fzx_frame_road_profile[6])[i]==GUARD);}
    raw=65000;fzx_fp_vblank(raw,1);
    fzx_fp_begin(0);CHECK(!current);scene(4,3,5);CHECK(!fzx_frame_road_profile[5]);
    fzx_fp_restart();fzx_fp_begin(10);
    CHECK(current==rows&&road_rows[0].ticks==0&&road_rows[0].commands==0&&fzx_frame_road_profile[5]==1);
    scene(4,10,90);scene(1,21,701);scene(6,31,702);scene(7,41,703);scene(4,20,150);
    CHECK(road_rows[0].ticks==30&&road_rows[0].commands==240);
    CHECK(fzx_frame_subsystems[4][0]==2&&fzx_frame_subsystems[4][1]==30&&fzx_frame_subsystems[4][3]==240);
    fzx_fp_submit(11);fzx_fp_present();fzx_fp_return();CHECK(rows[0].flags==3&&fzx_frame_profile[12]==1);
    fzx_fp_begin(11);CHECK(!road_rows[1].ticks&&!road_rows[1].commands&&fzx_frame_road_profile[5]==2);
    scene(4,17,8);CHECK(road_rows[1].ticks==17&&road_rows[1].commands==8);
    CHECK(road_rows[0].ticks==30&&road_rows[0].commands==240);
    fzx_fp_restart();scene(4,51,902);CHECK(!current&&!pending);
    fzx_fp_begin(20);CHECK(fzx_frame_profile[4]==1&&fzx_frame_road_profile[5]==1&&!road_rows[0].ticks&&!road_rows[0].commands);
    CHECK(!fzx_frame_subsystems[4][0]);
    scene(4,12,7);CHECK(road_rows[0].ticks==12&&road_rows[0].commands==7);
    for(uint32_t n=1;n<4096;++n){fzx_fp_begin(n+20);scene(4,2,n);}
    CHECK(fzx_frame_profile[4]==4096&&fzx_frame_road_profile[5]==4096);
    CHECK(road_rows[4095].ticks==2&&road_rows[4095].commands==4095);
    const road_row_t last=road_rows[4095];
    fzx_fp_begin(9000);CHECK(!current&&fzx_frame_profile[2]==2);
    scene(4,100,999);CHECK(!memcmp(&last,road_rows+4095,sizeof(last))&&fzx_frame_road_profile[5]==4096);
    for(unsigned i=0;i<8;++i){CHECK(((uint32_t *)rows)[(int)i-8]==GUARD);
        CHECK(((uint32_t *)(uintptr_t)fzx_frame_road_profile[6])[i]==GUARD);}
    CHECK(*(uint32_t *)(uintptr_t)(fzx_frame_road_profile[6]+32)==0xa5a5a5a5u);
    puts("FRAME_PROFILE_STREAM_HOST_PASS ABI rows accumulated road-only restart capacity finalguard");return 0;
}
'''


@unittest.skipUnless(os.name != 'nt' and shutil.which('gcc') and shutil.which('clang'),
                     'Linux HOST compilers required')
class FrameProfileStream(unittest.TestCase):
    def test_road_rows_and_controls(self):
        source = (ROOT / 'saturn/frame_profile.c').read_text()
        with tempfile.TemporaryDirectory(prefix='fzx-frame-stream-') as directory:
            tmp = Path(directory)
            (tmp / 'yaul.h').write_text('#include <stdint.h>\nuint16_t cpu_frt_count_get(void);\n')
            (tmp / 'driver.c').write_text(DRIVER)
            for name in ('frame_profile.h', 'frame_clock.h'):
                (tmp / name).write_text((ROOT / 'saturn' / name).read_text())

            def run(text, cc):
                (tmp / 'frame_profile.c').write_text(text)
                cmd = [cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-fno-sanitize-recover=all', '-DFZX_FRAME_PROFILE=1',
                    '-DFZX_SOURCE_ROAD_STREAM=1', '-I' + str(tmp), str(tmp / 'driver.c'), '-o', str(tmp / 'test')]
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return subprocess.run([str(tmp / 'test')], capture_output=True, text=True, timeout=10)

            for cc in ('gcc', 'clang'):
                result = run(source, cc)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(cc + ': ' + result.stdout.strip())
            for before, after in (
                ('end+=CAPACITY*sizeof(road_row_t)', 'end+=0*sizeof(road_row_t)'),
                ('if(subsystem==4)', 'if(subsystem==1)'),
                ('r->ticks+=elapsed', 'r->ticks=elapsed'),
                ('r->commands+=commands', 'r->commands=commands'),
                ('road_rows[n].ticks=0;', ''),
                ('road_rows[n].commands=0;', ''),
                ('fzx_frame_road_profile[5]=n+1', 'fzx_frame_road_profile[5]=n'),
                ('if(n>=CAPACITY)', 'if(n>CAPACITY)'),
            ):
                self.assertEqual(source.count(before), 1, before)
                result = run(source.replace(before, after), 'gcc')
                self.assertNotEqual(result.returncode, 0, before)
                self.assertIn('line ', result.stderr)
                print('rejected: ' + before)


if __name__ == '__main__':
    unittest.main()
