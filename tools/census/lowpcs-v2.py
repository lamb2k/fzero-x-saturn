"""lowpcs.py GATE_EVIDENCE [--allow-214 FILE]: the low work RAM census of a gate run under ymir-cov5 (claude-mem4, C-124).

Exit 0 only when every check below passes; any missing artifact, unknown image or unmapped run fails (exit 1).
- Mapping: every run in receipt.json carries its image, kind and build directory (engine_gate.py writes them; control
  builds point at ctl-builds/<control>). A run without them, a run folder not in the receipt, a build not in the
  receipt's builds or ctl-builds, or an unreadable map or symbol file fails. Nothing is inferred from run names.
- Low text: from the run's own map, the .low output section (VMA 0x00240000) and the .text.* input sections inside it.
  A map with no .low is reported (no low code linked: the standalone race images); a .low with no text fails.
- Census files: every run has cov-totals.txt and cov-exit-fields.txt (ymir-cov5) and cov-m-100 / cov-m-101; race runs
  cov-m-201; loop, reset and course runs cov-m-204 (not controls, which may stop early).
- Totals: per CPU the phase files sum to their cov-totals line, the phase lines sum to "all", and phase 214 equals
  the moved counts of cov-exit-fields.txt.
- Moved fields (phase 214): each row is RACE 4 -> TITLE 1 with the reset chord (pad 0f00) held, matches the trace
  (fields.txt: scene 4 in the previous field, 1 in that field), and loop and reset runs (not controls) have at least
  one. Every .low function counted in 214 is on the reviewed title/reset list (--allow-214).
- Strict: no .low PC in phase 100 (before main) or 204 (a running race), either CPU.
- Standalone race phases 200/201 are reported in their own table; a .low PC there fails as well.
"""
import bisect, json, re, sys
from pathlib import Path

ev = Path(sys.argv[1])
allow_file = Path(sys.argv[sys.argv.index('--allow-214') + 1]) if '--allow-214' in sys.argv else Path(__file__).with_name('lowpcs-214-allow.txt')
problems, rows, race_rows = [], [], []


def fail(msg):
    problems.append(msg)


def low_text(mp):
    """(lo, hi) of the .text.* sections inside .low, or None when the map links no .low."""
    t = mp.read_text(errors='replace')
    m = re.search(r'^\.low\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)', t, re.M)
    if not m:
        return None
    base, size = int(m.group(1), 16), int(m.group(2), 16)
    hi = base
    for x in re.finditer(r'^ \.text\.\S+\s*\n?\s+0x([0-9a-f]{8,16})\s+0x([0-9a-f]+)', t, re.M):
        a, s = int(x.group(1), 16), int(x.group(2), 16)
        if base <= a < base + size:
            hi = max(hi, a + s)
    return base, hi


def cov(rd, cpu, ph):
    f = rd / ('cov-%s-%s.txt' % (cpu, ph))
    if not f.exists():
        return None
    out = []
    for line in f.read_text().split('\n'):
        if line.strip():
            a, n = line.split()
            out.append((int(a, 16), int(n)))
    return out


try:
    rec = json.loads((ev / 'receipt.json').read_text())
except (OSError, ValueError) as e:
    print('FAIL receipt.json unreadable: %s' % e)
    sys.exit(1)
runs = rec.get('runs') or {}
if not isinstance(runs, dict):
    runs = {}
known_builds = set(rec.get('builds') or {})
allow = set(l.split('#')[0].strip() for l in allow_file.read_text().splitlines() if l.split('#')[0].strip()) if allow_file.exists() else None
if allow is None:
    fail('no reviewed 214 list %s' % allow_file)
if not runs:
    fail('receipt has no runs')
on_disk = {p.name for p in (ev / 'runs').iterdir() if p.is_dir()} if (ev / 'runs').is_dir() else set()
for extra in sorted(on_disk - set(runs)):
    fail('run folder %s is not in the receipt' % extra)

for name in sorted(runs):
    r, rd = runs[name], ev / 'runs' / name
    img, kind, build, ctl = r.get('image'), r.get('kind'), r.get('build'), r.get('control_build')
    if not (img and kind and build):
        fail('%s: no explicit image/kind/build in the receipt' % name)
        continue
    if not rd.is_dir():
        fail('%s: run folder missing' % name)
        continue
    bd = Path(build)
    if ctl:
        if bd.resolve() != (ev / 'ctl-builds' / ctl).resolve():
            fail('%s: control build %s is not ctl-builds/%s' % (name, bd, ctl))
            continue
    elif img not in known_builds or bd.resolve() != (ev / 'builds' / img).resolve():
        fail('%s: unknown image %s (build %s)' % (name, img, bd))
        continue
    mp, sp = bd / 'fzs-engine.map', bd / 'fzs-engine.sym'
    if not (mp.exists() and sp.exists()):
        fail('%s: map or symbols missing in %s' % (name, bd))
        continue
    is_ctl = name.startswith(('ctl-', 'arm-', 'census-'))
    for f in ['cov-totals.txt', 'cov-exit-fields.txt', 'cov-m-100.txt', 'cov-m-101.txt'] + \
             (['cov-m-201.txt'] if kind == 'race' else []) + \
             (['cov-m-204.txt'] if kind in ('loop', 'reset', 'course') and not is_ctl else []):
        if not (rd / f).exists():
            fail('%s: %s missing' % (name, f))
    if not (rd / 'cov-totals.txt').exists():
        continue
    # totals: phase files == their totals line; phases sum to all; 214 == moved counts
    tot = {}
    for line in (rd / 'cov-totals.txt').read_text().splitlines():
        p = line.split()
        if len(p) == 3 and p[1] == 'all':
            tot.setdefault(p[0], {})['all'] = int(p[2])
        elif len(p) == 4 and p[1] == 'phase':
            tot.setdefault(p[0], {})[p[2]] = int(p[3])
    exits = [l.split() for l in (rd / 'cov-exit-fields.txt').read_text().splitlines() if l.strip()] if (rd / 'cov-exit-fields.txt').exists() else []
    for cpu in ('m', 's'):
        t = tot.get(cpu)
        if not t or 'all' not in t:
            fail('%s: cov-totals has no %s totals' % (name, cpu))
            continue
        phases = {k: v for k, v in t.items() if k != 'all'}
        if sum(phases.values()) != t['all']:
            fail('%s: %s phases sum %d != all %d' % (name, cpu, sum(phases.values()), t['all']))
        for ph, n in phases.items():
            c = cov(rd, cpu, ph)
            got = sum(k for _, k in c) if c is not None else (0 if n == 0 else None)
            if got != n:
                fail('%s: cov-%s-%s sums %s, totals say %d' % (name, cpu, ph, got, n))
        moved = sum(int(e[4 if cpu == 'm' else 5]) for e in exits if len(e) == 6)
        if phases.get('214', 0) != moved:
            fail('%s: %s phase 214 total %d != moved %d' % (name, cpu, phases.get('214', 0), moved))
    # moved fields against the trace and the reset input
    trace = {}
    if (rd / 'fields.txt').exists():
        for line in (rd / 'fields.txt').read_text().splitlines():
            p = line.split()
            if len(p) >= 2:
                trace[int(p[0])] = int(p[1])
    for e in exits:
        if len(e) != 6:
            fail('%s: exit row "%s" is not frame/before/after/pad/m/s' % (name, ' '.join(e)))
            continue
        fr, before, after, pad = int(e[0]), int(e[1]), int(e[2]), int(e[3], 16)
        if (before, after) != (4, 1) or pad != 0x0F00:
            fail('%s: moved field %d is %d->%d with pad %04x (want 4->1 under the reset chord 0f00)' % (name, fr, before, after, pad))
        if trace.get(fr) != 1 or trace.get(fr - 1) != 4:
            fail('%s: moved field %d: trace scenes %s -> %s (want 4 -> 1)' % (name, fr, trace.get(fr - 1), trace.get(fr)))
    if kind in ('loop', 'reset') and not is_ctl and not exits:
        fail('%s: a %s run with no soft-reset exit field' % (name, kind))
    lt = low_text(mp)
    if lt is None:
        rows.append('%-26s %-22s no .low linked' % (name, ctl and 'ctl-builds/' + ctl or img))
        continue
    lo, hi = lt
    if hi <= lo:
        fail('%s: .low has no text in %s' % (name, mp))
        continue
    syms = sorted((int(l.split()[0], 16), l.split()[-1]) for l in sp.read_text().splitlines()
                  if len(l.split()) >= 3 and l.split()[1] in 'tTwW')
    keys = [a for a, _ in syms]

    def by_fn(phs):
        out = {}
        for cpu in ('m', 's'):
            for ph in phs:
                for a, n in cov(rd, cpu, ph) or []:
                    if lo <= a < hi:
                        k = (cpu + '/' + ph, syms[bisect.bisect_right(keys, a) - 1][1])
                        out[k] = out.get(k, 0) + 1
        return out

    def fmt(h):
        return ', '.join('%s %s %d' % (c, f, k) for (c, f), k in sorted(h.items()))

    strict, race, info = by_fn(('100', '204')), by_fn(('200', '201')), by_fn(('214',))
    if strict:
        fail('%s: .low PCs in 100/204: %s' % (name, fmt(strict)))
    if race:
        fail('%s: .low PCs in standalone race 200/201: %s' % (name, fmt(race)))
    off = sorted({f for (_, f) in info} - (allow or set()))
    if off:
        fail('%s: phase 214 ran .low functions outside the reviewed title/reset list: %s' % (name, ', '.join(off)))
    line = '%-26s %-22s low text %08x-%08x: 100/204 %d PCs' % (name, ctl and 'ctl-builds/' + ctl or img, lo, hi, sum(strict.values()))
    if kind == 'race':
        race_rows.append('%-26s %-22s 200/201 %d PCs%s' % (name, img, sum(race.values()), (': ' + fmt(race)) if race else ''))
    if info:
        line += '; INFO 214 (fields %s): %s' % (','.join(e[0] for e in exits), fmt(info))
    rows.append(line)

print('\n'.join(rows))
print('\nstandalone race phases 200/201 (.low images only):')
print('\n'.join(race_rows) if race_rows else '  (no race run used an image with .low)')
print('\n' + ('\n'.join('FAIL ' + p for p in problems) if problems else 'census PASS: %d runs mapped and reconciled' % len(runs)))
sys.exit(1 if problems else 0)
