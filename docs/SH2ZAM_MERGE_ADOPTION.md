# SH2ZAM view-merge-1 adoption

The port pins `view-merge-1`, commit
`1a5c14cd6b9f29ac1ac09f20b5e9579193f03db9`, in the pointer-only commit
`9ff1601`. Qualification preceded the pointer change and used synthetic
fixtures only. No library source was edited. The unrelated integer kernels
are not added to the port's build.

## Independent qualification

GCC and Clang with UBSan pass the upstream view, edge, guard and merge suites.
Clang ASan/UBSan also passes the merge suite. The merge sweep checks 120,000
strips and 6,507,825 runs against the independent 128-bit oracle; all five
upstream mutation controls are detected. The 10,120,500-sample forward mapping
property stays within the boundary maximum, apart from floating-point noise.

The old view source is an exact prefix of the new file: only the merge planner
was appended. The existing 4,096-vertex caller retains hash `1cbb2030` before
and after under both compilers. Its 32,768 edge samples and near-clip cases also
remain identical. This is HOST evidence, not native binary or timing evidence.

The new public caller fixture checks the port's {1,2,4} masks, partial-start
and partial-end alignment, clipping marker `0x80`, source Q5 row scaling,
output guards and unchanged output on rejection. Removing the clipping marker
or breaking the end alignment makes its controls fail.

## Port census after qualification

The private C3 probe now calls the tagged planner instead of its temporary
boundary test. It rebases source Q5 rows into increasing unsigned values;
pixel mode is invariant to that common row scale. End masks allow only complete
periods starting at phase zero, within the chosen maximum tier. Corners that
will need the current guard clip receive `0x80` before planning. The requested
mode is `SH2Z_VIEW_MERGE_PIXELS`, with tolerance 256 (1 px).

All 9,907 census records and 13 held command streams are byte-identical to the
previous HOST probe at each of the three tolerances. GCC/Clang agree for 1 px.
There are zero unaligned texture starts, and source state remains unchanged.

| Tolerance | Commands p99 / max | Screen error p99 | Source / tier texel p99 |
| --- | --- | --- | --- |
| 0.5 px | 424 / 451 | 0.500 px | 8.000 / 3.868 |
| 1 px | 413 / 445 | 0.650 px | 9.328 / 3.633 |
| 2 px | 406 / 432 | 1.109 px | 9.333 / 3.620 |

Only 1 px passes both requested p99 limits. Its worst individual held-view
texture p99 is 0.954 px; geometry coverage is within 1 px in every view.
Forced first tier fails the budget at p99 821 / max 1,033. The wrong-repetition
image control fails at 127.509 px p99. The image method, tails, exclusions and
native limitations are retained in [ROAD_STREAM.md](ROAD_STREAM.md).

## Phase starts and the remaining shifted fixture

The historical ×4 arm has 75 held commands whose start phase modulo the source
period is 16 or 20. Its encoded addresses match its formula, but the formula
rounds partial source starts down to a tier row: these cases lose up to 3.65625
source rows. This is a separate phase quantization problem, not evidence of a
bad texture-bank address. The new policy leaves partials single and merges only
complete phase-zero periods. Its source-UV audit covers 1,824 held textured
commands, including 509 merged runs and 691 partials, with zero wrong start rows.

The unchanged +32 synthetic fixture still fails: 32 commands versus 31 in the
frozen renderer. Its first extra piece is quad 0's final partial repetition.
The projected endpoint is Y=191 instead of Y=192, creating a one-pixel sliver.
The regular fixture passes, and the merge planner is not responsible: the
failure also occurs with single repetitions forced everywhere.

The issue is at the projector/adapter boundary. For example, with source centre
Y=112, focal length 160, view Y=-600 and Z=1000, the source final coordinate is
208. The current centre-minus-one adapter gives the library centre 111, and
the tagged projection gives 207. At Z=1200 the pair is 192 versus 191. The
library's documented quotient-floor rule is internally consistent; the port
adaptation is not exact at integer boundaries. A tiny view-Y bias was tested
previously and fixes these fixtures, but remains a diagnostic, not a general
rounding contract or an adopted camera change.

There is no blocker in the merge interface. Native stream acceptance still
requires a principled projector/adapter resolution that passes the unchanged
shifted fixture, followed by residency, guard clipping, VDP1 look and one-CPU
timing. Road CPU is UNRUN; no runtime speed or console acceptance is claimed.

Private evidence: `host/view-merge-adoption/` for synthetic qualification and
`host/road-stream-draw/tagged-merge-20261117/` for the subsequent census,
image checks, phase audit and shifted-fixture diagnostic. Retail-derived
inputs, commands and images remain outside git.
