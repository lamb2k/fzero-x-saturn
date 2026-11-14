# Tick cost model (card G1.3)

How much float work the original one-racer tick does, and what it would cost as SH2ZAM fixed
point on the master SH-2. **Every cycle figure here is MODELLED.** Op counts are reference tier.

## Method

1. `tools/census/closure.py` counts float operations per source line (libclang):
   `docs/census/float_ops_by_line.tsv`.
2. `tools/cost/float_ops.py` runs the reference's profiling build (`make -C ref PROFILE=1
   OPT=-O0`, gcov plus per-tick call counts) and weights each line's operations by its execution
   count. Totals and means are exact source-operation counts; per-tick p95 and max use average
   operations per call (MODELLED). The profiled build gives the gate's hashes.
3. `tools/cost/cycle_model.py` prices them: mul, div and sqrt at SH2ZAM's own modelled cycles
   per call (its math model, `bounded` profile: 41.3, 266.1 and 445.3 cycles, including call
   overhead), add/sub, compare, negate and convert at assumed short inline costs (4, 4, 2, 4).
   Integer work, memory traffic and control flow are not counted; the table also shows the
   float-only figure times 3.

Budget lines (plan 4.4, proposed until measured): master 8 ms = 229,091 cycles at 28.64 MHz;
one NTSC field = 477,750 cycles.

## Results

Mute City 1, retail Blue Falcon tuning, ROM-lane inputs M02 to M05 (21,248 ticks; only these
counts leave the evidence folder):

| Per tick | Float ops | Modelled cycles (float only) | Share of 8 ms | x3 overhead |
| --- | --- | --- | --- | --- |
| mean | 782.5 | 32,144 | 14.0% | 42.1% |
| p95 | 813.2 | 33,405 | 14.6% | 43.7% |
| max | 1,152.8 | 47,356 | 20.7% | 62.0% |

Mean mix: 294 add/sub, 345 mul, 23 div, 23 sqrt, 63 compare, 13 negate, 21 convert.

Ranked by modelled cycles: `Racer_UpdateFromControls` 11,089 (314 ops),
`Math_OrthonormalizeAroundForward` 6,597 (3 calls, sqrt and div heavy), chase camera
`Camera_SettingsUpdateFollowSmooth` 5,000 (not in tier T), track query `func_8009EBEC` 3,720,
`Math_OrthonormalizeAroundUp` 2,419. Synthetic fixture (committed TSVs in this folder): mean
717.5 ops, 30,227 cycles, 13.2% of the master line.

## Reading

On this model the original simulation tick fits the 8 ms master line with room, at the mean and
the worst tick, even with a threefold allowance for unmodelled work. Square roots and divides are
17 of every 100 float operations by count but half the cycles: they are where cartridge tables
and reciprocal reuse pay first (plan G5.4).

## Control: one function timed on the SH-2 (EMULATOR / MODELLED)

`saturn/bench` times `Math_OrthonormalizeAroundForward` (64 bases, FRT clock/8, interrupts
masked, best of 8 passes) in Yabause 0.9.15. CI's emulator job uploads its still.

| Version | Cycles per call | Max error vs float |
| --- | --- | --- |
| original float (GCC software float, integer correctly rounded sqrt) | 3,843 | reference |
| SH2ZAM straight translation (`vec3_normalize`, `vec3_cross`) | 3,076 | 4 LSB of Q16.16 |
| SH2ZAM in the original's shape (`rsqrt`, scale, cross) | 1,950 | 5 LSB |

With SH2ZAM `3ddb84c` (pinned 2026-11-14: newer normalize, DIV1 division) the straight
translation drops to 2,485 cycles and the rsqrt version to 1,934; errors unchanged.

The model above prices this function at 6,597 cycles for 3 calls a tick, 2,199 per call: within
13% of the rsqrt version, so the model ranks work sensibly. How the code is converted matters as
much as fixed versus float (1.6x between the two SH2ZAM versions). Software float was only 2x
the best fixed version here; that keeps it on the table for rare, parity-sensitive paths, not
for the tick (it was a sqrt-and-cross function; divide-heavy code will differ).

## Not yet measured (rest of G1.3)

- Cartridge-resident data: this emulator presents no cartridge (docs/EMULATOR.md). The bench
  image measures it on a console or another emulator unchanged.
- Integer and memory cost of the converted tick; that replaces the x3 allowance.
- Hardware: every number here is emulator or model. The hardware sheet (G4.5) runs this image.
