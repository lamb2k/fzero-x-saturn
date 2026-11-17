"""Bounded native model admission and all passes using synthetic source data."""
from pathlib import Path
import os,shlex,shutil,struct,subprocess,tempfile,unittest
from test_machine_menu_models import fixture
from machine_menu_models import model_pack,chart_pack,fnv
ROOT=Path(__file__).resolve().parents[1]

def compile_driver(cc,out,source=None):
    sh2=ROOT/'third_party/SH2ZAM/saturn'
    sources=['sh2zam.c','sh2zam_transform.c','sh2zam_geometry.c','sh2zam_trig.c','sh2zam_view.c']
    subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-Wconversion','-Wshadow','-Wpedantic',
        '-ffunction-sections','-fdata-sections',*shlex.split(os.environ.get('FZX_TEST_CFLAGS','')),
        '-I'+str(ROOT/'include'),'-I'+str(sh2/'include'),str(source or ROOT/'src/fzx_machine_menu_model.c'),
        str(ROOT/'tests/machine_menu_model_driver.c'),*[str(sh2/'src'/s)for s in sources],
        '-Wl,--gc-sections','-o',str(out)],check=True,capture_output=True)

def assets():
    machines,textures,motion=fixture()
    for m in machines:
        for geometry in (m['geometry'],m['reflection']):
            for v,xyz in zip(geometry['vertices'],[[-100,-100,0],[100,-100,0],[0,100,0]]):
                v['position']=xyz;v['normal']=[0,0,127]
    models=model_pack(machines,textures,motion)[0]
    mini=chart_pack([[0xFFFF]*16 for _ in range(30)],8,2,30,0)[0]
    setting=chart_pack([[0x801F+i for i in range(256)]],16,16,0,0)[0]
    camera=[65536 if i%5==0 else 0 for i in range(16)]+[65536,65536]
    camera[14]=-1000*65536
    return models,mini,setting,struct.pack('>36i',*(camera+camera))

@unittest.skipUnless(shutil.which('gcc') and shutil.which('clang'),'GCC/Clang required')
class NativeModels(unittest.TestCase):
    def test_render_and_rehashed_corruption_controls(self):
        models,mini,setting,camera=assets();outputs=[]
        with tempfile.TemporaryDirectory() as temp:
            d=Path(temp)
            (d/'camera').write_bytes(camera);(d/'slots').write_bytes(bytes(range(30)));(d/'reflection').write_bytes(b'\xff\xff'*256)
            def run(m,c,screen):
                (d/'models').write_bytes(m);(d/'charts').write_bytes(c)
                return subprocess.run([str(d/'driver'),*[str(d/x)for x in ('models','charts','camera','slots','reflection')],str(screen),'0','0'],capture_output=True)
            desc=struct.unpack_from('>24I',models,64)
            bad_models=[]
            for at,fmt,value in ((28,'>I',0xFFFFFFFC),(64+8,'>I',264),(64+16,'>I',133),
                (desc[3],'>H',65535),(desc[5]+2,'>B',3),(desc[7],'>H',65535),
                (struct.unpack_from('>I',models,28)[0]+12,'>i',1)):
                b=bytearray(models);struct.pack_into(fmt,b,at,value);struct.pack_into('>I',b,52,fnv(b[64:]));bad_models.append(bytes(b))
            bad_charts=[]
            for at,value in ((16,31),(24,0xFFFFFFF8),(64,0xFFFFFFF8),(68,0xFFFFFFF8),(72,7),(76,1)):
                b=bytearray(mini);struct.pack_into('>I',b,at,value);struct.pack_into('>I',b,32,fnv(b[64:]));bad_charts.append(bytes(b))
            for cc in ('gcc','clang'):
                compile_driver(cc,d/'driver')
                for chart,screen in ((mini,0),(setting,1)):
                    good=run(models,chart,screen);self.assertEqual(good.returncode,0,(cc,screen,good.stderr));outputs.append(good.stdout)
                for i,b in enumerate(bad_models):self.assertEqual(run(b,mini,0).returncode,1,(cc,'model',i))
                for i,b in enumerate(bad_charts):self.assertEqual(run(models,b,0).returncode,1,(cc,'chart',i))
            self.assertEqual(outputs[:2],outputs[2:])

if __name__=='__main__':unittest.main()
