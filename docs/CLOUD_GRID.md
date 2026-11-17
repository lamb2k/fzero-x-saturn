# C5 fixed cloud grid: HOST no-go

A bounded cloud grid fits both recorded race paths, but its clipped texture mapping
loses most of the visible cloud pattern at reviewed views. **Do not integrate or
publish the prototype renderer.** The native build, emulator and hardware are unrun;
this experiment establishes neither speed nor acceptable appearance.

## Candidate and evidence boundary

The local experiment replaces recursive cloud subdivision with at most twelve
64x64-texel cells. The four-cell axis can follow either source U or V: 4x3 or 3x4.
Texture orientation is unchanged. Clipping can produce up to four fan commands per
cell, with a hard cap of 48 commands. No runtime pixel conversion is proposed.

Original 64x32 IA8 cloud art is repeated twice vertically into each 64x64 cell.
Eight prebaked coverage variants total **65,536 bytes**. Repetition preserves the
source texels and period; RGB1555 intensity, ordered coverage and eight fade levels
are adaptations. They do not reproduce the source's fractional alpha. Each clipped
fan piece currently receives the entire cell texture, which changes its source UV
mapping. This last adaptation is the visual blocker.

Evidence is private under `host/cloud-grid` in `$FZX_EVIDENCE`. Source cameras and
backdrop state are read-only. The selected replay's cameras match the independently
sealed selected-source fixture. Legacy and selected replay settings remain distinct.

| HOST corpus | Inputs | Maximum cells | Maximum emitted commands | Fully transparent native layers |
| --- | ---: | ---: | ---: | ---: |
| Legacy tuning | 9,907 | 9 | 12 | 4 |
| Selected tuning | 7,059 | 12 | 12 | 6 |

Both full grid outputs are byte-identical across GCC and Clang with UBSan. Maximum
guard-adapted cells is five in either corpus. Source input snapshots remain unchanged.
Synthetic tests cover off identity, bounds/capacity failures, roll, viewing from above,
both grid orientations, rejection of 4x4 coverage and the opacity boundary. Ten
negative mutations fail under each compiler.

## Preserved failures and transparent-layer correction

- Fixed 4x3 coverage fails selected input 1,878 because it requires 2x4 cells.
  Allowing the explicit 3x4 orientation closes that extent failure without changing
  the twelve-cell or command caps.
- The first transposed prototype fails input 4,285: extrapolating a full cell from
  nearly collapsed source UVs exceeds its signed 32-bit view-space coordinates.
  Input 6,484 also has a zero source UV determinant. Both failures are preserved.
- At these inputs, all eight source fade values select native variant seven, whose
  entire 8,192-byte image is transparent. Source-half interpolation cannot select a
  lower variant between those vertices. The grid therefore culls a proven
  transparent native layer before inversion. The 236/237 fade boundary and a
  degenerate-UV visible control are tested; this is not an arbitrary geometry skip.

All 16,966 source layers still have some nonzero fractional opacity by their vertex
values. Only four legacy and six selected layers become entirely transparent under
the declared eight-level native conversion. These counts must not be presented as
source-invisible layers. The existing legacy comparison renderer itself fails
selected input 6,484; its selected comparison is partial through input 6,483.

## Appearance and causal isolation

Thirteen private cloud-only comparisons include selected inputs 1,878, 4,285 and
6,484. The source proxy independently intersects original triangles and samples IA8
with fractional alpha. The native proxy decodes the actual RGB1555 banks onto output
quads. Neither proxy models exact RDP filtering or VDP1 edge stepping. The shared
purple backdrop isolates cloud content; these are not final scene captures.

At two visibly failing poses, separate diagnostic substitutions hold output geometry
and original texels fixed while replacing only fade evaluation or only UV sampling:

| HOST view | Current grid visible pixels | Source fade only | Source UV only |
| --- | ---: | ---: | ---: |
| Legacy input 1,200 | 139 | 347 | 7,964 |
| Selected input 1,878 | 251 | 337 | 7,781 |

Here "visible" counts pixels different from the shared backdrop. Fade-only changes
leave the broad cloud pattern missing. Source UV sampling restores that pattern,
identifying full-cell remapping onto clipped fan pieces as the dominant failure at
these views. Some cells also cross the source halves' minimum-fade seam, so a single
full-cell midpoint is an additional approximation. Brightness and tolerances were
not adjusted to obtain these results.

The diagnostic source-UV and per-pixel fade substitutions are **HOST isolation
probes, not implemented Saturn capabilities**. Their remaining quantization error
is not a visual acceptance result.

## Unblock condition

Provide a native-compatible mapping that retains the original cloud pattern through
guard/fan clipping within the existing twelve-cell, 48-command and 65,536-byte caps.
Then repeat both full-route geometry gates and source/legacy comparisons at the
failed poses, source-half crossings, orientation transitions and near-plane cases,
followed by motion and native scene review. Increasing caps or substituting art is
not part of this experiment.

The copied legacy projection/divider prototype stays local. If a viable mapping is
found, use the existing SH2ZAM `view-center-1` projection and source-order guard APIs
through a narrow adapter, proving output parity and boundary behavior first. No
such adapter, native integration or timing qualification was performed here.

Receipts: `geometry-census.json`, `transparent-census.json`, `images/comparison.json`,
`diagnostic/causes.json`, `prototype/receipt.json`, and `final.receipt.json` beneath
`host/cloud-grid`. The earlier dual-RBG probe is a separate C5 investigation.
