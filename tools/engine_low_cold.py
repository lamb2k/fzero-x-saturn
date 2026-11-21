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

Carrying a list to another image (memory lane part 4: the list made on the combat image, used on engine):
    python3 tools/engine_low_cold.py --map BUILD/fzs-engine.map --filter OLD_LIST.ld [--ref-map OLD/fzs-engine.map]
        [--keep-file NAMES.txt] [--out saturn/engine/engine_low_text.ld] [--report REPORT.tsv]
keeps each section of OLD_LIST that this image has and skips the rest, never silently: each skipped section is
printed and written into the new list as a comment with its reason. A listed section is found here by its exact
name, or (with --ref-map, the map the old list was derived on) by its base name without .lto_priv.N when that base
names exactly one section in each map: LTO numbers static functions per build, so title_tick.lto_priv.0 there can
be title_tick here. A carried section whose name or size changed is noted in the list. Skipped: a base this map
does not have (ld would ignore the name); a base with more than one section in either map (the name may mean
another function); a kept name (main, engine_low_init, --keep, --keep-file). Every function of this image that the
old list does not name stays in high work RAM (hot) until a census on this image says otherwise. MAP is this
image's, built with OLD_LIST or with an empty list.
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


def text_sections(map_path, anywhere=False):
    """(name, address, size, object) of every .text.* input section linked into high work RAM (ANYWHERE: and those
    linked into low work RAM's .low)."""
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
    return [s for s in out if (anywhere or 0x06000000 <= s[1] < 0x06100000) and s[2] > 0]


def listed(path):
    """Section names of a generated list (lines '*(.text.NAME)'), in order, without repeats."""
    names = []
    for line in open(path):
        m = re.match(r'^\*\((\.text\.\S+)\)\s*$', line)
        if m and m.group(1) not in names:
            names.append(m.group(1))
    return names


def filter_list(a):
    """--filter: the sections of an earlier list that this image has (by name, or by a base name unique in both
    maps); the rest skipped, and both reported."""
    base = lambda name: LTO.sub('', name[len('.text.'):])

    def by_base(path):
        out = collections.defaultdict(list)
        for name, _, n, _ in text_sections(path, anywhere=True):
            out[base(name)].append((name, n))
        return out
    here = by_base(a.map)
    there = by_base(a.ref_map) if a.ref_map else None
    keep = ALWAYS_KEEP | set(filter(None, a.keep.split(',')))
    if a.keep_file:
        keep |= {LTO.sub('', l.split('#')[0].strip()) for l in open(a.keep_file)} - {''}
    kept, skipped = [], []
    for name in listed(a.filter):
        b = base(name)
        h = here.get(b, [])
        note = ''
        if b in keep:
            why = 'kept'
        elif not h:
            why = 'not on this image'
        elif there is None:
            # No reference map: the exact name only, and an LTO name only when it is the base's one section here.
            same = [s for s in h if s[0] == name]
            why = '' if same and (len(h) == 1 or not LTO.search(name)) else 'base %s has %d sections here' % (b, len(h))
            if not why:
                kept.append((name, sum(n for _, n in same), note))
                continue
        else:
            t = there.get(b, [])
            if not t:
                why = 'not in the reference map'
            elif len(h) != len(t):
                why = 'base %s has %d sections here, %d in the reference map' % (b, len(h), len(t))
            elif len(h) == 1:
                (hn, hs), (tn, ts) = h[0], t[0]
                if hn != name:
                    note += ' renamed from %s' % name
                if hs != ts:
                    note += ' size %d (was %d)' % (hs, ts)
                kept.append((hn, hs, note.strip()))
                continue
            elif sorted(n for s, n in h if s == name) == sorted(n for s, n in t if s == name) and any(s == name for s, _ in h):
                # One name on several sections (static functions of one name in several files): equal in both maps.
                kept.append((name, sum(n for s, n in h if s == name), '%d sections' % sum(s == name for s, _ in h)))
                continue
            else:
                why = 'base %s has %d sections that differ from the reference map' % (b, len(h))
        skipped.append((name, why))
    nl = chr(10)
    with open(a.out, 'w', newline=nl) as o:
        o.write('/* Cold functions in low work RAM (ENG_LOW_COLD, engine_low.x): %d sections, %d bytes.' % (
            len(kept), sum(n for _, n, _ in kept)) + nl)
        o.write(' * Generated by tools/engine_low_cold.py --filter from %s (%d names)%s;'
                % (os.path.basename(a.filter), len(kept) + len(skipped),
                   ' and its map ' + os.path.basename(os.path.dirname(a.ref_map)) if a.ref_map else '') + nl)
        o.write(' * regenerate it, do not edit it. Carried with a change (name or size; the census on this image' + nl)
        o.write(' * checks them), %d:' % sum(1 for k in kept if k[2]) + nl)
        for name, _, note in kept:
            if note:
                o.write(' *   %s: %s' % (name, note) + nl)
        o.write(' * Skipped (left in high work RAM), %d:' % len(skipped) + nl)
        for name, why in skipped:
            o.write(' *   %s: %s' % (name, why) + nl)
        o.write(' */' + nl)
        for name, _, _ in kept:
            o.write('*(%s)' % name + nl)
    if a.report:
        with open(a.report, 'w', newline=nl) as o:
            o.write('class\tsize\tsection\tnote' + nl)
            for name, n, note in kept:
                o.write('cold\t%d\t%s\t%s' % (n, name, note) + nl)
            for name, why in skipped:
                o.write('skipped\t-\t%s\t%s' % (name, why) + nl)
    print('carried %d sections, %d B (%d with a change); skipped %d' % (
        len(kept), sum(n for _, n, _ in kept), sum(1 for k in kept if k[2]), len(skipped)))
    for name, _, note in kept:
        if note:
            print('  carried %s: %s' % (name, note))
    for name, why in skipped:
        print('  skipped %s: %s' % (name, why))


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
    ap.add_argument('--census', nargs='+')
    ap.add_argument('--filter', default='', help='an earlier list to carry to this image (see above)')
    ap.add_argument('--ref-map', default='', help='with --filter: the map the earlier list was derived on')
    ap.add_argument('--keep', default='')
    ap.add_argument('--keep-file', default='')
    ap.add_argument('--hot-phases', default=','.join(sorted(HOT_PHASES)))
    ap.add_argument('--out', default='saturn/engine/engine_low_text.ld')
    ap.add_argument('--report', default='')
    a = ap.parse_args()
    if a.filter:
        return filter_list(a)
    if not a.census:
        ap.error('--census or --filter is required')
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
