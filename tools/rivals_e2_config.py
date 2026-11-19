#!/usr/bin/env python3
"""Field config for the native rival field from a reference GP report (HOST, E2 checks).
Usage: rivals_e2_config.py REFERENCE.json > FIELD.txt
One line per vehicle: id grid target_lap_ms chase max_energy aggression.
Aggression follows the source's STANDARD rule: only racers chasing another CPU racer attack
(15% per decision when alongside); everyone else is 0."""
import json, sys
d = json.load(open(sys.argv[1]))
for r in d['racers']:
    chase = r['chase']
    aggression = 38 if chase not in (0, 255) else 0
    print(r['id'], r['grid'], r['target_lap_ms'], chase, r['max_energy'], aggression)
