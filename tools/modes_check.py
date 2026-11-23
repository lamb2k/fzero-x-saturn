#!/usr/bin/env python3
"""Modes gate scorer (plan P3 item 3): rival lap times of a native Practice or Grand Prix run against the reference run.

    modes_check.py RUN_DIR MARK SYM HOST_REPORT.json [--band 5] [--laps 2]

RUN_DIR/MARK.wram-high is the Ymir dump at the mark (tools/engine_gate/scripts/hold_*.txt), SYM the image's .sym, and the
report one fzx-gp-ref run (the original code, HOST) with the same course, class and seed the image's rows were made from.
Per rival the native lap ticks (SH2ZAM line field brain lap_tick, relative to the field's start tick) are read and
compared with the report's lap_end_ticks. The check passes when the rivals' mean lap time, laps 1..LAPS, is within BAND
percent of the reference's. Only means are scored: single rivals depend on contact. Layout of sh2z_field_brain_t from the
pinned SH2ZAM line field (size 88, lap_tick at 24, laps_seen at 52). Prints one line per lap and PASS/FAIL."""
import argparse, json, struct, sys
from pathlib import Path

BRAIN, LAP_TICK = 88, 24


def load_syms(p):
    out = {}
    for line in Path(p).read_text().splitlines():
        a = line.split()
        if len(a) >= 3:
            out[a[2]] = int(a[0], 16)
    return out


def native_laps(run_dir, mark, sym, cars=30):
    hi = (Path(run_dir) / (mark + '.wram-high')).read_bytes()
    lo = (Path(run_dir) / (mark + '.wram-low')).read_bytes()

    def raw(a, n):
        a &= 0x07ffffff
        return hi[a - 0x06000000:a - 0x06000000 + n] if 0x06000000 <= a < 0x06100000 else lo[a - 0x00200000:a - 0x00200000 + n]
    base = sym['_brain']
    return [list(struct.unpack('>4i', raw(base + i * BRAIN + LAP_TICK, 16))) for i in range(cars)]


def lap_times(ends, laps):
    out, prev = [], 0
    for e in ends[:laps]:
        if e is None or e < 0:
            return None
        out.append(e - prev)
        prev = e
    return out if len(out) == laps else None


def score(native, host, laps, band):
    rows, ok = [], True
    for lap in range(laps):
        n = [t[lap] for i in range(1, len(native)) if (t := lap_times(native[i], lap + 1))]
        h = [t[lap] for r in host['racers'] if r['id'] >= 1 and (t := lap_times(r['lap_end_ticks'], lap + 1))]
        if not n or not h:
            rows.append((lap + 1, len(n), len(h), None)); ok = False; continue
        mn, mh = sum(n) / len(n), sum(h) / len(h)
        err = 100.0 * (mn - mh) / mh
        rows.append((lap + 1, len(n), len(h), mn, mh, err))
        ok = ok and abs(err) <= band
    return ok, rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('run_dir'); ap.add_argument('mark'); ap.add_argument('sym'); ap.add_argument('host')
    ap.add_argument('--band', type=float, default=5.0); ap.add_argument('--laps', type=int, default=2)
    a = ap.parse_args()
    host = json.loads(Path(a.host).read_text())
    ok, rows = score(native_laps(a.run_dir, a.mark, load_syms(a.sym)), host, a.laps, a.band)
    for r in rows:
        print('lap %d: native %d rivals, reference %d rivals' % r[:3] + ('' if r[3] is None else
              ', mean %.0f vs %.0f ticks, %+.2f%%' % r[3:]))
    print('%s modes_check rival lap times (band %.1f%%)' % ('PASS' if ok else 'FAIL', a.band))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
