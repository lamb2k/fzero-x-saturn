#!/usr/bin/env python3
"""Score the menu-audio runs (docs/MENU_AUDIO.md). EMULATOR tier (Ymir): audio captured at the SCSP output.

Evidence layout under EV (made by tools/engine_gate/scripts/menu-audio*.txt run with GATE_AUDIO=wav):
  builds/<build>/fzs-engine.sym         the image's symbols (the counters are read from the work-RAM dumps at each mark)
  runs/<run>/{result.txt,audio.wav,<mark>.wram-high,<mark>.wram-low}
Runs: a (menu-audio.txt), fx (menu-audio-fx.txt), load (menu-audio-load.txt), cad-on / cad-off (menu-audio-cadence.txt,
ENG_SND_MENU on / off), and the controls ctl-read, ctl-stuck, ctl-fx, ctl-load (the check on a run that must fail it).
Usage: menu_audio_score.py EV [--build menu-test]. Exit 0 when every check passes and every control fails.
"""
import argparse
import struct
import sys
import wave
from pathlib import Path

import numpy as np


class Run:
    def __init__(self, ev, name, build):
        self.d = ev / 'runs' / name
        self.name = name
        self.sym = {}
        for line in (ev / 'builds' / build / 'fzs-engine.sym').read_text().splitlines():
            p = line.split()
            if len(p) >= 3:
                self.sym[p[2]] = int(p[0], 16)
        self.marks, self.fields, self.kv = {}, 0, {}
        for line in (self.d / 'result.txt').read_text().splitlines():
            p = line.split()
            if p[:1] == ['mark']:
                self.marks[p[1]] = int(p[3])
            elif p[:1] == ['fields']:
                self.fields = int(p[1])
        self._dump = {}
        w = wave.open(str(self.d / 'audio.wav'), 'rb')
        self.n = w.getnframes()
        self.x = np.frombuffer(w.readframes(self.n), dtype='<i2').reshape(-1, 2).astype(np.float64)
        self.spf = self.n / self.fields

    def word(self, mark, sym, i=0):
        if mark not in self._dump:
            self._dump[mark] = ((self.d / (mark + '.wram-high')).read_bytes(), (self.d / (mark + '.wram-low')).read_bytes())
        hi, lo = self._dump[mark]
        a = self.sym['_' + sym] + 4 * i
        if 0x06000000 <= a < 0x06100000:
            return struct.unpack('>I', hi[a - 0x06000000:a - 0x06000000 + 4])[0]
        return struct.unpack('>I', lo[a - 0x00200000:a - 0x00200000 + 4])[0]

    def seg(self, f0, f1):
        return self.x[int(f0 * self.spf):int(f1 * self.spf)]

    def rms(self, f0, f1):
        s = self.seg(f0, f1)
        return float(np.sqrt((s ** 2).mean())) if len(s) else 0.0

    def peak(self, f0, f1):
        s = self.seg(f0, f1)
        return int(np.abs(s).max()) if len(s) else 0

    def audible_fields(self, f0, f1, thr=60, blk=5):
        """Fields from f0 to the last 5-field block whose peak is above thr."""
        last = -1
        for k, f in enumerate(range(f0, f1, blk)):
            if self.peak(f, f + blk) > thr:
                last = k
        return (last + 1) * blk


def checks(ev, build):
    out = []   # (name, ok, detail)
    def add(name, ok, detail):
        out.append((name, bool(ok), detail))

    a = Run(ev, 'a', build)
    m = a.marks
    # status and the reads counter
    add('status', a.word('m_race2', 'engine_snd_menu_report', 0) == 1, 'status %d' % a.word('m_race2', 'engine_snd_menu_report', 0))
    r0, r1 = a.word('m_title_music', 'engine_snd_disc', 0), a.word('m_race2', 'engine_snd_disc', 0)
    v = a.word('m_race2', 'engine_snd_disc', 1)
    add('disc', v == 0 and r1 > r0 and a.word('m_race2', 'fzx_menu_test', 3) == 1,
        'reads %d -> %d, reads while music not quiet %d, read result %d' % (r0, r1, v, a.word('m_race2', 'fzx_menu_test', 3)))
    add('hold', a.rms(m['m_title_music'] + 25, m['m_read_done']) < 1.0 and a.word('m_read_done', 'fzx_menu_test', 4) <= 30,
        'silent from field %d to %d (rms %.1f), granted after %d frames' % (
            m['m_title_music'] + 25, m['m_read_done'], a.rms(m['m_title_music'] + 25, m['m_read_done']), a.word('m_read_done', 'fzx_menu_test', 4)))
    saved, last = a.word('m_resumed', 'engine_snd_menu_report', 9), a.word('m_resumed', 'engine_snd_menu_report', 10)
    add('resume', a.rms(m['m_resumed'] - 60, m['m_resumed']) > 200 and saved < last < saved + 3000,
        'rms %.0f in the 60 fields before the mark; FAD paused %d, now %d' % (a.rms(m['m_resumed'] - 60, m['m_resumed']), saved, last))
    saved2, last2 = a.word('m_resumed2', 'engine_snd_menu_report', 9), a.word('m_resumed2', 'engine_snd_menu_report', 10)
    add('cancel', a.word('m_cancel_done', 'fzx_menu_test', 2) == 1 and a.rms(m['m_resumed2'] - 60, m['m_resumed2']) > 200 and saved2 < last2 < saved2 + 3000,
        'a cancelled hold read nothing (reads %d), music back (rms %.0f), FAD %d -> %d' % (
            a.word('m_cancel_done', 'fzx_menu_test', 2), a.rms(m['m_resumed2'] - 60, m['m_resumed2']), saved2, last2))
    cur, st, lf = (a.word('m_changed', 'engine_snd_menu_report', i) for i in (11, 5, 10))
    add('target', cur == 15 and st == 2 and lf > saved2 + 3000,
        'wanted 15 while held: playing %d, fresh starts %d (no stale resume), FAD %d' % (cur, st, lf))
    add('select', a.word('m_select', 'engine_snd_menu_report', 2) == 6 and
        a.word('m_cursor', 'engine_snd_menu_report', 2) == 9 and a.word('m_confirm', 'engine_snd_menu_report', 2) == 11 and
        a.word('m_confirm', 'engine_snd_menu_report', 3) == 0,
        'effects played %d of %d posted (six route effects, two skin changes, balance, then 62 and 33), dropped %d' % (
            a.word('m_confirm', 'engine_snd_menu_report', 2), a.word('m_confirm', 'engine_snd_menu_report', 1), a.word('m_confirm', 'engine_snd_menu_report', 3)))

    f = Run(ev, 'fx', build)
    fm = f.marks
    seq = [30, 16, 33, 32, 22, 62, 17]
    quiet = f.rms(fm['f_quiet'] - 20, fm['f_quiet'])
    res = []
    for k, sid in enumerate(seq[:-1]):
        f0 = fm['p%d' % sid]
        f1 = fm['p%d' % seq[k + 1]] - 3
        res.append((sid, f.peak(f0, f1), f.audible_fields(f0, f1)))
    gate32 = [d for sid, _, d in res if sid == 32][0]
    add('effects', quiet < 1.0 and all(p > 500 for _, p, _ in res),
        'quiet rms %.1f; peak by effect %s' % (quiet, ', '.join('%d:%d' % (sid, p) for sid, p, _ in res)))
    add('gate', 0 < gate32 <= 45, 'NA_SE_32 audible %d fields (gate 24 fields plus the key-off)' % gate32)
    add('unknown', f.word('f_end', 'engine_snd_menu_report', 3) == 1 and f.peak(fm['p17'], fm['f_end']) == 0,
        'id 17 dropped %d, silent; played %d' % (f.word('f_end', 'engine_snd_menu_report', 3), f.word('f_end', 'engine_snd_menu_report', 2)))

    l = Run(ev, 'load', build)
    lm = l.marks
    aud = l.audible_fields(lm['l_confirm'], lm['l_confirm'] + 200)
    after = l.audible_fields(lm['l_load'], lm['l_load'] + 120)
    add('load', lm['l_confirm'] <= lm['l_load'] and aud >= 120 and after >= 40,
        'confirm effect audible %d fields from the confirm; %d of them after LOAD began (field %d, confirm %d)' % (
            aud, after, lm['l_load'], lm['l_confirm']))

    on, off = Run(ev, 'cad-on', 'menu-scenes'), Run(ev, 'cad-off', 'nomenu')
    g = lambda r: (r.word('c_race2', 'fzx_scn_ctl', 7), r.word('c_race2', 'fzx_scn_ctl', 6))
    add('cadence', g(on)[0] == 0 and g(on)[1] <= 2 and g(on) == g(off),
        'menu gaps over two %d, worst gap %d fields (menu audio off: %d, %d)' % (g(on) + g(off)))

    # failed boot assets: navigation continues (the run reaches the race), the other half keeps working
    p = Run(ev, 'fail-pack', 'menu-test3')
    add('fail-pack', p.word('x_race2', 'engine_snd_menu_report', 0) == 2 and p.rms(p.marks['x_title'] - 40, p.marks['x_title']) > 200 and
        p.word('x_race2', 'engine_snd_menu_report', 2) == 0 and p.word('x_race2', 'fzx_menu_test', 3) == 1,
        'no effect pack: status %d, music rms %.0f, effects played %d of %d posted, the hold read ok, race reached at field %d' % (
            p.word('x_race2', 'engine_snd_menu_report', 0), p.rms(p.marks['x_title'] - 40, p.marks['x_title']),
            p.word('x_race2', 'engine_snd_menu_report', 2), p.word('x_race2', 'engine_snd_menu_report', 1), p.marks['x_race2']))
    t = Run(ev, 'fail-toc', 'menu-test4')
    add('fail-toc', t.word('x_race2', 'engine_snd_menu_report', 0) == 3 and t.rms(t.marks['x_title'] - 40, t.marks['x_title']) < 1.0 and
        t.word('x_read', 'fzx_menu_test', 4) == 0 and t.word('x_read', 'fzx_menu_test', 3) == 1 and t.word('x_race2', 'engine_snd_menu_report', 2) >= 1,
        'no TOC: status %d, silent title (rms %.1f), the hold was granted after %d frames and the read was ok, effects played %d, race reached at field %d' % (
            t.word('x_race2', 'engine_snd_menu_report', 0), t.rms(t.marks['x_title'] - 40, t.marks['x_title']), t.word('x_read', 'fzx_menu_test', 4),
            t.word('x_race2', 'engine_snd_menu_report', 2), t.marks['x_race2']))
    return out


def controls(ev, build):
    out = []
    def add(name, failed, detail):
        out.append((name, failed, detail))
    r = Run(ev, 'ctl-read', build)
    v = r.word('r_read_done', 'engine_snd_disc', 1)
    add('ctl-disc (read with no hold)', v >= 1, 'reads while music not quiet %d (the disc check needs 0)' % v)
    s = Run(ev, 'ctl-stuck', build)
    rr = s.rms(s.marks['s_stuck'] - 60, s.marks['s_stuck'])
    add('ctl-resume (hold never released)', rr < 200, 'rms %.0f in the 60 fields before the mark (the resume check needs > 200)' % rr)
    x = Run(ev, 'ctl-fx', build)
    pk = x.peak(x.marks['f_quiet'], x.marks['f_end'])
    add('ctl-effects (effects switched off)', pk <= 500, 'peak %d across all effects (the effect check needs > 500 each)' % pk)
    c = Run(ev, 'ctl-load', 'menu-test2')
    lm = c.marks
    aud = c.audible_fields(lm['l_load'], lm['l_load'] + 120)
    add('ctl-load (leave stops the effects)', aud < 40, 'effect audible %d fields after LOAD began (the load check needs >= 40)' % aud)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ev', type=Path)
    ap.add_argument('--build', default='menu-test')
    a = ap.parse_args()
    ok = True
    print('| check | result | detail |\n| --- | --- | --- |')
    for n, o, d in checks(a.ev, a.build):
        print('| %s | %s | %s |' % (n, 'PASS' if o else 'FAIL', d))
        ok &= o
    for n, f, d in controls(a.ev, a.build):
        print('| %s | %s | %s |' % (n, 'fails as required' if f else 'DID NOT FAIL', d))
        ok &= f
    print('\nEMULATOR tier (Ymir, audio at the SCSP output). Hardware not run.')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
