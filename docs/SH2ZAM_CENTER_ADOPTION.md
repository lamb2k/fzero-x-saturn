# SH2ZAM view-center-1 adoption

Pointer-only commit `8a61816` pins `view-center-1` at
`7de29ac0e8983f41306681363ca37c3441c3509e`. Isolated synthetic qualification
completed before any port caller edits. It includes the already-qualified
source-order guard and shade contracts; no library source was edited.

## Pointer qualification (HOST)

GCC and Clang strict/UBSan builds pass all eight selected suites before and
all eleven after: view, divider, edges, guard, merge, rounding, source-order
guard and shade, plus centre, integral equivalence and source-order centre.
The new centre oracle suites exercise fractional/boundary/extreme centres,
stale-centre rejection, crossing/snap/failure parity and mutation controls.
The source-order centre suite reports 77,969,279 checks per compiler; integral
source-order equivalence compares 2,746,894 calls / 1,582,210,368 output bytes
with zero differences. These are local reruns of the upstream independent oracle.

Separate original/truncating API recorders each cover ten entry points over
2,048 synthetic views, producing 28,868,608 bytes per arm. Before/after GCC and
Clang outputs are byte-identical within each family:

- Original: `934b8bc04fcd30f776e7153a2b92178009cc7254d03635f2646f8a6ede2fb242`.
- Truncating: `a1d06ae1ece43b72768761f4ad7ca1bfd5b35bc899cbf488134ab39411a840bf`.

The unchanged road caller also passes its synthetic fixtures against the new
pin. Private evidence and commands: `host/center-adoption/`; Linux build trees
were isolated in `/tmp/fzx-center-adoption`, with reports/logs copied out.
No retail fixtures were used to qualify the pointer.

## Separate caller change

`fzx_view16_from_source_center` preserves both Q16 centres and uses the existing
rotation, eye placement and half-open clip conversion. It lives in a separate
object, `src/fzx_view16_center.c`, so existing camera users retain their admitted
adapters. The experimental road caller uses centre batch/edge/source-order clip
APIs, preserves corner order, max_out=10 and negative-return rejection, and uses
the wrapped plain view for fog setup. No native path selects this caller yet.

Adapter tests pass 85,050 fractional source-projection samples, including
12648447 and 12648449, plus 9,807 integral samples byte-identical to the old
truncating path. Stale-centre and invalid-input rejection preserve output.
Rounding away the fraction is a failing mutation control.

The old caller grouping passes the full census but fails the held texture gate:
p99 1.308 px. The previously qualified private bounded-merge probe passes at
0.650 px. Its planner is therefore adopted in the experimental caller: step
original repetition boundaries, merge only complete periods under the tagged
one-pixel planner, and preserve partial-period phase. The frozen reference and
all gate thresholds remain unchanged. Failed-arm evidence is retained under
`host/road-stream-draw/center-runtime-20261117/`.

## Caller acceptance (HOST)

The actual experimental caller, with bounded merging, passes the unchanged
regular/+32 synthetic fixtures under GCC/Clang and the period, capacity and fog
mutation controls. Full census: 9,907 admitted views, zero exclusions, identical
GCC/Clang records; adaptive mean 197.557, p99 413, max 445 commands. Forced-first
control p99 821 / max 1,033 fails the p99 420 budget. Gather max is 552 positions,
unaligned records zero, source states unchanged.

All 13 held views pass the existing image gate: texture displacement p99
0.650 px, geometry maximum 1 px. This is the accepted percentile criterion,
not a claim of pixel-identical textures (maximum displacement is 2.5 px).
Actual caller census/held bytes match the qualified bounded probe exactly:

- Rows: `ea0a0c23893b9b20d44ce08c615bb9a91b1e2ccf76a8903cfaeb83948eda999d`.
- Held: `3b3e4cb915292f0e045d25a23b03a59b15588b5cd5c718ad63766554d93d3c3a`.

Private evidence: `host/road-stream-draw/center-admitted-20261117/` and
`host/center-adoption/caller-{adapter,bounded-road}.log`.

## Limits

HOST `_divu` uses the portable fallback. No native timing, VDP1 look or hardware
acceptance follows from these checks. The final combined scene, stack, memory
and 60 fps budgets remain open. The bounded planner adds two 65-word arrays and
a 64-byte run array locally (584 bytes before compiler allocation); target stack
cost must be measured before native adoption. No shade caller was enabled.
