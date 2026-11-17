"""ROM-free actual native GfxPool cartridge wrapper: HOST GCC/Clang UBSan."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
YAUL='''#ifndef TEST_WORKSPACE_YAUL_H
#define TEST_WORKSPACE_YAUL_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define DRAM_CART_ID_4MIB 0x5cu
void *dram_cart_area_get(void);
uint32_t dram_cart_size_get(void);
uint32_t dram_cart_id_get(void);
#endif
'''
ROAD='''#ifndef FZX_ROAD_NATIVE_H
#define FZX_ROAD_NATIVE_H
#include "fzx_road_pack.h"
typedef struct { uint32_t status,cart_id,bytes,hash,sectors,vertices,triangles,chunks; int32_t io_result; uint32_t volume_word; } fzx_road_load_status_t;
extern fzx_road_pack_t fzx_native_road;
extern volatile fzx_road_load_status_t fzx_road_load_status;
#endif
'''
REF='''#ifndef TEST_WORKSPACE_REF_H
#define TEST_WORKSPACE_REF_H
unsigned int ref_race_gfx_bytes(void);
int ref_race_gfx_bind(void *,unsigned int);
#endif
'''


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux GCC/Clang required')
class MachineMenuWorkspaceTests(unittest.TestCase):
    def test_cartridge_pool_and_rejection_controls(self):
        original=(ROOT/'saturn/race_workspace_native.c').read_text()
        with tempfile.TemporaryDirectory() as directory:
            temp=Path(directory)
            for name,text in [('yaul.h',YAUL),('road_native.h',ROAD),('ref.h',REF)]: (temp/name).write_text(text)
            def build(source,compiler,name):
                # Byte-for-byte production wrapper text, copied only to resolve
                # minimal SDK/road/reference shims instead of hardware headers.
                path=temp/(name+'.c');path.write_text(source);exe=temp/name
                command=[compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                         '-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(temp),'-I'+str(ROOT/'saturn'),'-I'+str(ROOT/'include'),
                         str(path),str(ROOT/'tests/machine_menu_workspace_driver.c'),'-o',str(exe)]
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr);return exe
            def run(exe,*args):return subprocess.run([str(exe),*args],capture_output=True,text=True)
            results=[]
            for compiler in ('gcc','clang'):
                result=run(build(original,compiler,compiler))
                self.assertEqual(result.returncode,0,result.stdout+result.stderr);results.append(result.stdout)
                seam='pool[i]=0;';self.assertEqual(original.count(seam),1)
                # Controlled failed-zero-write exercises the readback branch.
                faulty=build(original.replace(seam,'pool[i]=1;'),compiler,compiler+'-write-fault')
                result=run(faulty,'write-fault');self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual(*results)
            seam='before[i]!=GUARD || after[i]!=GUARD';self.assertEqual(original.count(seam),1)
            weakened=build(original.replace(seam,'before[i]!=GUARD'),'gcc','missing-after-guard')
            self.assertNotEqual(run(weakened).returncode,0,'missing after-guard control survived')
            self.assertEqual(original,(ROOT/'saturn/race_workspace_native.c').read_text(),'production wrapper changed during test')


if __name__=='__main__':unittest.main()
