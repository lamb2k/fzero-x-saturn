#!/usr/bin/env python3
"""Cold function list for the scenes image's low work RAM block (ENG_LOW_COLD, saturn/engine/engine_low.x).

A function is hot when either SH-2 executed any of its instructions during a race or before main in an
executed-PC census; every other function in high work RAM is cold. The census comes from a Ymir harness built with
an SH-2 instruction tracer (one count per executed address, per CPU and per phase), run over the gate's scenes loop
and all six course scripts on the image built with ENG_LOW_COLD=0:
    cov-m-<phase>.txt, cov-s-<phase>.txt: "<hex address> <count>" per executed instruction,
    phase 4 = the RACE scene (_fzx_scn_report[1]), 100 = before main, 101 = main up to the scenes magic, other
    numbers = the other scenes.
The harness is GPL (it links Ymir) and stays outside the repository with the gate's own harness.

Usage:
    python3 tools/engine_low_cold.py --map BUILD/fzs-engine.map --census RUN [RUN ...] \
        [--keep NAME,...] [--keep-file NAMES.txt] [--hot-phases 4,100,201] \
        [--out saturn/engine/engine_low_text.ld] [--report REPORT.tsv]

The map is the build the census ran on (ENG_LOW_COLD=0, or 1 with an empty function list). Output lines name exact input sections
(.text.NAME); a section whose name without its .lto_priv.N suffix also belongs to a hot function is left in high
work RAM, so an LTO renumbering can only move a function back, never move a hot one out. main and
engine_low_init run before the copy and always stay; --keep adds names (without the leading underscore).
--keep-file adds names from a file (one per line, # comments), for example every function whose source is Yaul,
libc or libgcc: interrupts are enabled before main, so their handlers must never be in .low. One way to list them:
    sh2eb-elf-nm -l --defined-only BUILD/fzs-engine.elf | awk -F'\t' '$2 ~ /yaul|libgcc|newlib/ {split($1, a, " ");
        if (a[2] ~ /^[tT]$/) print substr(a[3], 2)}' > KEEP.txt
--hot-phases gives the census phases that count as hot (default 4,100,201: the RACE scene, before main, the race
image's ticking race). With a census that splits the RACE scene (4 = entry, loads and countdown; 204 = from the
race's first tick on) use --hot-phases 100,201,204.
Exit 1 if a run has no race-phase census (a hot phase other than 100).
"""
import argparse
import bisect
import collections
import os
import re
import sys

HOT_PHASES = {'4', '100', '201'}
ALWAYS_KEEP = {'main', 'engine_low_init'}
LTO = re.compile(r'(\.lto_priv\.\d+)+$')


def text_sections(map_path):
    """(name, address, size, object) of every .text.* input section linked into high work RAM."""
    out, pend = [], None
    one = re.compile(r'^ (\.text\.\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
    head = re.compile(r'^ (\.text\.\S+)\s*$')
    tail = re.compile(r'^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$')
    for line in open(map_path, errors='replace'):
        m = one.match(line)
        if m:
            out.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4))); pend = None
            continue
        if pend:
            m = tail.match(line)
            if m:
                out.append((pend, int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
            pend = None
            continue
        m = head.match(line)
        if m:
            pend = m.group(1)
    return [s for s in out if 0x06000000 <= s[1] < 0x06100000 and s[2] > 0]


def census(runs, hot_phases=HOT_PHASES):
    hot = set()
    for rd in runs:
        phases = set()
        for fn in os.listdir(rd):
            m = re.match(r'cov-([ms])-(\d+)\.txt$', fn)
            if not m:
                continue
            phases.add(m.group(2))
            if m.group(2) not in hot_phases:
                continue
            for line in open(os.path.join(rd, fn)):
                hot.add(int(line.split()[0], 16))
        if not phases & (set(hot_phases) - {'100'}):
            sys.exit('%s: no race-phase census' % rd)
    return hot


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--map', required=True)
    ap.add_argument('--census', nargs='+', required=True)
    ap.add_argument('--keep', default='')
    ap.add_argument('--keep-file', default='')
    ap.add_argument('--hot-phases', default=','.join(sorted(HOT_PHASES)))
    ap.add_argument('--out', default='saturn/engine/engine_low_text.ld')
    ap.add_argument('--report', default='')
    a = ap.parse_args()
    secs = sorted(text_sections(a.map), key=lambda s: s[1])
    hot_pcs = sorted(census(a.census, set(filter(None, a.hot_phases.split(',')))))
    keep = ALWAYS_KEEP | set(filter(None, a.keep.split(',')))
    if a.keep_file:
        keep |= {LTO.sub('', l.split('#')[0].strip()) for l in open(a.keep_file)} - {''}

    def executed(lo, n):
        i = bisect.bisect_left(hot_pcs, lo)
        return i < len(hot_pcs) and hot_pcs[i] < lo + n

    base = lambda name: LTO.sub('', name[len('.text.'):])
    hot = {s[0] for s in secs if executed(s[1], s[2])}
    hot_bases = {base(n) for n in hot}
    rows, cold = [], []
    for name, addr, size, obj in secs:
        b = base(name)
        if name in hot:
            why = 'hot'
        elif b in keep:
            why = 'kept'
        elif b in hot_bases:
            why = 'name shared with a hot function'
        else:
            why = 'cold'
            cold.append((name, size))
        rows.append((why, size, addr, name, obj))
    with open(a.out, 'w', newline='\n') as o:
        o.write('/* Cold functions in low work RAM (ENG_LOW_COLD, engine_low.x): %d sections, %d bytes.\n'
                ' * Generated by tools/engine_low_cold.py from an executed-PC census (never run during a race or\n'
                ' * before main); regenerate it, do not edit it. */\n' % (len(cold), sum(s for _, s in cold)))
        for name, _ in sorted(cold):
            o.write('*(%s)\n' % name)
    if a.report:
        with open(a.report, 'w', newline='\n') as o:
            o.write('class\tsize\taddr\tsection\tobject\n')
            for r in rows:
                o.write('%s\t%d\t%08x\t%s\t%s\n' % r)
    tot = collections.Counter()
    for why, size, *_ in rows:
        tot[why] += size
    for k in sorted(tot):
        print('%-34s %8d B' % (k, tot[k]))


if __name__ == '__main__':
    main()
