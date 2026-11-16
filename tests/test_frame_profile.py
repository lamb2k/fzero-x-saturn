"""Extended FRT and bounded native recorder, using a deterministic fake IRQ."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
import sys
import struct

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from frame_profile import decode
DRIVER=r'''
#define _GNU_SOURCE
#include <sys/mman.h>
#include <assert.h>
#include "frame_profile.c"
static uint16_t raw;
static unsigned interrupt_read;
uint16_t cpu_frt_count_get(void){
    if(interrupt_read){interrupt_read=0;raw+=14000;fzx_fp_vblank(raw,clock_state.field+1);}
    return raw;
}
static void field(void){raw+=14000;fzx_fp_vblank(raw,clock_state.field+1);}
int main(void){
    assert(mmap((void *)0x200000,0x100000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=(void *)-1);
    assert(mmap((void *)0xfffff000,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)!=(void *)-1);
    assert(!fzx_fp_init(0x2ffff0,60));
    assert(fzx_fp_init(0x290000,60));
    raw=65000;fzx_fp_vblank(raw,1);
    for(unsigned i=0;i<20;i++)field();
    assert(stamp(NULL)==280000 && clock_state.faults==0);
    interrupt_read=1;assert(stamp(NULL)==294000);
    clock_state.epoch=0xfffff000u;field();assert(stamp(NULL)==9904);
    fzx_fp_begin(0);assert(!current);
    fzx_fp_restart();fzx_fp_begin(5);
    fzx_fp_scene_begin();raw+=10;fzx_fp_scene_end(4,9);
    assert(fzx_frame_subsystems[4][0]==1 && fzx_frame_subsystems[4][1]==10 && fzx_frame_subsystems[4][3]==9);
    fzx_frame_subsystems[4][1]=0xfffffffeu;fzx_frame_subsystems[4][3]=0xffffffffu;
    fzx_fp_scene_begin();raw+=4;fzx_fp_scene_end(4,2);
    assert(fzx_frame_subsystems[4][1]==2 && fzx_frame_subsystems[4][2]==1 && fzx_frame_subsystems[4][3]==1 && fzx_frame_subsystems[4][4]==1);
    assert(fzx_frame_subsystems[4][5]==2 && fzx_frame_subsystems[4][6]==9);
    raw-=14;
    for(unsigned i=1;i<6;i++){field();fzx_fp_mark(i);}
    field();fzx_fp_submit(10);field();fzx_fp_present();fzx_fp_return();
    assert(rows[0].flags==3 && rows[0].steps==5 && rows[0].present-rows[0].time[0]==98000);
    assert(fzx_frame_profile[12]==1 && rows[0].time[7]==rows[0].present);
    fzx_fp_begin(10);fzx_fp_submit(15);fzx_fp_begin(15);fzx_fp_submit(20);
    assert(fzx_frame_profile[2]==3);
    fzx_fp_restart();fzx_fp_begin(0);assert(fzx_frame_profile[4]==1 && !pending && fzx_frame_subsystems[4][0]==0);
    fzx_frame_profile[4]=CAPACITY;fzx_fp_begin(1);assert(fzx_frame_profile[2]==2 && !current);
    assert(((uint32_t *)rows)[-1]==GUARD && *(uint32_t *)(uintptr_t)fzx_frame_profile[7]==GUARD);
    raw+=25000;fzx_fp_vblank(raw,clock_state.field+1);assert(clock_state.faults==1);
    return 0;
}
'''

class NativeRecorder(unittest.TestCase):
    def test_decoder_and_negative_controls(self):
        rows=[]
        for i in range(4):
            start=0xffff0000+i*140000
            times=[(start+j*16000)&0xffffffff for j in range(8)]
            rows.extend([i*5,(i+1)*5,*times,(start+126000)&0xffffffff,10*i,10*i+9,14000,5,3])
        h=[0x465a4650,1,1,0x290000,4,4096,64,0x2d0000,0,14000,60,1,4,1,0,0]
        data=dict(header_hex=struct.pack('>16I',*h).hex(),rows_hex=struct.pack('>64I',*rows).hex(),guards_intact=True,intentional_holds=0)
        result=decode(data)
        self.assertEqual(result['delivered_fps'],6)
        self.assertEqual(result['missed_fields'],27)
        pal=rows.copy();palh=h.copy();palh[10]=50
        for i in range(4):
            pal[i*16]=i*6;pal[i*16+1]=(i+1)*6;pal[i*16+14]=6
        paldata=dict(data,header_hex=struct.pack('>16I',*palh).hex(),rows_hex=struct.pack('>64I',*pal).hex())
        self.assertEqual(decode(paldata)['simulation_steps']['maximum'],6)
        with self.assertRaises(ValueError):decode(dict(paldata,header_hex=data['header_hex']))

        h2=h.copy();h2[1]=2;h2[15]=7
        counters=[4,400,0,40,0,10,10,100]*7
        data2=dict(data,header_hex=struct.pack('>16I',*h2).hex(),subsystems_hex=struct.pack('>56I',*counters).hex())
        result2=decode(data2)
        self.assertEqual(result2['subsystems']['road']['commands_per_presented_frame'],10)
        self.assertEqual(result2['subsystems']['road']['frt_ticks_per_command'],10)
        bad=counters.copy();bad[0]=3
        with self.assertRaises(ValueError):decode(dict(data2,subsystems_hex=struct.pack('>56I',*bad).hex()))
        with self.assertRaises(ValueError):decode(dict(data2,subsystems_hex=''))
        bad=counters.copy();bad[1]=80000;bad[7]=20000
        with self.assertRaises(ValueError):decode(dict(data2,subsystems_hex=struct.pack('>56I',*bad).hex()))
        for index,value in [(15,1),(14,6),(16,1),(26,rows[26]-65536)]:
            bad=rows.copy();bad[index]=value
            with self.assertRaises(ValueError):decode(dict(data,rows_hex=struct.pack('>64I',*bad).hex()))
        with self.assertRaises(ValueError):decode(dict(data,intentional_holds=1))
        badh=h.copy();badh[8]=1
        with self.assertRaises(ValueError):decode(dict(data,header_hex=struct.pack('>16I',*badh).hex()))

    @unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'POSIX compilers')
    def test_wrap_interrupt_and_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)
            (p/'yaul.h').write_text('#include <stdint.h>\nuint16_t cpu_frt_count_get(void);\n')
            (p/'driver.c').write_text(DRIVER)
            for cc in ('gcc','clang'):
                for mutant in (False,True):
                    for name in ('frame_profile.c','frame_profile.h','frame_clock.h'):
                        s=(ROOT/'saturn'/name).read_text()
                        if mutant and name=='frame_clock.h':s=s.replace('c->epoch+=gap','c->epoch=(uint16_t)(c->epoch+gap)')
                        (p/name).write_text(s)
                    subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-DFZX_FRAME_PROFILE=1','-I'+tmp,str(p/'driver.c'),'-o',str(p/'probe')],check=True,capture_output=True)
                    r=subprocess.run([str(p/'probe')],capture_output=True)
                    if mutant:self.assertNotEqual(r.returncode,0)
                    else:self.assertEqual(r.returncode,0,r.stderr)

if __name__=='__main__':unittest.main()
