"""lowpcs.py v3 (C-132): the low work RAM census of a targeted rerun under the ymir-cov6 harness (claude-mem4).

  python lowpcs.py CENSUS_EV [--allow FILE] [--mask-phase RUN]

Exit 0 only when every check passes; any missing artifact, unlisted PC, unaccepted reset or ambiguous attribution fails.
CENSUS_EV/receipt.json maps every run to its image, kind, build directory and source evidence (the gate6 evidence the
build came from); a run may name the gate6 run it replays (gate6_run).

Rules (C-132 (a)(b)):
- Phase 100 (before main) stays strict on both CPUs. Standalone race phases 200/201 stay strict (race-mode runs).
- A script run has cov-low6.txt (H hits with the sequencer's ACTUAL state at the instant of the hit), cov-f6.txt (one F row
  per field: state at the field's end, the script pad, the consumed pad, counters) and cov-other6.txt (hits while the
  sequencer's scene is not RACE). Per CPU and PC: sum(H) + O must equal the sum over every cov-<cpu>-<phase>.txt (the
  unsplit totals), and the phase files must sum to the totals.
- A .low hit with scene RACE and the race running (from the field after the first field whose input count has moved past the
  epoch's entry count: the cov3 convention, the first tick's own field is entry work) is STRICT unless every one of
  these holds, otherwise it fails:
    * master CPU, outside fzx_race_frame (racedepth 0: live race work and its callees stay strict through LEAVE);
    * the function is on the transition list (8 symbols);
    * this race epoch has a verified accepted reset (below), and the hit is positively identified as work after the accepted
      request: its state shows the accepted counter above the epoch's start value and next = TITLE, on or before the flip field;
      the sequencer's actual phase at the hit has left RUN (LEAVE or later, next = TITLE): a hit with the phase still RUN is strict
      even after the request was accepted (C-133: the RUN check takes precedence over the reset allowlist).
  Any slave hit while the race runs is strict. A hit that fits no rule fails (ambiguous attribution fails).
- Verified accepted reset for an epoch E (scene RACE): the first F row where the accepted counter (report[9]) leaves its
  epoch-start value must show +1, the chord counter (report[21]) +1, the request pointing at TITLE, the same epoch, and the
  script pad AND the consumed pad (report[31]) holding the chord (0F00) in that field or the one before; every later F row
  stays scene RACE in epoch E until a flip field to scene TITLE in epoch E+1. A chord merely present, an earlier epoch's
  latched chord, a rejected request, a RACE -> RESULTS exit or missing boundary rows cannot qualify.
- Controls: --mask-phase RUN drops the hits taken while the actual phase is RUN (a mixed-LEAVE control: the LEAVE hits must
  still fail).
"""
import bisect, hashlib, json, re, sys
from pathlib import Path

RACE, TITLE = 4, 1
PH_RUN, PH_LEAVE = 1, 2
CHORD = 0x0F00
NONE = 0xFFFFFFFF
PHN = {0: 'ENTER', 1: '204', 2: 'LEAVE', 3: 'RETIRE', 4: 'PRELOAD', 5: 'FAULT'}


def main(argv):
    if not argv or argv[0].startswith('-'):
        print('usage: lowpcs.py CENSUS_EV [--allow FILE] [--mask-phase RUN]')
        return 2
    ev = Path(argv[0])
    allow_file = Path(argv[argv.index('--allow') + 1]) if '--allow' in argv else Path(__file__).with_name('lowpcs-transition-allow.txt')
    mask_run = '--mask-phase' in argv and argv[argv.index('--mask-phase') + 1] == 'RUN'
    problems, rows, race_rows, acct_rows = [], [], [], []

    def fail(m):
        problems.append(m)

    def low_text(mp):
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

    def phases_of(rd, cpu):
        return sorted({p.name.split('-')[2][:-4] for p in rd.glob('cov-%s-*.txt' % cpu)})

    def pc_sums(rd, cpu):
        s = {}
        for ph in phases_of(rd, cpu):
            for a, n in cov(rd, cpu, ph) or []:
                s[a] = s.get(a, 0) + n
        return s

    try:
        rec = json.loads((ev / 'receipt.json').read_text())
    except (OSError, ValueError) as e:
        print('FAIL receipt.json unreadable: %s' % e)
        return 1
    runs = rec.get('runs') or {}
    if not isinstance(runs, dict):
        runs = {}
    allow = set(l.split('#')[0].strip() for l in allow_file.read_text().splitlines() if l.split('#')[0].strip()) if allow_file.exists() else None
    if not allow:
        fail('no reviewed transition list %s' % allow_file)
        allow = set()
    if not runs:
        fail('receipt has no runs')
    on_disk = {p.name for p in (ev / 'runs').iterdir() if p.is_dir()} if (ev / 'runs').is_dir() else set()
    for extra in sorted(on_disk - set(runs)):
        fail('run folder %s is not in the receipt' % extra)
    if mask_run:
        rows.append('MASK: hits taken while the actual phase is RUN are dropped (control mode)')

    for name in sorted(runs):
        r, rd = runs[name], ev / 'runs' / name
        img, kind, build, ctl, src = r.get('image'), r.get('kind'), r.get('build'), r.get('control_build'), r.get('source_ev')
        if not (img and kind and build and src):
            fail('%s: no explicit image/kind/build/source_ev in the receipt' % name)
            continue
        if not rd.is_dir():
            fail('%s: run folder missing' % name)
            continue
        bd, srcp = Path(build), Path(src)
        try:
            src_rec = json.loads((srcp / 'receipt.json').read_text())
        except (OSError, ValueError):
            fail('%s: source evidence %s has no readable receipt' % (name, srcp))
            continue
        if ctl:
            if bd.resolve() != (srcp / 'ctl-builds' / ctl).resolve():
                fail('%s: control build %s is not %s/ctl-builds/%s' % (name, bd, srcp, ctl))
                continue
        else:
            if img not in (src_rec.get('builds') or {}) or bd.resolve() != (srcp / 'builds' / img).resolve():
                fail('%s: unknown image %s (build %s) in %s' % (name, img, bd, srcp))
                continue
            binf = bd / 'fzs-engine.bin'
            want = src_rec['builds'][img].get('bin', '')
            if not binf.exists() or hashlib.sha256(binf.read_bytes()).hexdigest() != want:
                fail('%s: %s does not match the source receipt BIN %s' % (name, binf, want[:16]))
                continue
        mp, sp = bd / 'fzs-engine.map', bd / 'fzs-engine.sym'
        if not (mp.exists() and sp.exists()):
            fail('%s: map or symbols missing in %s' % (name, bd))
            continue
        script = kind != 'race'
        need = ['cov-totals.txt', 'cov-m-100.txt', 'cov-m-101.txt'] + \
               (['cov-m-201.txt'] if kind == 'race' else ['cov-low6.txt', 'cov-f6.txt', 'cov-other6.txt'])
        miss = [f for f in need if not (rd / f).exists()]
        for f in miss:
            fail('%s: %s missing' % (name, f))
        if miss:
            continue
        # totals: phase files == their totals line; phases sum to all
        tot = {}
        for line in (rd / 'cov-totals.txt').read_text().splitlines():
            p = line.split()
            if len(p) == 3 and p[1] == 'all':
                tot.setdefault(p[0], {})['all'] = int(p[2])
            elif len(p) == 4 and p[1] == 'phase':
                tot.setdefault(p[0], {})[p[2]] = int(p[3])
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
        lt = low_text(mp)
        label = ctl and 'ctl-builds/' + ctl or img
        if lt is None:
            rows.append('%-24s %-22s no .low linked' % (name, label))
            continue
        lo, hi = lt
        if hi <= lo:
            fail('%s: .low has no text in %s' % (name, mp))
            continue
        syms = sorted((int(l.split()[0], 16), l.split()[-1]) for l in sp.read_text().splitlines()
                      if len(l.split()) >= 3 and l.split()[1] in 'tTwW')
        keys = [a for a, _ in syms]

        def fn(a):
            return syms[bisect.bisect_right(keys, a) - 1][1]

        def fmt(h):
            return ', '.join('%s %s %d' % (c, f, k) for (c, f), k in sorted(h.items()))

        # phase 100 strict, race-mode phases 200/201 strict
        strict = {}
        for cpu in ('m', 's'):
            for ph in (('100',) if script else ('100', '200', '201')):
                for a, n in cov(rd, cpu, ph) or []:
                    if lo <= a < hi:
                        k = (cpu + '/' + ph, fn(a))
                        strict[k] = strict.get(k, 0) + 1
        if strict:
            fail('%s: .low PCs in strict phases: %s' % (name, fmt(strict)))
        if not script:
            race_rows.append('%-24s %-22s 100/200/201 %d PCs' % (name, img, sum(strict.values())))
            continue

        # replay of the gate6 run (same image, script and harness behaviour): totals must be identical
        g6 = r.get('gate6_run')
        if g6:
            g6 = Path(g6)
            if not (g6 / 'cov-totals.txt').exists():
                fail('%s: gate6 run %s has no cov-totals' % (name, g6))
            else:
                for cpu in ('m', 's'):
                    a, b = pc_sums(rd, cpu), pc_sums(g6, cpu)
                    if a != b:
                        d = [hex(k) for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)][:4]
                        fail('%s: %s PC totals differ from the gate6 run (%d PCs, e.g. %s)' % (name, cpu, len([1 for k in set(a) | set(b) if a.get(k) != b.get(k)]), ' '.join(d)))
        # conservation: per CPU and PC, sum(H) + O == the unsplit totals (low range)
        H, F, E, Q, O = [], [], [], [], {}
        for line in (rd / 'cov-low6.txt').read_text().splitlines():
            p = line.split()
            try:
                if p[0] == 'H' and len(p) == 16:
                    H.append(dict(cpu=p[1], field=int(p[2]), pc=int(p[3], 16), n=int(p[4]), o1=int(p[5]), o2=int(p[6]), cur=int(p[7]),
                                  ph=int(p[8]), nx=int(p[9]), pd=int(p[10]), ep=int(p[11]), a9=int(p[12]), a21=int(p[13]), ic=int(p[14]), rd=int(p[15])))
                elif p[0] == 'E' and len(p) == 6:
                    E.append(dict(field=int(p[2]), ord=int(p[3]), kind=int(p[4])))
                elif p[0] == 'Q' and len(p) == 8:
                    Q.append(dict(field=int(p[2]), ord=int(p[3]), target=int(p[4]), rank=int(p[5]), a9=int(p[6]), pd=int(p[7])))
                else:
                    fail('%s: cov-low6 row "%s" is not understood' % (name, line))
            except ValueError:
                fail('%s: cov-low6 row "%s" is malformed' % (name, line))
        for line in (rd / 'cov-f6.txt').read_text().splitlines():
            p = line.split()
            try:
                if p[0] == 'F' and len(p) == 18:
                    F.append(dict(field=int(p[1]), pad=int(p[2], 16), rscene=int(p[3]), rphase=int(p[4]), cur=int(p[5]), ph=int(p[6]),
                                  nx=int(p[7]), pd=int(p[8]), ep=int(p[9]), a9=int(p[10]), a21=int(p[11]), rpad=int(p[12], 16),
                                  ic=int(p[13]), rd=int(p[14]), mo=int(p[15]), so=int(p[16]), ent=int(p[17])))
                else:
                    fail('%s: cov-f6 row "%s" is not understood' % (name, line))
            except ValueError:
                fail('%s: cov-f6 row "%s" is malformed' % (name, line))
        for line in (rd / 'cov-other6.txt').read_text().splitlines():
            p = line.split()
            if len(p) == 4 and p[0] == 'O':
                O[(p[1], int(p[2], 16))] = int(p[3])
            else:
                fail('%s: cov-other6 row "%s" is not understood' % (name, line))
        for cpu in ('m', 's'):
            sums = pc_sums(rd, cpu)
            low = {a: n for a, n in sums.items() if 0x00200000 <= a < 0x00300000}
            got = {}
            for h in H:
                if h['cpu'] == cpu:
                    got[h['pc']] = got.get(h['pc'], 0) + h['n']
            for (c, a), n in O.items():
                if c == cpu:
                    got[a] = got.get(a, 0) + n
            if got != low:
                bad = [hex(k) for k in sorted(set(got) | set(low)) if got.get(k) != low.get(k)]
                fail('%s: %s low hits do not reconcile to the unsplit totals (%d PCs, e.g. %s)' % (name, cpu, len(bad), ' '.join(bad[:4])))
        if not F:
            fail('%s: no F rows (no script field ran)' % name)
            continue
        Fs = sorted(F, key=lambda x: x['field'])
        byfield = {f['field']: f for f in Fs}
        if len(byfield) != len(Fs):
            fail('%s: duplicate F rows' % name)
            continue
        # race epochs: entry count and the verified accepted reset
        eps = {}
        for f in Fs:
            if f['cur'] == RACE:
                eps.setdefault(f['ep'], []).append(f)
        epinfo = {}
        for e, fl in eps.items():
            first = fl[0]
            # the race "runs" from the field AFTER the first field whose input count has moved past the entry count (the census
            # convention since cov3: the first tick's own field is entry work)
            ff = next((f['field'] for f in fl if f['ic'] >= 1 and f['ic'] != first['ic']), None)
            info = dict(entry_ic=first['ic'], run_from=None if ff is None else ff + 1, a9_0=first['a9'], a21_0=first['a21'], acc=None, why=None)
            acc = next((f for f in fl if f['a9'] != first['a9'] or f['a21'] != first['a21']), None)
            if acc is not None:
                # fields run in order: the F rows of the epoch continue until the flip
                ok = True
                why = ''
                if acc['a9'] != first['a9'] + 1 or acc['a21'] != first['a21'] + 1:
                    ok, why = False, 'counters moved by %d/%d, not 1/1' % (acc['a9'] - first['a9'], acc['a21'] - first['a21'])
                elif not ((acc['pd'] == TITLE and acc['ph'] <= PH_RUN) or (acc['nx'] == TITLE and acc['ph'] >= PH_LEAVE)):
                    ok, why = False, 'request does not point at TITLE (pending %x next %d phase %d)' % (acc['pd'], acc['nx'], acc['ph'])
                elif acc['ep'] != e:
                    ok, why = False, 'epoch changed at acceptance'
                else:
                    pf = byfield.get(acc['field'] - 1)
                    if not any(x is not None and x['pad'] & CHORD == CHORD and x['rpad'] & CHORD == CHORD for x in (acc, pf)):
                        ok, why = False, 'chord not injected and consumed in the acceptance field or the one before'
                flip = None
                if ok:
                    for f in Fs:
                        if f['field'] <= acc['field']:
                            continue
                        if f['cur'] == RACE and f['ep'] == e:
                            continue
                        flip = f
                        break
                    if flip is None:
                        ok, why = False, 'no flip field in the trace'
                    elif not (flip['cur'] == TITLE and flip['ep'] == e + 1):
                        ok, why = False, 'exit goes to scene %d epoch %d, not TITLE epoch %d' % (flip['cur'], flip['ep'], e + 1)
                    else:
                        for f in Fs:
                            if acc['field'] < f['field'] < flip['field'] and not (f['cur'] == RACE and f['ep'] == e):
                                ok, why = False, 'transition broken at field %d' % f['field']
                                break
                    # contiguous fields
                if ok:
                    rel = next((f['field'] for f in Fs if f['field'] >= acc['field'] and f['pad'] & CHORD != CHORD), None)
                    info.update(acc=acc, flip=flip, why='ok', rel=rel)
                else:
                    info.update(acc=None, why=why, bad=acc)
            epinfo[e] = info
        # classify the low hits taken while scene RACE
        Hm = [h for h in H if not (mask_run and h['ph'] == PH_RUN)]
        accounted = {}
        counted_fn = {}

        def bump(cls, f, n):
            accounted[(f, cls)] = accounted.get((f, cls), 0) + n

        fails = {}
        for h in Hm:
            if not (lo <= h['pc'] < hi):
                continue
            f = fn(h['pc'])
            info = epinfo.get(h['ep'])
            lab = '%s/%s' % (h['cpu'], PHN.get(h['ph'], 'P%d' % h['ph']))
            if info is None:
                fails[(lab + '/no-epoch-row', f)] = fails.get((lab + '/no-epoch-row', f), 0) + 1
                bump('STRICT', f, h['n'])
                continue
            running = info['run_from'] is not None and h['field'] >= info['run_from']
            if not running:
                bump('race entry', f, h['n'])
                continue
            ok = False
            if h['cpu'] == 'm' and h['rd'] == 0 and f in allow and info['acc'] is not None:
                acc, flip = info['acc'], info['flip']
                # C-133: the RUN check takes precedence over the reset allowlist. The exemption starts only after execution has
                # left RUN (the sequencer's actual phase LEAVE or later at the instant of the hit), so a post-Q hit in the
                # acceptance field with the phase still RUN stays strict. Ordered attribution is per hit (its own state).
                if h['field'] <= flip['field'] and h['a9'] > info['a9_0'] and h['ph'] >= PH_LEAVE and h['nx'] == TITLE:
                    ok = True
            if ok:
                bump('accepted reset', f, h['n'])
            else:
                fails[(lab, f)] = fails.get((lab, f), 0) + 1
                bump('STRICT', f, h['n'])
        for (a_c, a_pc), n in O.items():
            if lo <= a_pc < hi:
                bump('outside RACE', fn(a_pc), n)
        if fails:
            fail('%s: .low hits while the race runs: %s' % (name, fmt(fails)))
        # the verified resets of this run
        okr = [(e, i) for e, i in sorted(epinfo.items()) if i['acc'] is not None]
        for e, i in sorted(epinfo.items()):
            if i['acc'] is None and i['why'] not in (None,):
                if i['why'] != 'ok':
                    fail('%s: race epoch %d: accepted reset not verified: %s (field %d)' % (name, e, i['why'], (i.get('bad') or {}).get('field', -1)))
        is_ctl = name.startswith(('ctl-', 'arm-', 'census-'))
        if kind in ('loop', 'reset') and not is_ctl and not okr:
            fail('%s: a %s run with no verified accepted reset' % (name, kind))
        resets = '; '.join('epoch %d: accepted field %d (a9 %d->%d chord %d->%d pad %04x consumed %04x), chord released field %s, flip field %d (pad %04x)' % (
            e, i['acc']['field'], i['a9_0'], i['acc']['a9'], i['a21_0'], i['acc']['a21'], i['acc']['pad'], i['acc']['rpad'], i['rel'], i['flip']['field'], i['flip']['pad']) for e, i in okr)
        rows.append('%-24s %-22s low text %08x-%08x: STRICT %d; %d accepted reset(s)%s' % (
            name, label, lo, hi, sum(fails.values()), len(okr), (' [' + resets + ']') if resets else ''))
        # accounting of the transition symbols, master
        for f in sorted(allow):
            tot_m = sum(n for (c, a), n in O.items() if c == 'm' and lo <= a < hi and fn(a) == f) + \
                sum(h['n'] for h in H if h['cpu'] == 'm' and lo <= h['pc'] < hi and fn(h['pc']) == f)
            parts = {cls: accounted.get((f, cls), 0) for cls in ('outside RACE', 'race entry', 'accepted reset', 'STRICT')}
            if not mask_run and sum(parts.values()) != tot_m:
                fail('%s: %s hits %d are not fully attributed (%s)' % (name, f, tot_m, parts))
            acct_rows.append('%-24s %-22s %6d  outside-RACE %d  race-entry %d  accepted-reset %d  STRICT %d' % (
                name, f, tot_m, parts['outside RACE'], parts['race entry'], parts['accepted reset'], parts['STRICT']))
    print('\n'.join(rows))
    print('\nstandalone race (.low images only):')
    print('\n'.join(race_rows) if race_rows else '  (no race-mode run)')
    print('\ntransition symbols per run (master, hits):')
    print('\n'.join(acct_rows) if acct_rows else '  (none)')
    print('\n' + ('\n'.join('FAIL ' + p for p in problems) if problems else 'census PASS: %d runs mapped, reconciled and attributed' % len(runs)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
