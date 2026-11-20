#!/usr/bin/env python3
"""Class table for the attacks judge (tools/attacks_duel.py): per event class, the windows that hold a
reference event of the class, how many pass unmutated, and how many fail under the class's control.
Usage: attacks_duel_table.py JUDGE_DIR [OUT.json]"""
import json, sys
from pathlib import Path

CLASSES = ['contact', 'side', 'spin', 'damage', 'spin_out', 'retire', 'ko']
CONTROL = {'damage': ['impact1.2', 'share-swap', 'heavy9'], 'side': ['push2.5'], 'spin': ['push2.5'],
           'contact': ['reach45'], 'spin_out': ['heavy5'], 'retire': ['spin140'], 'ko': ['credit-off']}


def by_class(j):
    out = {c: [] for c in CLASSES}
    for sc, v in j['scenarios'].items():
        for w in v['windows']:
            for c, lst in w.get('events', {}).items():
                if any(x['ref'] for x in lst):
                    out[c].append(((sc, w['start']), w['ok']))
    return out


d = Path(sys.argv[1])
base = by_class(json.loads((d / 'judge-base.json').read_text()))
table = {}
for c in CLASSES:
    row = {'windows': len(base[c]), 'passing': sum(ok for _, ok in base[c]), 'controls': {}}
    for m in CONTROL[c]:
        f = d / ('judge-%s.json' % m)
        if not f.exists():
            continue
        mm = dict(by_class(json.loads(f.read_text()))[c])
        row['controls'][m] = sum(1 for k, ok in base[c] if ok and not mm.get(k, True))
    table[c] = row
    print('%-8s windows %3d  pass %3d  control fails %s' % (c, row['windows'], row['passing'],
          ', '.join('%s %d/%d' % (m, n, row['passing']) for m, n in row['controls'].items())))
if len(sys.argv) > 2:
    Path(sys.argv[2]).write_text(json.dumps(table, indent=1))
