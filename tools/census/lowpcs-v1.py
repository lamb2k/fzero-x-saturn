"""lowpcs.py GATE_EVIDENCE: for every run of a census gate (ymir-cov3 or ymir-cov4), count executed PCs inside each
image's .low text (0x00240000 up to the end of the last .text.* section linked there, from that image's map, or the
control's own map under ctl-builds/) in the phases that must never run low code: 100 (before main) and 204 (the
running race), per CPU, listed by function. Exit 1 if any.
claude-mem4: with ymir-cov4, phase 214 (the one field in which a soft reset leaves a running race, RACE -> TITLE;
its frame numbers in cov-exit-fields.txt) is listed as INFO with the functions it ran from .low; it does not count."""
import bisect, sys, os, re, json
from pathlib import Path
ev = Path(sys.argv[1]); bad = 0
rec = json.loads((ev / 'receipt.json').read_text())
img_of = {r['name']: r.get('image') for r in rec.get('runs', []) if isinstance(r, dict)} if isinstance(rec.get('runs'), list) else {}


def low_text(mp):
    lo, hi = 0x00240000, 0x00240000
    for m in re.finditer(r'^ \.text\.\S+\s*\n?\s+0x(00 ?2[0-9a-f]{5})\s+0x([0-9a-f]+)', open(mp, errors='replace').read(), re.M):
        a, s = int(m.group(1).replace(' ', ''), 16), int(m.group(2), 16); hi = max(hi, a + s)
    return lo, hi


def by_function(rd, phases, lo, hi, syms, keys):
    out = {}
    for fn in os.listdir(rd):
        m = re.match(r'cov-([ms])-(\d+)\.txt$', fn)
        if not m or m.group(2) not in phases: continue
        for line in open(rd / fn):
            a = int(line.split()[0], 16)
            if lo <= a < hi:
                k = (m.group(1) + '/' + m.group(2), syms[bisect.bisect_right(keys, a) - 1][1])
                out[k] = out.get(k, 0) + 1
    return out


for rd in sorted((ev / 'runs').iterdir()):
    if not rd.is_dir(): continue
    name = rd.name; img = img_of.get(name) or ('scenes-drv-snd0' if 'snd0' in name else 'scenes-drv')
    mp = ev / 'builds' / img / 'fzs-engine.map'
    if (ev / 'ctl-builds' / name / 'fzs-engine.map').exists():   # a control built with its own vars: its own map
        img, mp = 'ctl-builds/' + name, ev / 'ctl-builds' / name / 'fzs-engine.map'
    if not mp.exists(): continue
    lo, hi = low_text(mp)
    syms = sorted((int(l.split()[0], 16), l.split()[-1]) for l in open(mp.with_suffix('.sym'))
                  if len(l.split()) >= 3 and l.split()[1] in 'tTwW')
    keys = [a for a, _ in syms]
    hits = by_function(rd, ('100', '204'), lo, hi, syms, keys)
    n = sum(hits.values()); bad += n
    print('%-28s %-22s low text %08x-%08x: %d PCs in phases 100/204%s' % (
        name, img, lo, hi, n, (' : ' + ', '.join('%s %s %d' % (c, f, k) for (c, f), k in sorted(hits.items()))) if hits else ''))
    info = by_function(rd, ('214',), lo, hi, syms, keys)
    if info:
        ef = rd / 'cov-exit-fields.txt'
        fields = ef.read_text().split() if ef.exists() else []
        print('%-28s INFO phase 214 (soft-reset exit field%s %s): .low PCs by function: %s' % (
            name, 's' if len(fields) != 1 else '', ','.join(fields) or '?',
            ', '.join('%s %s %d' % (c, f, k) for (c, f), k in sorted(info.items()))))
sys.exit(1 if bad else 0)
