# Road stream: C3 in progress

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

The bake now emits complete-file FNV identities for both files. Regeneration
leaves the metadata and texture payloads byte-identical to C2, at 151,128 and
143,976 bytes. The original pack passes GCC and Clang with ASan/UBSan. Synthetic
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

- Obtain the requested guard-band clipping contract from the library owner.
  The near-only contract leaves source-visible pieces outside VDP1's coordinate
  range; see [the dependency qualification](SH2ZAM_VIEW_ADOPTION.md).
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
