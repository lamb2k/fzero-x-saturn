"""gp_order.py RUN_DIR SYM [ROUNDS] [--control missing-round|wrong-points]: Grand Prix cup acceptance from a scenes
harness run (claude-jgp ymir-jgp work-RAM-high dumps named by the script's marks: roundN_start, roundN_results,
roundN_standings). Reads the cup (engine_cup.h fzx_cup_t at _cup), the RESULTS record (fzx_classification_t at
_scn_cls, engine_scenes.c) and the field (engine_field.c veh[], 64 B each).

Every check must hold for PASS (C-097, C-127 finding 2):
- every round has its start, results and standings dump, each a whole work-RAM image (a missing or short dump is a
  FAIL, never a skipped round);
- the cup progresses: every start dump is RACING in cup round N-1 of the same cup with a run id not seen before, and
  every round's scored record names that cup, cup round N-1 and the cup's own course for that round (engine_cup.c
  cups[]: cup c, round r = course 6c + r), so the course and round sequences are distinct and in cup order;
- the RESULTS payload is the scored race: at roundN_results the RESULTS record (_scn_cls) and the cup's accepted record
  are both valid, carry the start dump's run id and their own hashes (engine_cup.c result/attempt hash), and are
  byte-equal to the record the standings dump scored (header and every active row);
- from round 2 the start order is the previous round's finish reversed (cpu.c: position = N - D_80115D50[i] + 1):
  ROUNDS - 1 transitions;
- each round's points are exact per entrant: entrant at place p gains gRacePositionPoints[p] (racer.c) when its row
  scores, else 0; totals are the running sums from 0;
- last_place equals each entrant's finishing place;
- the cup state at each standings dump is SCORED before the cup's last round and FINAL after it (state 5); the final
  standings (points, ties to the better last place, the player first on a tie with a rival; engine_scenes.c gp_ahead)
  are printed.

Controls (each must FAIL): --control missing-round drops the last round's standings dump; --control wrong-points
replaces round 2's award by a monotone but wrong schedule (100, 99, 98, ...). tests/test_gp_order.py builds synthetic
runs: C-127's (every course ID zero, FINAL throughout, empty RESULTS payloads) and each fault on its own must FAIL.
--player-ms a,b,...: also require the player's official time each round to equal these (HOST) values."""
import struct, sys

POINTS = [100, 93, 87, 81, 76, 71, 66, 62, 58, 54, 50, 47, 44, 41, 38,
          35, 33, 31, 29, 27, 25, 23, 22, 21, 20, 19, 18, 17, 16, 15]   # racer.c gRacePositionPoints
CUP_RACING, CUP_SCORED, CUP_FINAL = 2, 3, 5      # engine_cup.h FZX_CUP_*
CUP_ROUNDS, CUP_COUNT = 6, 4                     # engine_cup.c cups[]: four cups of six rounds, course 6c + r
CL_SIZE = 20 + 8 * 30                            # sizeof(fzx_classification_t)
CUP_LAST = 324                                   # offsetof(fzx_cup_t, last)
WRAM = 0x06000000

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
NEED = max(sym['_cup'] - WRAM + CUP_LAST + CL_SIZE, sym['_veh'] - WRAM + 64 * 30,
           (sym['_scn_cls'] - WRAM + CL_SIZE) if '_scn_cls' in sym else 0)

def dump(name):
    if control == 'missing-round' and name == 'round%d_standings' % R: return None
    try: return open('%s/%s.wram-high' % (run, name), 'rb').read()
    except OSError: return None

def fnv(h, b):
    for x in b: h = ((h ^ x) * 16777619) & 0xffffffff
    return h

def classification(d, a):
    """fzx_classification_t at offset a: header, active rows, and whether both hashes are its own (engine_cup.c)."""
    run_id, = struct.unpack_from('>I', d, a)
    head = d[a + 4:a + 11]
    c_cup, c_round, course, cnt, out, pp, kos, valid = d[a + 4:a + 12]
    rh, ah = struct.unpack_from('>II', d, a + 12)
    n = min(cnt, 30)
    rows = [struct.unpack_from('>BBBBI', d, a + 20 + 8 * i) for i in range(n)]
    body = b''.join(bytes(r[:4]) + struct.pack('<I', r[4]) for r in rows)
    h = fnv(fnv(2166136261, head), body)
    h2 = fnv(fnv(fnv(2166136261, struct.pack('<I', run_id)), head), body)
    return dict(run_id=run_id, cup=c_cup, round=c_round, course=course, count=cnt, outcome=out, player_place=pp,
                kos=kos, valid=valid, rows=rows, hashes_ok=cnt <= 30 and rh == h and ah == h2,
                raw=d[a:a + 20 + 8 * n])

def cup(d):
    a = sym['_cup'] - WRAM
    st, c, diff, rnd = d[a:a + 4]
    run_id, acc = struct.unpack_from('>II', d, a + 8)
    ents = d[a + 20:a + 142]; total = list(struct.unpack_from('>30i', d, a + 144))
    last_place = list(d[a + 264:a + 294])
    last = classification(d, a + CUP_LAST)
    return dict(state=st, cup=c, diff=diff, round=rnd, run_id=run_id, accepted=acc, total=total, last_place=last_place,
                last=last, last_round=last['round'], course=last['course'], outcome=last['outcome'],
                player_place=last['player_place'], rows=last['rows'], count=ents[0])

def field(d):
    a = sym['_veh'] - WRAM; out = []
    for i in range(30):
        dist, lat, spd, lv, lap, node = struct.unpack_from('>iiiiiI', d, a + 64 * i)
        out.append((i, lap, dist, spd))
    return [i for i, *_ in sorted(out, key=lambda v: (-v[1], -v[2]))], out   # front first

fails = []
def check(ok, what):
    if not ok: fails.append(what)
    return ok

prev = None; prev_total = [0] * 30; transitions = 0; c = None; the_cup = None; run_ids = []; seen = []
for r in range(1, R + 1):
    s, res, sta = dump('round%d_start' % r), dump('round%d_results' % r), dump('round%d_standings' % r)
    if not check(s is not None and res is not None and sta is not None,
                 'round %d: a dump is missing (start %s, results %s, standings %s)' % (r, s is not None, res is not None, sta is not None)):
        break
    if not check(all(len(x) >= NEED for x in (s, res, sta)),
                 'round %d: a dump is empty or short (start %d, results %d, standings %d B; need %d)' % (
                     r, len(s), len(res), len(sta), NEED)):
        break
    order, cars = field(s)
    print('round %d start: front-to-back %s  (moving %d)' % (r, ' '.join(map(str, order)), sum(1 for x in cars if x[3])))
    cs = cup(s)
    if the_cup is None:
        the_cup = cs['cup']
    check(cs['state'] == CUP_RACING and cs['cup'] == the_cup and cs['round'] == r - 1,
          'round %d: start dump is not racing cup %d round %d (state %d, cup %d, round %d)' % (
              r, the_cup, r - 1, cs['state'], cs['cup'], cs['round']))
    check(cs['run_id'] != 0 and cs['run_id'] not in run_ids,
          'round %d: start run id %d is zero or repeats an earlier round' % (r, cs['run_id']))
    run_ids.append(cs['run_id'])
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
    L = c['last']
    want_course = 6 * the_cup + (r - 1) if the_cup < CUP_COUNT else None
    check(c['cup'] == the_cup and c['round'] == r - 1 and L['cup'] == the_cup and L['round'] == r - 1 and
          L['course'] == want_course,
          'round %d: standings cup %d round %d, scored record cup %d round %d course %d (cup order: cup %d round %d course %s)' % (
              r, c['cup'], c['round'], L['cup'], L['round'], L['course'], the_cup, r - 1, want_course))
    check(L['valid'] == 1 and L['hashes_ok'] and L['run_id'] == cs['run_id'] and c['accepted'] == cs['run_id'],
          'round %d: scored record not this race (valid %d, hashes %s, run %d, accepted %d, start run %d)' % (
              r, L['valid'], 'own' if L['hashes_ok'] else 'wrong', L['run_id'], c['accepted'], cs['run_id']))
    want_state = CUP_FINAL if r == CUP_ROUNDS else CUP_SCORED
    check(c['state'] == want_state, 'round %d: standings cup state %d, want %d' % (r, c['state'], want_state))
    # RESULTS shows the record the standings scored: the RESULTS scene's record and the cup's accepted one.
    shown = [('cup', cup(res)['last'])]
    if '_scn_cls' in sym:
        shown.append(('RESULTS', classification(res, sym['_scn_cls'] - WRAM)))
    for what, x in shown:
        check(x['valid'] == 1 and x['hashes_ok'] and x['run_id'] == cs['run_id'] and x['raw'] == L['raw'],
              'round %d: %s record at results (valid %d, hashes %s, run %d, %d rows) is not the scored one (run %d, %d rows)' % (
                  r, what, x['valid'], 'own' if x['hashes_ok'] else 'wrong', x['run_id'], x['count'], L['run_id'], L['count']))
    seen.append((L['round'], L['course']))
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

if c is not None and len(seen) == R:
    check(len(set(seen)) == R and [x[0] for x in seen] == list(range(R)),
          'rounds and courses do not progress: %s' % seen)
    check(transitions == R - 1, 'grid transitions %d of %d' % (transitions, R - 1))
    lp = c['last_place']
    def key(e): return (-c['total'][e], 0 if e == 0 else 1, lp[e])
    # gp_ahead: points, then the player ahead of a tied rival, then the better last place
    final = sorted(range(30), key=key)
    print('FINAL STANDINGS: %s' % ' '.join('%d:%d' % (e, c['total'][e]) for e in final))
    print('player final place %d with %d points' % (final.index(0) + 1, c['total'][0]))
for f in fails: print('FAIL', f)
print('VERDICT', 'PASS' if not fails else 'FAIL', '(control %s)' % control if control else '')
sys.exit(0 if not fails else 1)
