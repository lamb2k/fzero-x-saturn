#!/usr/bin/env python3
"""Field config for the native rival field from a reference GP report (HOST, E2 checks).
Usage: rivals_e2_config.py REFERENCE.json [--course SLUG] [--speed-k K] [--class-terms] > FIELD.txt
One line per vehicle: id grid target_lap_ms chase max_energy aggression finish_ms (tools/rivals_finish.py, 0 = none).
Aggression follows the source's STANDARD rule: only racers chasing another CPU racer attack
(15% per decision when alongside); everyone else is 0.
--class-terms (line-field-6, claude/rival-class): the aggression is the source's own by character and class, and two
columns follow, each racer's spin and rear attack chances (Q16), from tools/rivals_class.py."""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import rivals_finish  # noqa: E402
d = json.load(open(sys.argv[1]))
slug = sys.argv[sys.argv.index('--course') + 1] if '--course' in sys.argv else 'mute-city-1'
finish = rivals_finish.targets(d, slug)
speed_k = float(sys.argv[sys.argv.index('--speed-k') + 1]) if '--speed-k' in sys.argv else 0.0
caps = rivals_finish.chase_caps(d, slug, speed_k) if speed_k else {i: 0 for i in finish}
terms = None
if '--class-terms' in sys.argv:
    import rivals_class  # noqa: E402
    terms = {row[0]: row[1:] for row in rivals_class.racers(d)}
for r in d['racers']:
    chase = r['chase']
    aggression = 38 if chase not in (0, 255) else 0
    if terms is None:
        print(r['id'], r['grid'], r['target_lap_ms'], chase, r['max_energy'], aggression, finish[r['id']], caps[r['id']])
    else:
        side, spin, rear = terms[r['id']]
        print(r['id'], r['grid'], r['target_lap_ms'], chase, r['max_energy'], side, finish[r['id']], caps[r['id']], spin, rear)
