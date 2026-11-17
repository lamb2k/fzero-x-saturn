"""Native preview admission/projection with synthetic source-format tubes."""
from pathlib import Path
import os,shlex,shutil,struct,subprocess,tempfile,unittest,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from course_menu_preview import pack,topology,fnv

def fixture():
    vertices=[[0,0,0],[100,0,0],[0,100,0],[-100,0,-100],[0,0,-100],[0,100,-100],[0,0,0],[100,0,0],[0,100,0]]
    courses=[dict(vertices=vertices,quads=topology(len(vertices))) for _ in range(24)]
    matrix=[float(i%5==0) for i in range(16)]+[1.0,1.0]
    matrix[14]=-1000.0
    return pack(courses,[matrix]*360)[0]

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class NativePreview(unittest.TestCase):
    def test_admission_projection_and_controls(self):
        original=fixture();mutations=[]
        def mutate(at,fmt,value):
            b=bytearray(original);struct.pack_into(fmt,b,at,value)
            struct.pack_into('>I',b,48,fnv(b[64:]));mutations.append(bytes(b))
        mutate(20,'>I',0xffffffff)
        mutate(36,'>I',449)
        mutate(52,'>I',768)
        mutate(64,'>I',1)
        mutate(68,'>I',0xffffffff)
        mutate(struct.unpack_from('>I',original,40)[0],'>H',0xffff)
        mutate(struct.unpack_from('>I',original,44)[0]+12,'>i',1)
        mutate(struct.unpack_from('>I',original,44)[0]+64,'>i',0)
        mutations += [original[:-1],original+bytes(4),bytes(64)]
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);results=[]
            for cc in ('gcc','clang'):
                exe=d/cc
                subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
                    *shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
                    '-I'+str(ROOT/'include'),'-I'+str(ROOT/'third_party/SH2ZAM/saturn/include'),
                    str(ROOT/'src/fzx_course_preview.c'),str(ROOT/'third_party/SH2ZAM/saturn/src/sh2zam_view.c'),
                    str(ROOT/'tests/course_preview_driver.c'),'-o',str(exe)],check=True,capture_output=True)
                def run(data):
                    (d/'pack').write_bytes(data)
                    return subprocess.run([str(exe),str(d/'pack')],capture_output=True)
                good=run(original);self.assertEqual(good.returncode,0,good.stderr)
                results.append(good.stdout)
                for i,b in enumerate(mutations):self.assertEqual(run(b).returncode,1,(cc,i))
            self.assertEqual(results[0],results[1])

if __name__=='__main__':unittest.main()
