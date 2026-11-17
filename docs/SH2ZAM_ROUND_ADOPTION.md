# SH2ZAM view-round-1 adoption

The pointer-only commit `4e943c7` pins `view-round-1` at
`7ed2330480b5341f88c2c60d6f25704d95606a93`. Independent synthetic qualification
preceded the pointer change. No library source was edited, and no port caller
was changed by that commit.

## Contract

The opt-in `_trunc` batch, project, edge, near-clip and guard-clip APIs truncate
the final screen coordinate toward zero. This matches the frozen road renderer's
Q16 position followed by C division by 65,536. Portable and `_divu` forms share
the contract. The prior APIs keep their documented quotient-rounding behavior.

The port's current centre-minus-one adapter is approximate at integer projected
boundaries and at negative screen coordinates. The new rule permits an integer
source centre without that bias. Fractional source centres need rejection or a
separately defined representation; silently rounding them cannot establish exact
source projection. Clip inputs and all projecting paths for one road view must
use the same rule.

## HOST qualification

GCC 13.3 and Clang 18.1 with strict warnings and UBSan pass the new revision's
six selected upstream suites and the previous revision's five suites. The new
rounding suite runs 17,728,412 checks per compiler, including all ten portable
and HOST `_divu` entry points. Five deliberately altered expected-result rules
are detected: floor X, ceil Y, asymmetric quotient clamp, omitted fraction check
and omitted guard clamp. These controls alter the independent oracle, not the
library implementation.

An independent recorder exercises the ten old entry points over 2,048 synthetic
views, including rejection paths and initialized output buffers. Its 28,868,608
output bytes are identical across both revisions and both compilers:

`934b8bc04fcd30f776e7153a2b92178009cc7254d03635f2646f8a6ede2fb242`.

All five existing port view/adapter/edge/merge tests pass before and after the
pointer change. The 4,096-vertex caller retains hash `1cbb2030`; its edge and
guard checks and merge-policy controls remain unchanged. These local checks
used the same preserved core WIP on both sides of the pointer change.

Upstream CMake Release uses effective `-O3`; the independent recorder uses
`-O2`. HOST `_divu` executes its portable fallback. This does not exercise the
SH-2 divider assembly, establish native program identity or validate timing.

## Separate caller gate

The road caller still needs its explicit truncation adapter and consistent
`_trunc` paths, followed by the unchanged regular/+32 fixtures, full original
9,907-view census, phase audit and held-image checks. The shared legacy adapter
also serves countdown and vehicle rendering, whose behavior must remain
unchanged when the road experiment is disabled.

The final-coordinate rule does not remove differences from Q14 rotation,
stepped-edge quantization or guard-plane crossings. No camera bias is adopted.
Native residency, VDP1 output, one-CPU cost and whole-scene performance remain
separate gates. No emulator, hardware or road-image acceptance follows from
this dependency adoption.

Private synthetic evidence and reproducible commands reside in
`host/view-round-adoption/`. Original assets and retail-derived captures remain
outside git.
