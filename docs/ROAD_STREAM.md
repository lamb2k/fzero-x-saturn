# Road stream: C3 in progress

## Current acceptance: owner decision, 2026-11-17

C3 moves to a Saturn-native renderer. Packet, command and per-view hash identity
with the frozen renderer across the census, route and regular/+32 fixtures is
lifted. Source projection rounding, guard-clip order/snap, fog thresholds and
the merge per-repetition bound are no longer gates. SH2ZAM tags remain usable
but their source-matching contracts are optional.

Compare with the original game's image: report geometry/texture displacement
in screen pixels, colour, cracks, holes, popping, wrong order and wrong fog.
No numeric visual threshold is set. The current first-corner fan mapping has a
visible corner-order defect; fix or replace the mapping without imposing source
arithmetic identity. Keep 16 KiB unused stack, allocation guards, no crashes or
hangs, road420/vehicle70 command budgets, legacy default, hygiene/ROM rules and
honest evidence tiers. Opus owns C3 performance on `claude/c3-c4-perf`.

## Historical experiments and superseded gates

The sections below preserve the earlier sequence of experiments. Their identity,
fixture, source-arithmetic and numeric visual requirements describe historical
gates, not current acceptance. Lifting them does not establish visual or speed
acceptance. Current policy is in [the render programme](RENDER_PLAN_CURRENT.md).

The compact pack reader, visible-vertex gather and camera adapter are implemented
and HOST-qualified. They compile for SH-2, but are not yet linked into the Saturn
race. Command generation, clipping, texture mapping, image-space qualification,
native residency and the one-CPU timing gate remain open. The existing road
renderer and the C0 whole-route baseline are unchanged.

## Camera conversion

`fzx_view16_from_source` converts the existing source camera to SH2ZAM's tagged
view contract once per camera update. It rounds the Q2.30 rotation to Q1.14 and
uses `sh2z_view_place` with the same source eye to derive translation. Scaling
the old translation independently produced errors up to three pixels in the
source replay because rotation rounding was then relative to the world origin.

The library rounds projected Y upward, while the current native road truncates
positive screen coordinates. The adapter rounds the source viewport centre to
the nearest pixel and subtracts one from Y to account for that convention.
Source viewport centres can be fractional, including during the transition at
input 506. Half-open source clipping becomes an inclusive library rectangle.
No library kernel or frozen reference renderer was changed.

GCC and Clang/UBSan agree over all 9,907 recorded source views: **1,291,264 visible,
on-screen original vertex instances**, maximum one pixel of error in either
coordinate, zero over the one-pixel bound. This is a vertex-projection check,
not the required clipped-piece and texture-coordinate image gate.

Synthetic tests cover 42,777 projections, fractional viewport centres, invalid
inputs and unchanged output on rejection. Removing the Y adaptation, changing
the inclusive clip conversion or swapping eye axes makes the controls fail.

## Pack admission and visibility

`fzx_road_stream_open` checks the compiled complete-file identity, embedded
checksum, section extents, original row references, chunk coverage, repetition
metadata reconstructed from source UVs, image modes/addresses and far provenance.
It allocates no storage. The texture loader must verify the separately compiled
complete texture-file hash before drawing; the offline receipt retains pixel,
palette and far-chord proofs. Far records are not activated by admission.

The bake now emits complete-file FNV identities for both files. The row-alignment correction retains source geometry/UV records and
duplicates lookup pixels horizontally where required. Packs occupy 151,128 and
147,328 bytes; the reader rejects the old unaligned lookup image widths. The original pack passes GCC and Clang with ASan/UBSan. Synthetic
valid-checksum corruptions, wrong identity, truncated payloads and insufficient
texture extent are rejected. A control disabling repetition validation admits
the deliberately wrong repetition record, proving the rejection test is active.

`fzx_road_stream_gather` follows the original road groups, directions and outer
wall state. It copies each referenced original position once and supplies a
global-to-local row map for one `sh2z_view_batch` call. The full replay matches
independently traversed original-pack visibility and coordinates exactly.
Maximum demand is **552 positions**, fitting a 576-record output allocation
(11,520 bytes for transformed records). A capacity of 551 fails at the actual
worst views with no usable output count. This is a required allocation size,
not yet proof that the combined Saturn memory layout fits.

SH-2 object compilation with the Yaul specs succeeds. Standalone text sizes are
608 bytes for the camera adapter and 3,037 bytes for pack admission plus gather;
both have zero static data/BSS. These are SDK build facts, not runtime timing or
whole-program memory results. The helpers are absent from the native build's
explicit source list, so this checkpoint changes no race executable.

## Remaining C3 work

- `view-guard-1` is [qualified and adopted](SH2ZAM_GUARD_ADOPTION.md).
  New callers still need their own image-space and coordinate-bound gates.
- Implement bounded piece stepping, depth/fog tables including dynamic source
  fog, and prebuilt native command templates behind `FZX_ROAD_STREAM=1`.
- Bind the compact packs, low-RAM row map and high-RAM transformed output to a
  measured memory/VRAM layout; remove the old vertex cache only on this path.
- Run the full image-space and wrong-repetition controls, forced-first-tier
  budget control, knob-off identity and native correctness gates.
- Measure the 13 held views including the 213 ms baseline worst view. Require
  p99 road CPU at most 8 ms MODELLED; above 16 ms stops this approach.

Private evidence: `host/road-stream/view16-source.json`, `pack-source.json`,
compiler outputs and logs. Public tests are `test_view16.py` and
`test_road_stream.py`. No native speedup, image acceptance or HARDWARE result is
claimed. C3 and the complete first-course goal remain unfinished.

## Bounded command-builder experiment: blocked

The local, unintegrated builder completes all 9,907 HOST views with matching
GCC/Clang UBSan records and zero unaligned texture rows. Adaptive command count
is p99 337 / maximum 357; forced first tier is p99 821 / maximum 1,033.
Geometry in the 13 held views stays within one pixel of the frozen renderer.
These counts do not establish image or native acceptance.

Two texture-mapping hypotheses fail the required one-source-texel p99 gate:
matching the reference clipper gives 9.333333 texels; preserving partial
endpoints while regrouping interior repetitions gives 9.333916 texels. The
second arm also reaches 429 commands. A shifted synthetic texture fixture
still emits 32 commands against the reference's 31; its unchanged assertion
fails. No failing experiment is included in the native build or published as
an accepted renderer, and the old road remains intact.

That experiment was stopped. Private evidence: `host/road-stream-draw/RESULTS.md`
and `qualification-blocked.json`. Its failed arms remain unqualified.

## Reopened texture investigation, owner authorization 2026-11-17

The owner explicitly permits changing the texture metric from one original-source
texel. The selected replacement is p99 at most one screen pixel, measured as
Chebyshev displacement of texture rows against the per-repetition reference.
Source-texel and drawn-tier-texel p99 remain required reports beside that metric.
The one-pixel geometry bound, 420-command budget, shifted +32 synthetic fixture,
and existing image-gate method remain unchanged. No native acceptance follows
from this authorization alone.

The HOST census tested both rails at every interior repetition boundary using
exact integer products, at 0.5, 1 and 2 screen pixels. Partial first/last
repetitions stay in the single-repetition tier; a merged run starts at phase
zero and spans only complete repetitions. No SH2ZAM file was changed.

| Boundary tolerance | Commands p99 / max, 9,907 views | Texture-row screen p99, 13 held views | Source / drawn-tier texel p99 |
| --- | --- | --- | --- |
| 0.5 px | 424 / 451 | 0.500 px | 8.000 / 3.868 |
| 1 px | 413 / 445 | 0.650 px | 9.328 / 3.633 |
| 2 px | 406 / 432 | 1.109 px | 9.333 / 3.620 |

**Choose `view-merge-1` pixel mode, tolerance 256 in Q8 (1 px).** Only this
arm meets both the command p99 and texture-row p99 limits in this experiment.
Its worst individual held-view p99 is 0.954 px; geometry coverage stays within
1 px in every held view. GCC and Clang/UBSan produce identical complete census
and held-command files for the 1 px arm. Forced first tier remains p99 821 /
max 1,033, failing the unchanged command budget.

The image check retains the frozen ideal ruled-quad inverse mapping, pixel
centres, painter order and periodic/mirrored texture equivalence. For each
comparable pixel it measures the displacement of the candidate texture row
along the reference piece at the same transverse coordinate. The reference
rail is locally extended when the equivalent row crosses a piece boundary;
this is a HOST local mapping metric, not native VDP1 raster acceptance.
Source-texel error remains reported and is not relabelled as passing its old
limit. The 1 px arm has 472,098 comparable textured samples; 1,086 exceed 1 px
and the maximum is 14.521 px. Separately excluded/reported are 270 material
mismatches and 10,116 common mean-row pixels with undefined longitudinal phase.
These tails and exclusions remain visible for native look qualification.

Analytic checks give zero for identity/full-period shifts, 1 px for a known
one-pixel row shift, and 49.5 px p99 for a doubled-repetition control. Doubling
repetitions in the actual held commands also fails (127.509 px p99). A separate
source-UV audit checks 1,824 textured held commands: 509 aligned merged runs,
691 single-repetition partials, and zero wrong start rows.

### Shifted fixture: projection-rounding diagnosis, still open

The unchanged +32 fixture fails at 32 versus 31 commands in the census arm.
The first extra command is quad 0's final partial repetition: its endpoint
projects to Y=191 while the frozen renderer projects Y=192, making a one-pixel
sliver that the reference discards. The following quad starts one pixel higher
in the reference. This failure is distinct from choosing the texture start row.

A private diagnostic subtracting 1 Q18.14 unit from view-space Y has no effect;
subtracting 16 units makes both regular and shifted fixtures pass unchanged,
including zero edge error. This is evidence of the rounding cause, not an
accepted camera adjustment. Its full-route GCC/Clang census still gives p99
413 / max 445 and its held-view texture p99 stays 0.650 px. The tagged projector floors its Q16 quotient
before subtracting from integer centre Y; the source truncates the final
projected coordinate. The existing centre-minus-one adapter differs at exact
integer boundaries. Resolve that contract with the SH2ZAM owner before runtime
adoption; do not change the frozen reference or weaken the shifted assertion.

Private evidence: `host/road-stream-draw/bounded-merge-20261117/`, including
`census.json`, `image-px*.json`, `metric-controls.json`, `phase-audit.json` and
shift diagnostics. The experiment and original failed evidence stay private.
C3 remains unintegrated: tagged planner adoption, shifted fixture resolution,
native memory/timing and VDP1 look gates are open. C8 finish/results WIP is
preserved; C6 still depends on a qualified C3 renderer.

## Tagged planner adoption, 2026-11-17

`view-merge-1` is now [independently qualified and pinned](SH2ZAM_MERGE_ADOPTION.md).
Its private caller produces the same complete census and held command bytes at
all three tolerances, including the image numbers above. The historical Ã—4
phase-start audit identifies partial-row quantization of up to 3.65625 source
rows in the reported 16/20-phase cases; the new complete-period alignment avoids
that merged-partial case. The +32 single-repetition fixture still fails at
32 versus 31 commands. No native stream or camera-bias change is admitted.

## Truncation caller update, 2026-11-17

The [opt-in integral-centre adapter](SH2ZAM_ROUND_ADOPTION.md) resolves the
unchanged regular/+32 fixture mismatch. Its HOST tests pass; no native path
selects it. Full-route admission now exposes 223 fractional source centres,
which remain rejected pending the Q16-centre library contract.

The integral subset retains p99 413 commands. Thirteen held images pass with
the reference clipper and consistent truncation (texture p99 0.650 px), but
library guard clipping fails texture mapping because it changes the polygon's
first corner and therefore the emitted fan. This is separate from merge
selection or texture start phase. Both contract requests went directly to the
library owner. C3 remains blocked for native adoption, with evidence preserved
in `host/road-stream-draw/trunc-20261117/`; the old reference remains frozen.

## Source-order guard adoption, 2026-11-17

[view-guard-src-1](SH2ZAM_GUARD_SRC_ADOPTION.md) is independently qualified and
pinned at e93dd4b (pointer-only commit 24e4a2b). The experimental command builder
uses its source-order truncating guard at both call sites, with max_out=10 and
negative-return failure preserved. The frozen reference is unchanged.
Regular/+32 fixtures pass both compilers, including mutation controls. All 13
held images pass (texture p99 0.650 px; geometry within 1 px). The integral
census admits 9,684 views, p99 413 commands; forced-first-tier p99 822 fails 420.
The 223 fractional views still require Q16 centres. Native adoption remains
blocked on that contract and native alignment/memory/timing/look gates.

## Q16 centre and committed bounded planner, 2026-11-17

[view-center-1](SH2ZAM_CENTER_ADOPTION.md) is independently qualified, with
pointer-only commit 8a61816. The separate centre adapter preserves fractional
centres; the experimental caller now uses centre batch/edge/source-order guard
APIs and the previously private bounded-merge planner. The simple grouping arm
failed texture p99 at 1.308 px; its evidence remains preserved.
The actual updated caller admits all 9,907 views, no exclusions, with identical
GCC/Clang outputs: p99 413 / max 445 commands, forced-first p99 821 fails 420.
All 13 held views pass (texture p99 0.650 px; geometry within 1 px), and regular
and shifted synthetic fixtures plus mutation controls pass unchanged.
This closes the HOST fractional-centre/anchor/merge image blockers. Native
integration, memory/stack, timing and VDP1 look qualification remain open.

## Native decisive experiment

[The native report](ROAD_STREAM_NATIVE.md) records the selected 7,059-input
run: source finish and memory guards pass, but road CPU p99 is 133.143 ms
MODELLED, above the 16 ms stop threshold. This implementation is a no-go.
The selected ideal-image gate also fails at input 684; the older HOST image
qualification above does not transfer across tuning and replay changes.
