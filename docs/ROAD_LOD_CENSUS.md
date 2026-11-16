# Road tier census: C1

The proposed 16 / 8 / 4 pixel thresholds pass the HOST command budget over
all 9,907 recorded source views. This authorizes proceeding to the road bake;
it does not establish image quality or Saturn speed. The running game still
uses the old road renderer and the [3.287 fps MODELLED baseline](FRAME_PERFORMANCE.md).

| Count, including one native road clip-setup command | Mean | p99 | Maximum |
| --- | ---: | ---: | ---: |
| Proposed repetition tiers | 168.862 | 335 | 355 |
| Forced full repetition control | 334.436 | 818 | 1,027 |

The gate is p99 at or below 420. The control exceeds it. Thresholds were not
retuned, and no far-merging saving is included. The worst candidate view is
input 750; the worst control view is input 938. These are source replay inputs,
not presented pictures from the slower native baseline.

## What was counted

- All 2,613 source vertices, 2,483 paired quads and 300 chunks remain available.
- The original visible-chunk plan, conditional outer walls, painter traversal,
  winding tests and clipping are retained.
- Repetition length uses the existing maximum parent-corner Manhattan span,
  divided by its UV repetition span. This conservatively includes road width.
  At least 16 pixels uses individual repetitions; 8 to 16 uses pairs; 4 to 8
  uses groups of four; below 4 uses one mean-row quad. Near crossings keep
  individual repetitions. Partial first/last repetitions retain their phase.
- A temporary HOST copy of the frozen renderer changes only that grouping and
  mean selection. Its real integer clipping/emission path counts the pieces.
  Grouped texture addresses are dummy values: no proposed tier pixels exist yet.
- The forced-first-tier control disables mean substitution as well as grouping.
- Every fixed-point camera matches the independent Python conversion exactly.
  A separate conservative parent bound also covers every emitted count.

GCC and Clang with undefined-behavior sanitization produce identical 9,907
view/count records. Synthetic tests cover threshold boundaries, negative UV
phase, source group wrapping, malformed inputs and fixed-point conversion.
The temporary adapter rejects changed reference sentinels. No Saturn source,
build flag, binary or SH2ZAM pointer changes in this card.

## Texture and far-tier sizing

The 28 strips contain 3 to 209 unique RGB1555 colours after the existing
source-compatible horizontal resampling. Only 8 fit 16 colours losslessly.
Quantizing the others requires an image-quality check; a 4-bit storage estimate
is not proof that their palettes are adequate.

| Proposed storage, retaining each strip's base width and height | Bytes |
| --- | ---: |
| Each periodic image tier: 1, 2, 4 or 8 repetitions | 34,816 at 16 bits |
| Four tiers plus per-strip mean rows | 140,576 at 16 bits |
| Same images with three full fog copies | 421,728 at 16 bits |
| Four tiers and mean rows at 4 bits, with one 16-colour palette per image | 39,624, sizing only |

The 140,576-byte single-set option fits the plan's 150 KB texture ceiling.
Three full fog copies do not. C2/C3 must explicitly choose and check palette
quantization, shared fog treatment or another bounded representation; carrying
over the old three-copy layout silently would fail the budget.

Topology provides 2,288 overlapping two-chunk chains and 1,952 four-chunk
chains with matching material/strip/outer state and exact shared endpoint rows.
Within a view's far tier, greedy disjoint selection finds up to 60 two-chunk
groups (120 original quads) or 24 four-chunk groups (96 original quads).
These alternatives are not additive. Intermediate curvature and appearance
are not yet checked, so no merge is credited to the passing command count.

## Reproduction and evidence

On a POSIX host with GCC or Clang and NumPy installed:

```sh
python3 tools/road_lod_census.py --cc gcc \
  --replay "$FZX_EVIDENCE/host/road-tiles/all-source.bin" \
  --output "$FZX_EVIDENCE/host/road-lod-census/gcc"
python3 -m unittest discover -s tests -p test_road_lod_census.py -v
```

The tool checks source/pack receipts and requires the JSON to match the native
pack's source hash. Outputs stay private: summary, per-input counts, converted
views and compiler/source/input receipts. Public tests use synthetic cameras.

Private run folders: `host/road-lod-census/gcc-initial` and `clang-final`.
The first conservative-only estimate is retained separately; its p99 of 768
including setup was too loose to decide feasibility. Exact HOST clipping
resolved that uncertainty without changing the thresholds.

Replay SHA-256: `e0ae1fe82819253a53086aae6489339214245cafa9b6164fda5eecfc2cea267e`.
Road JSON SHA-256: `f0fd05ec5a7e4ce416dac3e042e5f00660dab00999b58dd98328d753c904b34c`.

Next: C2's receipt-bound `FZXRLD01` pack, compact repetition metadata, bounded
far tiers and offline source-texture tiers. C3 must still pass its image-space
controls and measured CPU budget on the actual native renderer.
