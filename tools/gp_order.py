"""gp_order.py RUN_DIR SYM [ROUNDS] [--control missing-round|wrong-points]: Grand Prix cup acceptance from a scenes
harness run (claude-jgp ymir-jgp work-RAM-high dumps named by the script's marks: roundN_start, roundN_results,
roundN_standings). Reads the cup (engine_cup.h fzx_cup_t at _cup) and the field (engine_field.c veh[], 64 B each).

Every check must hold for PASS (C-097):
- every round has its start, results and standings dump (a missing dump is a FAIL, never a skipped round);
- from round 2 the start order is the previous round's finish reversed (cpu.c: position = N - D_80115D50[i] + 1):
  ROUNDS - 1 transitions;
- each round's points are exact per entrant: entrant at place p gains gRacePositionPoints[p] (racer.c) when its row
  scores, else 0; totals are the running sums from 0;
- last_place equals each entrant's finishing place;
- after the last round the cup is FINAL (state 5) and the final standings (points, ties to the better last place,
  the player first on a tie with a rival; engine_scenes.c gp_ahead) are printed.

Controls (each must FAIL): --control missing-round drops the last round's standings dump; --control wrong-points
replaces round 2's award by a monotone but wrong schedule (100, 99, 98, ...).
--player-ms a,b,...: also require the player's official time each round to equal these (HOST) values."""
import struct, sys

POINTS = [100, 93, 87, 81, 76, 71, 66, 62, 58, 54, 50, 47, 44, 41, 38,
          35, 33, 31, 29, 27, 25, 23, 22, 21, 20, 19, 18, 17, 16, 15]   # racer.c gRacePositionPoints
CUP_FINAL = 5

args = [a for a in sys.argv[1:] if not a.startswith('--')]
control = None
if '--control' in sys.argv:
    control = sys.argv[sys.argv.index('--control') + 1]
    args.remove(control)
player_ms = None   # --player-ms a,b,...: the player's official time each round must equal (HOST references)
if '--player-ms' in sys.argv:
    v = sys.argv[sys.argv.index('--player-ms') + 1]; args.remove(v)
    player_ms = [int(x) for x in v.split(',')]
run, symf = args[0], args[1]
R = int(args[2]) if len(args) > 2 else 6
sym = {}
for l in open(symf):
    p = l.split()
    if len(p) == 3: sym[p[2].split('.lto_priv.')[0]] = int(p[0], 16)

def dump(name):
    if control == 'missing-round' and name == 'round%d_standings' % R: return None
    try: return open('%s/%s.wram-high' % (run, name), 'rb').read()
    except OSError: return None

def cup(d):
    a = sym['_cup'] - 0x06000000
    st, c, diff, rnd = d[a:a + 4]
    ents = d[a + 20:a + 142]; total = list(struct.unpack_from('>30i', d, a + 144))
    last_place = list(d[a + 264:a + 294])
    L = a + 324
    lc, lr, lco, cnt, out, pp, kos, valid = d[L + 4:L + 12]
    rows = [struct.unpack_from('>BBBBI', d, L + 20 + 8 * i) for i in range(cnt)]
    return dict(state=st, diff=diff, total=total, last_place=last_place, last_round=lr, course=lco, outcome=out,
                player_place=pp, rows=rows, count=ents[0])

def field(d):
    a = sym['_veh'] - 0x06000000; out = []
    for i in range(30):
        dist, lat, spd, lv, lap, node = struct.unpack_from('>iiiiiI', d, a + 64 * i)
        out.append((i, lap, dist, spd))
    return [i for i, *_ in sorted(out, key=lambda v: (-v[1], -v[2]))], out   # front first

fails = []
def check(ok, what):
    if not ok: fails.append(what)
    return ok

prev = None; prev_total = [0] * 30; transitions = 0; c = None
for r in range(1, R + 1):
    s, res, sta = dump('round%d_start' % r), dump('round%d_results' % r), dump('round%d_standings' % r)
    if not check(s is not None and res is not None and sta is not None,
                 'round %d: a dump is missing (start %s, results %s, standings %s)' % (r, s is not None, res is not None, sta is not None)):
        break
    order, cars = field(s)
    print('round %d start: front-to-back %s  (moving %d)' % (r, ' '.join(map(str, order)), sum(1 for x in cars if x[3])))
    if prev is not None:
        want = [row[0] for row in prev['rows']][::-1]          # last place starts at the front
        same = want == order
        print('  reversed previous finish %s -> %s' % (' '.join(map(str, want)), 'MATCH' if same else 'DIFFER'))
        transitions += check(same, 'round %d grid is not the previous finish reversed' % r)
    c = cup(sta)
    fin = [row[0] for row in c['rows']]
    if control == 'wrong-points' and r == 2:   # a monotone but wrong award, written into the dump's totals
        for k, row in enumerate(c['rows']):
            c['total'][row[0]] = prev_total[row[0]] + (100 - k if row[2] else 0)
    print('  round %d (difficulty %d, cup round %d, course %d, outcome %d, player place %d) finish: %s' % (
        r, c['diff'], c['last_round'], c['course'], c['outcome'], c['player_place'], ' '.join(map(str, fin))))
    check(c['outcome'] == 1 and len(fin) == c['count'] == 30 and sorted(fin) == list(range(30)),
          'round %d: not a 30-entrant finish (outcome %d, rows %d)' % (r, c['outcome'], len(fin)))
    want_pts = [0] * 30
    for k, row in enumerate(c['rows']):
        want_pts[row[0]] = POINTS[k] if row[2] else 0
    got_pts = [c['total'][e] - prev_total[e] for e in range(30)]
    bad = [e for e in range(30) if got_pts[e] != want_pts[e]]
    print('  points by place: %s' % ' '.join(str(got_pts[e]) for e in fin))
    prow = [row for row in c['rows'] if row[0] == 0]
    pms = prow[0][4] if prow else None
    print('  player time %s ms (status %s)' % (pms, prow[0][1] if prow else '-'))
    if player_ms is not None:
        check(r <= len(player_ms) and pms == player_ms[r - 1],
              'round %d: player time %s ms, HOST %s' % (r, pms, player_ms[r - 1] if r <= len(player_ms) else '-'))
    check(not bad, 'round %d: points differ from the schedule for entrants %s' % (r, bad))
    check(all(c['last_place'][e] == k + 1 for k, e in enumerate(fin)), 'round %d: last_place inconsistent' % r)
    prev, prev_total = c, list(c['total'])

if c is not None and not any(f.startswith('round %d:' % R) and 'missing' in f for f in fails):
    check(transitions == R - 1, 'grid transitions %d of %d' % (transitions, R - 1))
    check(c['state'] == CUP_FINAL, 'after round %d the cup state is %d, not FINAL' % (R, c['state']))
    lp = c['last_place']
    def key(e): return (-c['total'][e], 0 if e == 0 else 1, lp[e])
    # gp_ahead: points, then the player ahead of a tied rival, then the better last place
    final = sorted(range(30), key=key)
    print('FINAL STANDINGS: %s' % ' '.join('%d:%d' % (e, c['total'][e]) for e in final))
    print('player final place %d with %d points' % (final.index(0) + 1, c['total'][0]))
for f in fails: print('FAIL', f)
print('VERDICT', 'PASS' if not fails else 'FAIL', '(control %s)' % control if control else '')
sys.exit(0 if not fails else 1)
