#!/usr/bin/env python3
"""Controls of the C-136 bound scores (tiers R and S) on real windows (HOST), frozen before the fresh set was viewed, the C-116
controls re-run under the new score: the judge must be able to fail under it. The score key is c136R_ok or c136S_ok (--key). Reads the judge's result files of one set (written by
tools/attacks_contact_judge.py, c092-<tag>.json, base and one per planted mutation).

For every mutation m with a target class (tools/attacks_duel.py MUTATIONS): some window that passes the revised score
unmutated must fail it under m (opp-player and no-destroy are informational: they are controls of other cards).
coast-bias (a biased damp, one Q16 LSB toward zero a tick, tools/attacks_host/duel_native.c): some eligible-racer window
that passes the revised score unmutated must fail it on the speed check alone (position and events fine), and the floor
must not have let it through.
Usage: attacks_c136_controls.py DIR [--key=c136R_ok|c136S_ok] [mutation ...]   (default: every c092-<mutation>.json in DIR)
Exit 1 if any control is missing or does not fail as it must."""
import json
import sys
from pathlib import Path

TARGET = {'impact1.2': 'damage', 'share-swap': 'damage', 'heavy9': 'damage', 'push2.5': 'side', 'reach45': 'contact',
          'spin140': 'retire', 'credit-off': 'ko', 'heavy5': 'spin_out'}


def windows(res):
    return {(sc, w['start']): w for sc, v in res['scenarios'].items() for w in v['windows']}


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--key=')]
    key = next((x[6:] for x in sys.argv[1:] if x.startswith('--key=')), 'c136S_ok')
    d = Path(args[0])
    base = windows(json.loads((d / 'c092-base.json').read_text()))
    names = args[1:] or sorted(p.name[5:-5] for p in d.glob('c092-*.json') if p.name not in ('c092-base.json',) and '-overlap' not in p.name)
    fails = 0
    for m in names:
        f = d / ('c092-%s.json' % m)
        if not f.exists():
            print('FAIL %s: no result file' % m)
            fails += 1
            continue
        mut = windows(json.loads(f.read_text()))
        dropped = [k for k, w in base.items() if w.get(key) and not mut.get(k, {}).get(key)]
        if m == 'coast-bias':
            ek = 'speed_ok_c136R' if key == 'c136R_ok' else 'speed_ok_c136S'
            speed_only = [k for k in dropped if mut[k].get('pos_ok') and mut[k].get('events_ok')
                          and not all(e.get(ek, True) for e in mut[k].get('endpoints', []))
                          and any(e.get('eligible') for e in base[k].get('endpoints', []))]
            ok = bool(speed_only)
            print('%s coast-bias: %d windows passing the %s score now fail it, %d of them on the speed check alone with an eligible racer'
                  % ('PASS' if ok else 'FAIL', len(dropped), key, len(speed_only)))
        elif m in TARGET:
            ok = bool(dropped)
            print('%s %s: %d windows passing the %s score now fail it (target class %s)' % ('PASS' if ok else 'FAIL', m, len(dropped), key, TARGET[m]))
        else:
            ok = True
            print('INFO %s: %d windows passing the %s score now fail it' % (m, len(dropped), key))
        fails += not ok
    print('controls: %d failing' % fails)
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
