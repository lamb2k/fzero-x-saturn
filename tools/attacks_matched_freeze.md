# Matched-start additional score: freeze record (C-142 item 1, HOST)

Frozen before any untouched trajectory was scored. The score is `tools/attacks_matched.py score`; the population of
untouched trajectories is `tools/attacks_heldout_c142.json` (14 scenarios chosen by a seeded generator from the
reference's scenario parameters alone; no native window has run on it). Hashes of both files are recorded in the commit
that adds this record; the files are not edited after it:

    attacks_matched.py          b4fe279074ca2893df4bdc11a632bc81fc0b2cc4d817f0e6bda5f950dfb5136f
    attacks_heldout_c142.json   3bb9be8da5d36180a2f211591f3289b16cf90008702661d9befa9b36a745ba60

## Definition

For a judge window (the judge's own plan: T, length n, the pack p0.pack/p1.pack = the reference state after tick T in the
native formats):

1. The entry state serialized by the native is the pack's state block. `attacks_matched.py table` publishes, per key, the
   format, the count of values, how many the conversion changes and the largest change. Conversion: the source float,
   nearest fixed-point value in the pack's format (`tools/physics_pack.py fx`, ties away from zero), then the nearest
   float32 (what the reference stores). Formats: pos, vel, fwd, spos, disp, ground, bvel Q16; body, road, gravity_up,
   tilt_up, ground_up Q30; scalars t/prop Q30, four Q16 widths and height, three Q12 distances, speed/energy/hover Q16,
   accel_force Q(force_frac), spin steer Q16; dynamics slide boost .. wall bounce Q16, tilt input Q30. Integer fields
   (flags, node, timers, attack state, lap data) and every field the pack does not hold are the original run's.
2. The reference (same code, same pads from ref.pads, same simulation phase) is replayed from tick 0 with
   `--load-state T+1:SRC0:SRC1`: just before tick T+1 the converted state replaces the baked fields. This run is the
   converted-start reference. Self-check: loading the UNCONVERTED state reproduces the original trace and pair log exactly.
3. The matched-start score of a native run of that window is the judge's ordinary C-092 requirement set applied against the
   converted-start reference in place of the original: pair events by type, count, order within one tick; damage within
   max(0.1, 3%); exact KO attacker and victim and credits; position within 32 units at the end; speed within
   max(q, 0.005 |v|) at the end for both racers. No exclusion, no shortened window, no native-dependent grading. The
   judge's functions are called unchanged (attacks_contact_judge.py at the freeze commit of C-092/C-116).
4. Paired diagnostic (`paired`): the converted-start reference scored against the original reference with the same
   functions; signed end errors, both event sequences, the first divergent predicate or discrete writer. A window that fails
   is a deterministic paired failure. `sensitivity` (seeded +-N q start velocities) is reported apart and grants nothing.
5. Both scores are reported, with denominators: the ordinary C-092 score (original reference) and the matched-start score.
   Neither changes C-092 or C-116.

## Self-checks and controls (frozen results, on the inspected sets)

- Load identity (the unconverted state reproduces the trace and the pair log of every window): c136 1019 of 1019, judge 2054 of
  2054, ho-pads 1411 of 1411, ho-starts 1966 of 1966, fresh 1460 of 1460.
- Controls (native runner planted faults, c136 set, F=16): the matched score must lose at least 90% of the windows the
  ordinary C-092 score loses for the same fault. Result: share-swap 212/212, credit-off 42/42, spin140 31/31,
  opp-player 42/45, push2.5 140/140, reach45 224/224, coast-bias 257/258, heavy5 126/125, impact1.2 223/223
  (matched drop / C-092 drop). no-destroy: no drop in either score on the c136 set (0 / 0) and on the judge set
  (0 / 0): neither score catches it on these populations (they hold no crashed machine the fault would keep alive).
- Failing a control on the untouched set is reported, not repaired.
