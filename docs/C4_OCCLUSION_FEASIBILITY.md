# C4 offline occlusion feasibility

## Current acceptance and ownership, 2026-11-17

Codex owns C4 draw order; Opus owns performance on `claude/c4-perf`.
C4 packet/stat identity and source renderer arithmetic identity are lifted.
Judge ordering visually against the original game's image: screen-pixel
displacement, colour, occlusion defects and popping. No numeric visual threshold
is set. A source-geometric proof or zero-error match to the old oracle is not a
prerequisite; review held-out native images and motion, including transparency
and equal-depth materials. SH2ZAM source-matching tags remain optional.

Keep the vehicle70 command budget, 16 KiB unused stack, allocation guards,
no crashes/hangs, legacy default, hygiene/ROM rules and honest evidence tiers.

## Historical feasibility evidence

The following results preserve earlier oracle measurements and failures. Their
exact tolerances and proposed proof requirements are superseded by the visual
rules above. The observed ordering defects still need native visual review;
lifting identity gates does not turn these results into appearance acceptance.

HOST sampling supports a new mechanism: derive fixed orders from actual overlap
constraints instead of centroid keys. It does not reopen C4's native look or speed
gates. No pack, runtime, library, native build or emulator run changed.

The existing perspective-depth oracle was reused on its same 15 legacy-route
views, including held input 624 and the 300/301 octant crossing. At every covered
pixel, each back face must precede the visible front face. A second census checks
all overlapping face pairs, including occluded pairs. Both graphs are acyclic in
all five sampled LOD/octant groups. A deterministic topological order per group
reproduces the sampled depth image with **zero wrong overlap pixels**, versus 572
for the existing eight centroid orders. Reversing these orders is a failing
control: 2,165 wrong overlap pixels.

This is a fit to those same sampled views, not an independent held-out validation
or a proof over an octant. The graph uses opaque geometric coverage at pixel
centers in crop (96,112)..(224,224), exact source transforms, exact back-face
culling and perspective inverse-depth interpolation. It does not model material
transparency, texture colors, lighting, Saturn raster edges or native conservative
culling. Equal depths within 1e-10 inverse-depth units are treated as ties. The
old oracle compares face ownership; this follow-up compares geometric depth and
therefore deliberately ignores equal-depth ownership differences.

Two bounded hypotheses were tested:

| Mechanism | Sample ordering | Sample command implication |
| --- | --- | --- |
| Retain paired faces, derive fixed per-octant constraint orders | Zero cyclic groups; zero wrong depth pixels | Maximum 70 exactly front-facing faces, 63 touching crop pixels |
| Unpair into original source triangles, then derive orders | Zero cyclic groups; zero wrong depth pixels | Maximum 83 exactly front-facing triangles, 75 touching crop pixels; exceeds 70 even before native conservative margins |

The triangle partition is unnecessary for this sample and worsens the budget.
The face-order result warrants offline qualification, not another sorting-key
experiment. Do not infer that reversing every merged pair is an acceptable
partition.

## Full legacy-route extension

The same fixed-face-order hypothesis was extended to all 9,907 saved inputs,
using sparse raster bounds over the complete 320 by 224 view. This took 81.9
seconds on HOST after a 700-input, 6.4-second benchmark. There are 9,846 visible
states and 61 source-invisible states; no near-plane exclusions occurred. All
12 observed LOD/octant visible-front constraint graphs are acyclic. Fixed orders
fit all their collected constraints, but that remains a fit to this route.
Unlike the 15-view census, this larger pass collects only constraints against
the visible front face, not every pair hidden behind it. This is sufficient for
the opaque depth image, and insufficient to qualify transparent materials.

Even inputs train one fixed order per LOD/octant; odd inputs are held out.
These training orders fail 15 distinct held-out constraints in 11 views across
five groups. Thus finite sampled training has not established a camera-domain
ordering guarantee. Private evidence records the exact source-face indices and
first witness input for each failed precedence. Reversing the full-fit orders
fails constraints in 9,654 views; full-fit orders violate none. These are graph
validation counts, not newly rendered pixel-error counts.

The exact front-face maximum rises to 71 at inputs 544, 545 and 546; the maximum
number touching any full-frame pixel is 66. Neither is the native conservative
emitted-command count. A bounded bake must still cover unseen camera directions,
transparency, equal-depth material precedence and native culling/raster behavior.
No cycles were found that justify a spatial face partition on this opaque route;
if broader constraints cycle, report the exact edge/view witnesses and partition
only those source faces while rechecking the command budget.

## Independent selected-route validation

The full legacy-union orders were frozen and applied without retraining to the
separate selected-tuning 7,059-input route. This is a genuine held-out failure:
**three wrong visible depth pixels in three views**, one each at inputs 2471,
2475 and 6873. All are LOD 1 / octant 2. The first needs source face 101 before
15; the other two need face 86 before 83. The frozen order violates those
precedences. The tolerance and 70-command requirement were not relaxed.

All 7,059 selected states were visible; there were no near-plane exclusions or
unknown LOD/octant groups. All nine selected constraint graphs, and their unions
with the legacy constraints, are acyclic. This diagnosis does not change the
frozen orders or turn the failed validation into a pass. Exact front-facing and
pixel-bearing maxima are both 66, with no over-70 exact-front views. Occluded
faces were not silently removed from the exact-front count, and it is not a
native emitted-command bound.

Selected snapshots use character 0, skin 1 and engine 0.5078125 with the qualified
selected inputs. GCC and Clang snapshots agree; every camera packet matches the
existing selected oracle, repeated machine snapshots agree and the snapshot
leaves camera state unchanged. This capture is a private HOST-only bridge build.
No native image was built or launched. Pixel validation took 61.8 seconds.

Private evidence: `host/c4-occlusion-selected/source.receipt`,
`validation-7059.json`, `frozen-order-witnesses.json`,
`combined-cycle-diagnostic.json`, `views-7059.json` and `analysis.receipt`.
The report seals the frozen legacy-order hash and selected state hash.
Private generators: `generated/c4-capture-selected.py` and
`generated/c4-occlusion-selected.py`.

The mechanism remains promising but unqualified: simply fitting the three new
constraints would use the held-out data as training. The next meaningful unblock
is a source-geometric camera-domain ordering proof or a separately qualified
bounded offline direction partition with new independent coverage, including
transparent/equal-depth material behavior. Native look, cost and command gates
remain required. There is no evidence here requiring geometric face splitting.

C4 remains blocked. The full legacy HOST native-command census is still p99 67,
maximum 112; the sampled and full-route exact-culling counts cannot replace it. Existing
machine cost remains 9.273 ms MODELLED versus 1 ms, and native appearance/memory
requirements remain open. Unblock requires a qualified order across the actual
camera domain, emitted-command maximum at most 70, native still comparison,
1 ms bounded vehicle work and the combined memory/frame gates.

Private evidence: `host/c4-occlusion-feasibility/report.json`, `faces-views.json`
and `triangles-views.json`. The report records source-manifest, legacy-state and
analysis-script SHA256 hashes. Private script:
`generated/c4-occlusion-feasibility.py`. All 15 states come from the **9,907-input
legacy** domain. Selected-route validation is separately reported above and
fails; neither result qualifies native appearance. HARDWARE remains UNRUN.

Full-route private evidence: `host/c4-occlusion-full/report-9907.json`,
`views-9907.json`, `edges-9907.json` and `validation.json`. Original 15-view
evidence is preserved. Both evidence folders have SHA256 analysis receipts.
New private generators are `generated/c4-occlusion-full.py` and
`generated/c4-occlusion-validate.py`; report hashes identify the actual census
script/source/state inputs. This remains HOST geometry feasibility only.
