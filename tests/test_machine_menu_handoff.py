"""HOST original-racer handoff: selected tuning/skin, restart and off parity."""
from pathlib import Path
import contextlib
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'ref'))
from fixture_course import record


@unittest.skipUnless(os.name!='nt' and shutil.which('gcc') and shutil.which('clang'),'Linux host compilers required')
@unittest.skipUnless((ROOT/'upstream/G-Diffuser/decomp/src/game/racer.c').is_file(),'pinned source required')
class MachineHandoffTests(unittest.TestCase):
    def test_original_racer_handoff(self):
        evidence=os.environ.get('FZX_HANDOFF_EVIDENCE')
        context=contextlib.nullcontext(evidence)if evidence else tempfile.TemporaryDirectory()
        with context as directory:
            out=Path(directory).resolve();out.mkdir(parents=True,exist_ok=True)
            fixture=out/'course.bin';fixture.write_bytes(record())
            build_source=(ROOT/'ref/build.sh').read_text()
            builds={};results={}
            def build(cc,enabled,external=False):
                name=cc+'-'+str(enabled)+('-external'if external else'');base=out/name
                source=build_source
                changes={
                    'ROOT=$(cd "$(dirname "$0")/.." && pwd)':'ROOT='+shlex.quote(str(ROOT)),
                    'OUT=$ROOT/ref/build/${CC}${OPT}':'OUT='+shlex.quote(str(base)),
                    '# Driver side: host headers only.':'compile "$ROOT/tests/machine_menu_handoff_probe.c" "$OUT/handoff_probe.o"\nOBJS="$OBJS $OUT/handoff_probe.o"\n# Driver side: host headers only.',
                    '"$ROOT/ref/$DRIVER.c"':'"$ROOT/tests/machine_menu_handoff_driver.c"',
                    '-DFZX_SOURCE_CAMERA=$CAMERA -DFZX_ROAD_PLAN=$PLAN':'-DFZX_SOURCE_MACHINE_MENU=$MACHINE_MENU -DFZX_SOURCE_CAMERA=$CAMERA -DFZX_ROAD_PLAN=$PLAN',
                }
                for old,new in changes.items():
                    self.assertEqual(source.count(old),1,old);source=source.replace(old,new)
                if external:
                    old='if [ "$MACHINE_MENU" = 1 ]; then CFLAGS="$CFLAGS -DFZX_SOURCE_MACHINE_MENU=1"; fi'
                    self.assertEqual(source.count(old),1)
                    source=source.replace(old,old.replace('-DFZX_SOURCE_MACHINE_MENU=1','-DFZX_SOURCE_MACHINE_MENU=1 -DFZX_RACE_GFX_EXTERNAL=1'))
                script=out/(name+'.sh');script.write_text(source)
                env=dict(os.environ,FZX_SOURCE_MACHINE_MENU=str(enabled),FZX_SOURCE_CAMERA='0',FZX_ROAD_EXPORT='0',FZX_ROAD_PLAN='0',FZX_BACKDROP_STATE='0')
                p=subprocess.run(['bash',str(script),cc,'-O2'],cwd=ROOT,env=env,capture_output=True,text=True)
                (out/(name+'.log')).write_text(p.stdout+p.stderr)
                self.assertEqual(p.returncode,0,(p.stdout+p.stderr)[-6000:])
                path=Path(str(base)+('-machine-menu'if enabled else ''))
                builds[name]=path;return path/'fzx-ref'
            def run(exe,mode,skin,engine,storage=0):
                return subprocess.run([str(exe),str(fixture),str(mode),str(skin),str(engine),str(storage)],capture_output=True,text=True)
            configs=[(0,0,.5),(1,0,.5),(1,0,0),(1,0,1),(1,1,.5),(1,2,.5),(1,3,.5),(2,2,.75)]
            baseline=run(build('gcc',0),0,0,.5)
            self.assertEqual(baseline.returncode,0,baseline.stderr)
            self.assertEqual(baseline.stdout.splitlines()[1],'005ac6ea9e9d2c31','legacy flag-off synthetic trace changed')
            oracle=run(builds['gcc-0']/'fzx-ref',3,0,.5)
            self.assertEqual(oracle.returncode,0,oracle.stderr)
            self.assertNotEqual(baseline.stdout,oracle.stdout,'legacy zero-engine control unexpectedly equals source settings')
            for cc in ('gcc','clang'):
                exe=build(cc,1);rows=[]
                for config in configs:
                    p=run(exe,*config)
                    self.assertEqual(p.returncode,0,(cc,config,p.returncode,p.stderr))
                    rows.append(p.stdout)
                self.assertEqual(rows[0],oracle.stdout,'flag-on default differs from independent source settings handoff')
                self.assertEqual(rows[1],oracle.stdout,'explicit default differs from source settings')
                # Both full simulation and source tuning fields must respond to the slider.
                for i in (2,3):
                    self.assertNotEqual(rows[i].splitlines()[1],rows[0].splitlines()[1],'engine control did not affect simulation')
                    self.assertNotEqual(rows[i].splitlines()[0].split()[7:26],rows[0].splitlines()[0].split()[7:26],'engine control did not affect original tuning')
                # Skin is presentation only; source body colour changes, physics is identical.
                for i in (4,5,6):
                    self.assertEqual(rows[i].splitlines()[1],rows[0].splitlines()[1])
                    self.assertNotEqual(rows[i].splitlines()[0].split()[2:5],rows[0].splitlines()[0].split()[2:5])
                results[cc]=rows
                (out/(cc+'-handoff.txt')).write_text('\n'.join(rows))
            self.assertEqual(results['gcc'],results['clang'])
            external_results={}
            for cc in ('gcc','clang'):
                # Explicit storage must retain the ordinary HOST closure's state.
                ordinary=builds[cc+'-1']/'fzx-ref'
                for config,expected in zip(configs,results[cc]):
                    p=run(ordinary,*config,storage=1)
                    self.assertEqual(p.returncode,0,(cc,'explicit bind',config,p.returncode,p.stderr))
                    self.assertEqual(p.stdout,expected)
                external=build(cc,1,external=True)
                missing=run(external,*configs[0],storage=2)
                self.assertEqual(missing.returncode,1,('missing bind was not rejected before source init',cc,missing.returncode,missing.stderr))
                rows=[]
                for config,expected in zip(configs,results[cc]):
                    p=run(external,*config,storage=1)
                    self.assertEqual(p.returncode,0,(cc,'external pool',config,p.returncode,p.stderr))
                    self.assertEqual(p.stdout,expected,'external pool changed source tuning, skin, physics or restart')
                    rows.append(p.stdout)
                external_results[cc]=rows
                (out/(cc+'-external-handoff.txt')).write_text('\n'.join(rows))
            self.assertEqual(external_results['gcc'],external_results['clang'])
            # Recompile only the isolated preprocessed bridge for wrong-behavior controls.
            path=builds['gcc-1'];preprocessed=(path/'ref_bridge.i').read_text()
            controls=[('engine','gPlayerEngine[0] = sSelectedEngine;','gPlayerEngine[0] = 0.5f;',configs[2]),
                      ('skin','gPlayerMachineSkins[0] = (s16)sSelectedSkin;','gPlayerMachineSkins[0] = 0;',configs[6]),
                      ('skin_range','skin >= 4','skin > 4',configs[7]),
                      ('nan','!(engine >= 0.0f && engine <= 1.0f)','(engine < 0.0f || engine > 1.0f)',configs[7]),
                      ('missing_copy','func_8008969C();',';',configs[6]),
                      ('missing_engine_conversion','func_80089724();',';',configs[1])]
            rejected={}
            for name,old,new,config in controls:
                self.assertEqual(preprocessed.count(old),1,old)
                changed=out/(name+'.i');changed.write_text(preprocessed.replace(old,new))
                obj=out/(name+'.o');exe=out/(name+'-bad')
                command=['gcc','-m32','-msse2','-mfpmath=sse','-fno-fast-math','-fno-unsafe-math-optimizations','-ffp-contract=off','-fno-strict-aliasing','-fno-pic','-fno-builtin','-O2','-std=gnu11','-w','-c',str(changed),'-o',str(obj)]
                p=subprocess.run(command,capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr)
                objects=[str(p)for p in sorted(path.glob('*.o'))if p.name!='ref_bridge.o']
                p=subprocess.run(['gcc','-m32','-no-pie',*objects,str(obj),'-lm','-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stderr)
                p=run(exe,*config);self.assertNotEqual(p.returncode,0,'wrong-behavior control survived: '+name)
                rejected[name]=p.returncode
            self.assertEqual(build_source,(ROOT/'ref/build.sh').read_text(),'shared build changed during gate')
            report=dict(tier='HOST',source='original Racer_Init and Racer_Update',fixture='synthetic course and ROM boot constants',
                        compilers=['gcc-O2','clang-O2'],variants=len(configs),ticks_per_run=600,restarts=len(configs)*2,
                        flag0_legacy_trace_preserved=True,default_matches_independent_source_handoff=True,
                        default_corrects_legacy_zero_engine=True,gcc_clang_parity=True,invalid_inputs=10,
                        external_pool=dict(compilers=['gcc-O2','clang-O2'],variants=len(configs),ticks_per_run=600,
                            source_and_restart_parity=True,guards_preserved=True,guard_bytes_per_side=32,
                            missing_bind_rejected=True,null_misaligned_short_zero_rejected=True,
                            invalid_rebind_preserves_storage=True),
                        controls_rejected=rejected,capture_sha256=hashlib.sha256(''.join(results['gcc']).encode()).hexdigest(),
                        native='UNRUN',hardware='UNRUN')
            (out/'handoff-qualification.json').write_text(json.dumps(report,indent=2)+'\n')
            print(json.dumps(report,sort_keys=True))


if __name__=='__main__':unittest.main()
