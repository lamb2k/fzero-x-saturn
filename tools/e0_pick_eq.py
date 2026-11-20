#!/usr/bin/env python3
"""E0 HOST = SH-2 equality, pick slice (ENGINE_PLAN section 5, gate 3).

Private stage only: the camera path and the dumps are ROM-derived. This tool
prints offsets, counts and booleans, never their contents.

  build    compile tools/e0_pick_host.c (saturn/engine/engine_pick_body.h, the
           code engine_main.c includes) with the engine image's -D set
  compare  run the HOST pick and stable cap and compare the plan and every
           piece of pick state byte for byte with the E0 image's work RAM:
             replay  from boot state over every slave job the image ran (the
                     ticks from its frame rows, then the held tick), against
                     the first dump;
             step    from the first dump's state, one job at the held tick,
                     against the second dump (one slave job later).

The dumps are claude-eq's headless Ymir hold harness output for the E0 image
(FZX_E1_DRIVE=0): <dir>/holds.* with the slave idle after the held fields and
<dir>/holds2.* one slave job later.
"""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from e0_road_eq import engine_defines, first_diff, symbols  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
CHUNKS, CAP_LIST, GROUPS = 512, 128, 64
# The pick state in e0_pick_host.c's OUTPUT order: (symbol, bytes). The SH-2 holds each big-endian
# in this layout, so the dumped bytes compare directly.
FIELDS = (('fzx_source_road_plan', 796), ('cap_last', 4 * CHUNKS), ('cap_frame', 4), ('cap_kept', CHUNKS),
          ('cap_wait', CHUNKS), ('cap_list', 2 * CAP_LIST), ('cap_depth', 4 * CAP_LIST), ('pick_runs', 12 * GROUPS),
          ('pick_depth', 4 * (CHUNKS + 1)), ('pick_state', CHUNKS + 1), ('fzx_e0_pick', 12),
          ('fzx_e0_cap_dropped', 4))
SOURCES = ('saturn/engine/engine_scene.c', 'saturn/engine/engine_path.c', 'src/fzx_view.c')
SH2ZAM_UNITS = ('sh2zam_segmesh.c',)


def cmd_build(a):
    defines = engine_defines(Path(a.cflags).read_text())
    lib = Path(a.sh2zam)
    args = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *defines,
            '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'ref'), '-I' + str(ROOT / 'saturn/engine'),
            '-I' + str(lib / 'saturn/include'), '-I' + str(Path(a.identity)), str(ROOT / 'tools/e0_pick_host.c'),
            *[str(ROOT / s) for s in SOURCES], *[str(lib / 'saturn/src' / u) for u in SH2ZAM_UNITS],
            '-lm', '-o', a.out]
    r = subprocess.run(args, capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode:
        sys.exit(r.returncode)
    print('built', a.out, 'defines', len(defines))


class Dump:
    def __init__(self, prefix, sym):
        p = Path(prefix)
        self.high = p.with_name(p.name + '.wram-high').read_bytes()
        self.low = p.with_name(p.name + '.wram-low').read_bytes()
        self.sym = sym

    def at(self, addr, size):
        a = addr & 0x0FFFFFFF
        if 0x06000000 <= a and a + size <= 0x06000000 + len(self.high):
            return self.high[a - 0x06000000:a - 0x06000000 + size]
        if 0x00200000 <= a and a + size <= 0x00200000 + len(self.low):
            return self.low[a - 0x00200000:a - 0x00200000 + size]
        sys.exit('address %#x outside the dumped memory' % addr)

    def word(self, name, index=0):
        return struct.unpack('>I', self.at(self.sym['_' + name] + 4 * index, 4))[0]

    def state(self):
        return [self.at(self.sym['_' + n], size) for n, size in FIELDS]


def run_host(a, tag, jobs, start_state):
    work = Path(a.work)
    work.mkdir(parents=True, exist_ok=True)
    inp = work / ('pick-%s-%d.in' % (tag, a.tick))
    inp.write_bytes(struct.pack('>4sIII', b'E0PI', 1, 1 if start_state else 0, len(jobs)) +
                    struct.pack('>%dI' % len(jobs), *jobs) + (b''.join(start_state) if start_state else b''))
    out_path = work / ('pick-%s-%d.out' % (tag, a.tick))
    r = subprocess.run([a.exe, a.path, str(inp), str(out_path)] + (['--flip-cam'] if a.flip_cam else []),
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit('HOST pick failed: ' + r.stderr.strip())
    out = out_path.read_bytes()
    o, parts = 16, []
    for _, size in FIELDS:
        parts.append(out[o:o + size])
        o += size
    if o != len(out):
        sys.exit('HOST output size %d, expected %d' % (len(out), o))
    return parts


def cmd_compare(a):
    sym = symbols(a.sym)
    d1, d2 = Dump(a.dump, sym), Dump(a.dump + '2', sym)
    seq1, done1 = struct.unpack('>II', d1.at(sym['_job'], 8))
    seq2, done2 = struct.unpack('>II', d2.at(sym['_job'], 8))
    calls1, calls2 = d1.word('fzx_e0_pick', 5), d2.word('fzx_e0_pick', 5)
    checks = {
        'hold_tick': d1.word('fzx_input_count') == a.tick and d1.word('fzx_e0_hold_tick') == a.tick,
        'slave_idle_both': seq1 == done1 and seq2 == done2,
        'one_job_between': seq2 == seq1 + 1 and calls2 == calls1 + 1,
        'job_tick': d1.word('job_tick') == a.tick and d2.word('job_tick') == a.tick,
    }
    if a.mode == 'replay':
        # Every pick call since boot: the frame rows' ticks, then the held tick for any job not yet in a row.
        rows, base = d1.word('fzx_e0_report', 1), d1.word('fzx_e0_report', 3)
        ticks = [struct.unpack('>I', d1.at(base + i * a.row_words * 4, 4))[0] for i in range(rows)]
        extra = calls1 - rows
        checks['rows_cover_jobs'] = 0 <= extra <= 2 and (not ticks or ticks[-1] == a.tick)
        jobs = ticks + [a.tick] * max(extra, 0)
        host, sh2 = run_host(a, 'replay', jobs, None), d1.state()
    else:
        jobs = [a.tick]
        host, sh2 = run_host(a, 'step', jobs, d1.state()), d2.state()
    sections = {n: first_diff(h, s) for (n, _), h, s in zip(FIELDS, host, sh2)}
    equal = all(v is None for v in sections.values())
    plan = sh2[0]
    result = dict(tick=a.tick, mode=a.mode + ('-flip' if a.flip_cam else ''), jobs=len(jobs),
                  first_hold_job=next((i for i, t in enumerate(jobs) if t == a.tick), None),
                  groups=struct.unpack('>I', plan[4:8])[0], visible_chunks=struct.unpack('>I', plan[8:12])[0],
                  cap_frame=struct.unpack('>I', sh2[2])[0], equal=equal,
                  differing=sorted(k for k, v in sections.items() if v is not None),
                  first_diff=sections, checks=checks, ok_checks=all(checks.values()))
    print(json.dumps(result, sort_keys=True))
    if a.flip_cam:
        return 0 if not equal else 1   # the control passes when the comparison fails
    return 0 if equal and result['ok_checks'] else 1


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build')
    b.add_argument('--cflags', required=True, help='file holding the E0 image SH_CFLAGS line')
    b.add_argument('--sh2zam', required=True, help='SH2ZAM checkout at the engine pin')
    b.add_argument('--identity', required=True, help='directory of engine_path_identity.h')
    b.add_argument('--out', required=True)
    c = sub.add_parser('compare')
    c.add_argument('mode', choices=('replay', 'step'))
    c.add_argument('--sym', required=True)
    c.add_argument('--dump', required=True, help='dump prefix, e.g. RUN/holds (RUN/holds2 is read too)')
    c.add_argument('--tick', type=int, required=True)
    c.add_argument('--exe', required=True)
    c.add_argument('--path', required=True, help='ENGPATH.PAK')
    c.add_argument('--work', required=True)
    c.add_argument('--row-words', type=int, default=9, help='frame row words (9 in the E0 image)')
    c.add_argument('--flip-cam', action='store_true')
    a = p.parse_args()
    sys.exit(cmd_build(a) if a.cmd == 'build' else cmd_compare(a))


if __name__ == '__main__':
    main()
