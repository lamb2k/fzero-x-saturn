"""rerun.py (C-132/C-133): drivers for the targeted census under ymir-cov6. Paths come from $FZX_EVIDENCE (default ~/fzx-evidence),
the BIOS from <evidence>/host/tools/ymir-gate/local.json (outside git), the tracer from $FZX_CENSUS_HARNESS (default
<evidence>/host/claude-mem4/harness/ymir-cov6.exe). Every Ymir launch waits for a slot: $FZX_GATE_SLOT_CMD, a shell command that
exits 0 when a run may start (the same variable tools/engine_gate.py reads).

  python rerun.py replay LABEL --gate GATE_EV --hot HOT_EV
      the e65730a replay: loop, loop.2, loop-snd0, reset, reset-snd0, arm-smpc2 (GATE_EV), census-hot and census-hot-reset (HOT_EV),
      from the exact gate images, discs, maps and scripts, one Ymir at a time. Writes <evidence>/host/claude-gate/LABEL/receipt.json.
  python rerun.py hot LABEL --gate GATE_EV --hot HOT_EV
      only the two hot controls: census-hot (the course script of HOT_EV) and census-hot-reset (the hot image under GATE_EV's
      x-reset.txt, the mixed-LEAVE control), on whatever images HOT_EV and GATE_EV hold.
  python rerun.py pack LABEL --gate GATE_EV --runs loop,loop.2,...
      a census evidence folder for tools/census/lowpcs.py from the runs of an ordinary gate run made with the cov6 harness
      (hard links to the run folders; source_ev = GATE_EV; no gate6 replay).
"""
import json, os, shutil, subprocess, sys, time
from pathlib import Path

EVR = Path(os.environ.get('FZX_EVIDENCE') or Path.home() / 'fzx-evidence') / 'host'
HARNESS = Path(os.environ.get('FZX_CENSUS_HARNESS') or EVR / 'claude-mem4' / 'harness' / 'ymir-cov6.exe')


def bios():
    return json.loads((EVR / 'tools' / 'ymir-gate' / 'local.json').read_text())['bios']


def arg(argv, name):
    return argv[argv.index(name) + 1]


def run_jobs(ev, jobs):
    (ev / 'runs').mkdir(parents=True, exist_ok=True)
    rpath = ev / 'receipt.json'
    receipt = json.loads(rpath.read_text()) if rpath.exists() else dict(gate='census', version=1, label=ev.name, harness=str(HARNESS), runs={})
    slot = os.environ.get('FZX_GATE_SLOT_CMD')
    for name, r, src, script, cap, g6run in jobs:
        for p in (script, Path(r['cue']), Path(r['build']) / 'fzs-engine.sym', Path(r['build']) / 'fzs-engine.map'):
            if not p.exists():
                raise SystemExit('missing %s' % p)
        out = ev / 'runs' / name
        if out.exists():
            shutil.rmtree(out)
        while slot and subprocess.run(slot, shell=True, capture_output=True).returncode != 0:
            time.sleep(60)
        env = dict(os.environ, GATE_AUDIO='1' if 'mixed' in r['cue'] else '0')
        t0 = time.time()
        p = subprocess.run([str(HARNESS), bios(), r['cue'], str(Path(r['build']) / 'fzs-engine.sym'), str(out), str(cap), 'script', str(script)],
                           capture_output=True, text=True, env=env)
        (ev / 'runs' / (name + '.log')).write_text('\n'.join(l for l in (p.stdout + p.stderr).splitlines() if not l.startswith(('debug', 'info'))))
        receipt['runs'][name] = dict(rc=p.returncode, seconds=round(time.time() - t0, 1), cue=r['cue'], image=r['image'], kind=r['kind'],
                                     build=r['build'], control_build=r.get('control_build', ''), source_ev=str(src), script=str(script),
                                     gate6_run=str(g6run) if g6run else '')
        rpath.write_text(json.dumps(receipt, indent=1) + '\n')
        print('run %-18s rc %d %5.0f s' % (name, p.returncode, time.time() - t0), flush=True)
    print('RERUN-DONE', flush=True)


def hot_jobs(gate, hot):
    rh = json.loads((hot / 'receipt.json').read_text())['runs']['census-hot']
    return [('census-hot', rh, hot, hot / 'scripts' / '0-course.txt', 30000, hot / 'runs' / 'census-hot'),
            ('census-hot-reset', dict(rh, kind='reset'), hot, gate / 'scripts' / 'x-reset.txt', 40000, None)]


def main(argv):
    cmd, label = argv[0], argv[1]
    ev = EVR / 'claude-gate' / label
    if cmd == 'replay':
        gate, hot = Path(arg(argv, '--gate')), Path(arg(argv, '--hot'))
        g = json.loads((gate / 'receipt.json').read_text())
        jobs = []
        for n in ('loop', 'loop.2', 'loop-snd0', 'reset', 'reset-snd0', 'arm-smpc2'):
            r = g['runs'][n]
            script = gate / 'scripts' / ('x-reset.txt' if r['kind'] == 'reset' else 'x-r104040-loop.txt')
            jobs.append((n, r, gate, script, 40000, gate / 'runs' / n))
        run_jobs(ev, jobs + hot_jobs(gate, hot))
    elif cmd == 'hot':
        run_jobs(ev, hot_jobs(Path(arg(argv, '--gate')), Path(arg(argv, '--hot'))))
    elif cmd == 'pack':
        gate, names = Path(arg(argv, '--gate')), arg(argv, '--runs').split(',')
        g = json.loads((gate / 'receipt.json').read_text())
        (ev / 'runs').mkdir(parents=True, exist_ok=True)
        runs = {}
        for n in names:
            r = g['runs'][n]
            if (ev / 'runs' / n).exists():
                shutil.rmtree(ev / 'runs' / n)
            subprocess.run(['cp', '-al', str(gate / 'runs' / n), str(ev / 'runs' / n)], check=True)
            runs[n] = dict(rc=r['rc'], cue=r['cue'], image=r['image'], kind=r['kind'], build=r['build'], control_build=r.get('control_build', ''),
                           source_ev=str(gate), gate6_run='')
        (ev / 'receipt.json').write_text(json.dumps(dict(gate='census', version=1, label=label, source=str(gate), runs=runs), indent=1) + '\n')
        print('packed', ','.join(names))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
