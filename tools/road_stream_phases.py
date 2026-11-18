#!/usr/bin/env python3
"""Decode the default-off C3 per-phase profile (FZX_ROAD_STREAM_PHASES=1). MODELLED.

Inputs are a native image's .sym file and full low/high work-RAM snapshots taken
at the end of an uninterrupted replay (the same capture as tools/frame_profile.py).
Each phase row is reconciled with the same frame's scene-4 road ticks; the decoder
rejects layout, guard, count or reconciliation disagreements instead of guessing.
"""
import argparse
import json
import math
from pathlib import Path
import struct

PHASES = ['other', 'eye_view', 'gather', 'batch_center_divu', 'quad_setup_cull', 'guard_clip',
          'edge_steps', 'merge_plan', 'fog_fade', 'command_build', 'native_convert',
          'clip_setup_report']
# Version 2 splits quad setup: fetch/trivial reject, whole-quad winding, per-piece.
PHASES_V2 = PHASES + ['quad_fetch_reject', 'quad_winding_cull', 'piece_setup']
COUNTS = ['boundaries', 'stepped_points', 'quad_clip_calls', 'piece_clip_calls', 'clip_corners',
          'pieces', 'merge_calls', 'edge_calls', 'switches', 'positions', 'quads', 'culled',
          'clipped', 'commands', 'chunks', 'fog_updates']
GUARD = bytes.fromhex('39c5a71e') * 8
# Largest per-frame |phase sum - scene4| in ticks: the two boundary stamps and the
# final phase switch lie a few instructions apart, never a phase's worth of work.
RECONCILE_TICKS = 64


def symbols(text):
    out = {}
    for line in text.splitlines():
        w = line.split()
        if len(w) == 3:
            out[w[2].split('.lto_priv.')[0]] = int(w[0], 16)
    return out


def stats(values):
    s = sorted(values)
    n = len(s)
    return dict(mean=sum(s) / n, p50=s[math.ceil(.5 * n) - 1], p99=s[math.ceil(.99 * n) - 1],
                maximum=s[-1])


def decode(sym_text, low, high):
    sym = symbols(sym_text)

    def read(a, n):
        a &= 0x1fffffff
        for start, data in ((0x06000000, high), (0x00200000, low)):
            if start <= a and a + n <= start + len(data):
                return data[a - start:a - start + n]
        raise ValueError('uncaptured RAM %x+%d' % (a, n))

    version = struct.unpack('>2I', read(sym['_fzx_road_phase_profile'], 8))[1]
    names = {1: PHASES, 2: PHASES_V2}.get(version)
    if names is None:
        raise ValueError('phase version')
    p = len(names)
    stride = 4 * p + 32
    h = struct.unpack('>%dI' % (12 + 2 * p), read(sym['_fzx_road_phase_profile'], 4 * (12 + 2 * p)))
    if h[0] != 0x43335048 or h[4] != stride or h[8] != p or h[9] != len(COUNTS):
        raise ValueError('phase header')
    rows_at, capacity, written, seen, end, dropped = h[2], h[3], h[5], h[6], h[7], h[10]
    if read(rows_at - 32, 32) != GUARD or read(end, 32) != GUARD or end != rows_at + capacity * stride:
        raise ValueError('phase guards or extent')
    fh = struct.unpack('>16I', read(sym['_fzx_frame_profile'], 64))
    rh = struct.unpack('>8I', read(sym['_fzx_frame_road_profile'], 32))
    frames = fh[4]
    if rh[5] != frames or seen != frames or written != min(frames, capacity) or dropped != max(0, frames - capacity):
        raise ValueError('phase rows disagree with frame rows')
    frame_rows = list(struct.iter_unpack('>16I', read(fh[3], frames * 64)))
    road = list(struct.iter_unpack('>2I', read(rh[2], frames * 8)))
    rows = [struct.unpack('>%dI16H' % p, read(rows_at + i * stride, stride)) for i in range(written)]

    def delta(a, b):
        return (a - b) & 0xffffffff
    fields = delta(frame_rows[-1][12], frame_rows[0][12])
    tick_ms = fields * 1000 / fh[10] / delta(frame_rows[-1][10], frame_rows[0][10])
    diffs = [sum(r[:p]) - road[i][0] for i, r in enumerate(rows)]
    # A frame whose sum is off by a whole 16-bit FRT period (within the usual
    # tolerance) holds one interval that read as negative: an interrupt read the
    # FRC between this phase stamp's FRCH and FRCL reads (shared latch). Such
    # frames are counted and left out of the per-phase statistics; any other
    # disagreement is still rejected.
    wrapped = [i for i, d in enumerate(diffs) if abs(d) > RECONCILE_TICKS and
               abs(d - round(d / 65536) * 65536) <= RECONCILE_TICKS and round(d / 65536) != 0]
    if any(abs(d) > RECONCILE_TICKS for i, d in enumerate(diffs) if i not in wrapped):
        raise ValueError('phase sum does not reconcile with scene 4')
    if len(wrapped) > max(1, len(rows) // 100):
        raise ValueError('too many FRT latch-race frames')
    keep = [i for i in range(len(rows)) if i not in wrapped]
    rows = [rows[i] for i in keep]
    road = [road[i] for i in keep]
    diffs = [diffs[i] for i in keep]
    if any(r[p + 13] + 1 != road[i][1] for i, r in enumerate(rows)):
        raise ValueError('phase command count disagrees with scene 4 (plus user clip)')
    totals = [h[12 + 2 * i] + (h[13 + 2 * i] << 32) for i in range(p)]
    all_ticks = sum(totals)
    return dict(
        tier='MODELLED', hardware='UNRUN', version=version, frames=frames, rows=written, dropped=dropped,
        frt_race_frames_excluded=len(wrapped),
        tick_ms=tick_ms, scene4_road_ms=stats([t * tick_ms for t, c in road]),
        phase_sum_ms=stats([sum(r[:p]) * tick_ms for r in rows]),
        reconcile=dict(per_frame_phase_minus_scene4_ticks=stats(diffs), min_ticks=min(diffs),
                       total_phase_ticks=all_ticks, total_scene4_ticks=sum(t for t, c in road),
                       total_ratio=all_ticks / sum(t for t, c in road)),
        phases_ms={n: dict(**stats([r[i] * tick_ms for r in rows]),
                           total_mean=totals[i] * tick_ms / frames, share=totals[i] / all_ticks)
                   for i, n in enumerate(names)},
        counts={n: stats([r[p + i] for r in rows]) for i, n in enumerate(COUNTS)},
        scope='Scene 4 partitioned into exclusive phases; includes interrupts taken in a phase '
              'and the profile switch cost. Emulator timing, not hardware.')


def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument('sym', type=Path)
    a.add_argument('wram_low', type=Path)
    a.add_argument('wram_high', type=Path)
    a.add_argument('--output', type=Path)
    args = a.parse_args()
    result = decode(args.sym.read_text(), args.wram_low.read_bytes(), args.wram_high.read_bytes())
    text = json.dumps(result, indent=2)
    if args.output:
        args.output.write_text(text + '\n', encoding='utf-8')
    print(text)


if __name__ == '__main__':
    main()
