#!/usr/bin/env python3
"""HOST gate for source backdrop state; builds must already exist.

Uses a synthetic oval and machine. The oracle runs the original source update
against separate storage. Deliberate native-code mutations are compiled only in
the ignored gate directory, without modifying any receipted build output.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'generated/backdrop_state/check'
CONFIGS = ('gcc-O2', 'clang-O2')


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], text=True, capture_output=True,
                          check=True, **kwargs)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    course = OUT/'fixture.bin'
    run([sys.executable, ROOT/'ref/fixture_course.py', course])
    hashes = {}
    trace_hashes = {}
    for config in CONFIGS:
        build = ROOT/'ref/build'/(config+'-camera-fixture-backdrop')
        baseline = ROOT/'ref/build'/(config+'-camera-fixture')/'fzx-ref'
        current = {}
        for script in sorted((ROOT/'ref/inputs').glob('T*.txt')):
            args = ['--course', course, '--byte-order', 'big', '--inputs', script]
            trace = OUT/(config+'-'+script.stem+'.trace')
            result = run([build/'fzx-ref', *args], env={**os.environ, 'REF_BACKDROP_TRACE': str(trace)})
            off = run([baseline, *args])
            if result.stdout != off.stdout:
                raise ValueError('Backdrop changed simulation: '+config+' '+script.stem)
            current[script.stem] = result.stdout.strip()
            digest = hashlib.sha256(trace.read_bytes()).hexdigest()
            if script.stem in trace_hashes and digest != trace_hashes[script.stem]:
                raise ValueError('Backdrop compiler divergence: '+script.stem)
            trace_hashes[script.stem] = digest
        hashes[config] = current
        if current != hashes[CONFIGS[0]]:
            raise ValueError('Simulation compiler divergence')
    build = ROOT/'ref/build/gcc-O2-camera-fixture-backdrop'
    preprocessed = (build/'background.i').read_text()
    boundary = preprocessed.index('static Background native_backgrounds[1];')
    stock, native = preprocessed[:boundary], preprocessed[boundary:]
    controls = {
        'pause': ('if (!native_paused)', 'if (1)'),
        'double_scroll': ('cloudScroll->xScroll += cloudScroll->xScrollSpeed;',
                          'cloudScroll->xScroll += cloudScroll->xScrollSpeed * 0.5f;'),
        'cloud_height': ('cloudHeightAboveEye == 0.0f', 'cloudHeightAboveEye <= 0.0f'),
        'sky_uv': ('((u16*)((vtx)->v.tc))[0] = ((s) & 0xFFFF);',
                   '((u16*)((vtx)->v.tc))[0] = ((s + 32) & 0xFFFF);'),
    }
    results = {}
    flags = ['-m32', '-msse2', '-mfpmath=sse', '-fno-fast-math', '-fno-unsafe-math-optimizations',
             '-ffp-contract=off', '-fno-strict-aliasing', '-fno-pic', '-fno-builtin', '-O2', '-w']
    objects = [p for p in sorted(build.glob('*.o')) if p.name != 'background.o']
    for name, (old, new) in controls.items():
        if old not in native:
            raise ValueError('Control boundary missing: '+name)
        mutant = OUT/(name+'.i')
        start = native.index('static void native_Background_UpdateCloudVtx(') if name == 'pause' else 0
        mutant.write_text(stock+native[:start]+native[start:].replace(old, new, 1))
        obj = OUT/(name+'.o')
        binary = OUT/(name+'-ref')
        run(['gcc', *flags, '-c', mutant, '-o', obj])
        run(['gcc', '-m32', '-no-pie', *objects, obj, '-lm', '-o', binary])
        command = [str(binary), '--course', str(course), '--byte-order', 'big', '--inputs',
                   str(ROOT/'ref/inputs/T02_full_throttle.txt')]
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode != 3 or 'backdrop' not in result.stderr:
            raise ValueError('Mutation was not rejected: '+name)
        results[name] = result.stderr.strip()
    report = {'tier': 'HOST', 'scripts': hashes, 'packet_sha256': trace_hashes,
              'controls': results, 'hardware': 'UNRUN', 'native_render': 'UNRUN'}
    (OUT/'report.json').write_text(json.dumps(report, sort_keys=True, indent=2)+'\n')
    print('HOST backdrop: 10 scripts x 2 compilers, knob-off simulation identity, 4 mutations PASS')


if __name__ == '__main__':
    main()
