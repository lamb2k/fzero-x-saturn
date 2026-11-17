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

## Integral-centre caller, 2026-11-17

The separate `fzx_view16_from_source_trunc` adapter requires integral Q16
centres, preserves X/Y without bias, and accepts the full int16 centre range.
The original adapter remains unchanged for countdown/vehicle callers.
No native caller selects the new adapter in this checkpoint.

GCC and Clang with UBSan pass 9,807 independent frozen-source projection cases
per compiler, including negative screen coordinates, translated eyes, exact
208/192 rows, rotation/clip conversion and transactional rejection. Controls
using the old rounding, restoring Y-minus-one, or accepting fractional centres
all fail. Seven targeted caller/reference suites pass; the legacy 4,096-vertex
hash remains `1cbb2030`. The unchanged regular and +32 road fixtures now pass
in the private experimental caller, with zero regular-fixture edge error.

The full route is still blocked: 223 of 9,907 source views have fractional Y
centres. Five are 193 minus 1/65,536 and 218 are 193 plus 1/65,536 pixels.
The first is input 506. Both compilers agree; these are the converted source
projection values, not an adapter error. They are rejected, never rounded.
The library owner is preparing an opt-in Q16-centre contract.

The admitted 9,684-view subset has p99/max 413/445 adaptive commands and
822/1,033 forced-first commands. This excludes the rejected views and cannot
establish a full-route pass. All thirteen held views are integral. Using the
reference clipper with consistent truncation, their texture displacement p99
is 0.650 px, geometry coverage is within 1 px, and the wrong-repetition control
fails at 127.684 px. Source/tier texel p99 are 9.305/3.619; 1,078 of 472,110
samples exceed 1 px (maximum 2.5). There are 258 material mismatches and
10,116 common mean-row pixels excluded from the phase metric.

A second blocker is the guard clipper's polygon anchor. Its cyclic corner
order differs from the reference, changing the VDP1 fan and full-rectangle
texture mapping even when the polygon coverage matches. With library clipping,
held texture p99 is 365.124 px (forced-first 362.721), while geometry remains
within 1 px. The first held difference is input 300. The reference visits the
closing edge before vertex zero; the library starts with vertex zero and its
following edge. The library owner received this distinct contract request.
No replacement clip kernel or camera bias was added to the port.

A mixed-rounding diagnostic also failed and remains preserved; its leftover
old projection call was corrected in the separate consistent experiment.
The source-UV audit still finds zero start-phase errors in 1,824 commands.
Evidence and scripts: `host/road-stream-draw/trunc-20261117/`. Native clipping,
residency, VDP1 images and one-CPU timing remain unqualified.
