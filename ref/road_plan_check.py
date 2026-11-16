#!/usr/bin/env python3
"""Native course planner vs original Course_Draw, synthetic courses only."""
from pathlib import Path
import concurrent.futures,hashlib,os,subprocess,sys
from road_check import fixtures
ROOT=Path(__file__).resolve().parents[1]
FLAGS=['-m32','-msse2','-mfpmath=sse','-fno-fast-math','-fno-unsafe-math-optimizations',
       '-ffp-contract=off','-fno-strict-aliasing','-fno-pic','-fno-builtin','-g','-std=gnu11','-w']

def main():
    out=ROOT/'ref/build/road-plan-check';out.mkdir(parents=True,exist_ok=True)
    subprocess.run([sys.executable,str(ROOT/'tools/camera_assets.py'),'--art','fixture'],check=True)
    courses=fixtures()
    for name,data in courses.items():(out/(name+'.bin')).write_bytes(data)
    def exercise(exe,label):
        results={}
        for name in courses:
            trace=out/(name+'-'+label+'.txt')
            args=[str(exe),'--course',str(out/(name+'.bin')),'--byte-order','big',
                  '--inputs',str(ROOT/'ref/inputs/T10_random.txt'),'--ticks','2000']
            env=dict(os.environ,REF_ROAD_PLAN_TRACE=str(trace))
            first=subprocess.run(args,env=env,capture_output=True)
            if first.returncode:return None
            data=trace.read_bytes()
            repeated=subprocess.run(args,env=env,capture_output=True)
            if repeated.returncode or data!=trace.read_bytes():raise RuntimeError('non-repeatable planner')
            results[name]=(data,first.stdout)
        return results
    def build(arm):
        cc,opt=arm;label=cc+opt
        with (out/(label+'.log')).open('w') as log:
            subprocess.run(['bash',str(ROOT/'ref/build.sh'),cc,opt],cwd=ROOT,
                env=dict(os.environ,FZX_SOURCE_CAMERA='1',REF_CAMERA_ART='fixture',FZX_ROAD_PLAN='1'),
                stdout=log,stderr=subprocess.STDOUT,check=True)
        folder=ROOT/'ref/build'/(label+'-camera-fixture-plan')
        result=exercise(folder/'fzx-ref',label)
        if result is None:raise RuntimeError('source planner mismatch: '+label)
        return result
    arms=[(cc,opt) for cc in ('gcc','clang') for opt in ('-O0','-O2')]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:results=list(executor.map(build,arms))
    for name in courses:
        if any(r[name]!=results[0][name] for r in results):raise RuntimeError('compiler mismatch: '+name)
        print(name,'ticks',len(results[0][name][0].splitlines()),hashlib.sha256(results[0][name][0]).hexdigest())
    folder=ROOT/'ref/build/gcc-O2-camera-fixture-plan'
    original=(folder/'course.i').read_text()
    start=original.index('void ref_road_plan(RefRoadPlan *out, unsigned int draw_frame, const unsigned char course_fog[3]) {')
    native=original[start:]
    controls=[('ref_road_group(out, furthestChunkGroup, 1);','ref_road_group(out, furthestChunkGroup, -1);'),
              ('255 - ((draw_frame * 8) % 256)','255 - ((draw_frame * 7) % 256)'),
              ('course_fog[0] >> 2','course_fog[0] >> 1'),
              ('out->outer = D_800F89D8;','out->outer = !D_800F89D8;')]
    for i,(old,new) in enumerate(controls):
        if old not in native:raise RuntimeError('mutation not applied')
        mutant=out/('mutant%d.i'%i);obj=mutant.with_suffix('.o');exe=mutant.with_suffix('.exe')
        mutant.write_text(original[:start]+native.replace(old,new))
        subprocess.run(['gcc',*FLAGS,'-O2','-c',str(mutant),'-o',str(obj)],check=True,capture_output=True)
        objs=[str(p) for p in folder.glob('*.o') if p.name!='course.o']
        subprocess.run(['gcc','-m32','-no-pie',*objs,str(obj),'-lm','-o',str(exe)],check=True,capture_output=True)
        if exercise(exe,'mutant%d'%i) is not None:raise RuntimeError('planner mutation escaped: '+str(i))
    print('source road planner: 4 compiler arms, repeats, original command oracle and 4 mutation controls PASS')

if __name__=='__main__':main()
